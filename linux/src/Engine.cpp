//
//  Engine.cpp
//  AudioKitSynthOne - Linux port
//

#include "Engine.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <sstream>
#include <fstream>
#include <ctime>
#include <cstdio>
#include <cmath>

#include "AEMessageQueue.h"
#include "PlatformPaths.h"
#include "S1DSPKernel.hpp"

namespace fs = std::filesystem;

namespace s1 {

namespace {

constexpr int kFtableSize = S1_FTABLE_SIZE;             // 4096
constexpr int kNumWaveforms = S1_NUM_WAVEFORMS;         // 4
constexpr int kNumBands = S1_NUM_BANDLIMITED_FTABLES;   // 13

/// Index every .json under `root` by stem, so "sawtooth_0000" resolves whether
/// it sits in Sawtooth/ or anywhere else. On iOS the bundle is flat; in the
/// source tree the tables are filed under per-waveform subdirectories.
std::map<std::string, fs::path> indexJson(const fs::path &root) {
    std::map<std::string, fs::path> byStem;
    std::error_code ec;
    if (!fs::exists(root, ec)) return byStem;

    for (auto it = fs::recursive_directory_iterator(root, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".json") continue;
        byStem.emplace(it->path().stem().string(), it->path());
    }
    return byStem;
}

// Preset key -> DSP parameter. Transcribed from Manager+PresetDataManager's
// loadPreset(); the preset format keeps the original VCO-era names for a
// number of parameters the DSP has since renamed.
struct Mapping { S1Parameter parameter; const char *key; };
const Mapping kMappings[] = {
    // delay
    {delayOn, "delayToggled"},
    {delayFeedback, "delayFeedback"},
    {delayMix, "delayMix"},
    {delayTime, "delayTime"},
    {delayInputCutoffTrackingRatio, "delayInputCutoffTrackingRatio"},
    {delayInputResonance, "delayInputResonance"},
    // reverb
    {reverbOn, "reverbToggled"},
    {reverbFeedback, "reverbFeedback"},
    {reverbHighPass, "reverbHighPass"},
    {reverbMix, "reverbMix"},
    {compressorReverbInputRatio, "compressorReverbInputRatio"},
    {compressorReverbWetRatio, "compressorReverbWetRatio"},
    {compressorReverbInputThreshold, "compressorReverbInputThreshold"},
    {compressorReverbWetThreshold, "compressorReverbWetThreshold"},
    {compressorReverbInputAttack, "compressorReverbInputAttack"},
    {compressorReverbWetAttack, "compressorReverbWetAttack"},
    {compressorReverbInputRelease, "compressorReverbInputRelease"},
    {compressorReverbWetRelease, "compressorReverbWetRelease"},
    {compressorReverbInputMakeupGain, "compressorReverbInputMakeupGain"},
    {compressorReverbWetMakeupGain, "compressorReverbWetMakeupGain"},
    // arp / seq
    {arpRate, "arpRate"},
    {arpIsOn, "isArpMode"},
    {arpIsSequencer, "arpIsSequencer"},
    {arpDirection, "arpDirection"},
    {arpInterval, "arpInterval"},
    {arpOctave, "arpOctave"},
    {arpTotalSteps, "arpTotalSteps"},
    {arpSeqTempoMultiplier, "arpSeqTempoMultiplier"},
    // global
    {tempoSyncToArpRate, "tempoSyncToArpRate"},
    {lfo1Rate, "lfoRate"},
    {lfo2Rate, "lfo2Rate"},
    {autoPanFrequency, "autoPanFrequency"},
    {masterVolume, "masterVolume"},
    {isMono, "isMono"},
    {glide, "glide"},
    {widen, "widen"},
    // oscillators
    {index1, "waveform1"},
    {index2, "waveform2"},
    {morph1SemitoneOffset, "vco1Semitone"},
    {morph2SemitoneOffset, "vco2Semitone"},
    {morph2Detuning, "vco2Detuning"},
    {morph1Volume, "vco1Volume"},
    {morph2Volume, "vco2Volume"},
    {morphBalance, "vcoBalance"},
    {subVolume, "subVolume"},
    {subOctaveDown, "subOsc24Toggled"},
    {subIsSquare, "subOscSquareToggled"},
    {fmVolume, "fmVolume"},
    {fmAmount, "fmAmount"},
    {noiseVolume, "noiseVolume"},
    // filter
    {cutoff, "cutoff"},
    {resonance, "resonance"},
    {filterADSRMix, "filterADSRMix"},
    {filterAttackDuration, "filterAttack"},
    {filterDecayDuration, "filterDecay"},
    {filterSustainLevel, "filterSustain"},
    {filterReleaseDuration, "filterRelease"},
    {filterType, "filterType"},
    // amp envelope
    {attackDuration, "attackDuration"},
    {decayDuration, "decayDuration"},
    {sustainLevel, "sustainLevel"},
    {releaseDuration, "releaseDuration"},
    // fx
    {bitCrushSampleRate, "crushFreq"},
    {autoPanAmount, "autoPanAmount"},
    {phaserMix, "phaserMix"},
    {phaserRate, "phaserRate"},
    {phaserFeedback, "phaserFeedback"},
    {phaserNotchWidth, "phaserNotchWidth"},
    // lfo
    {lfo1Index, "lfoWaveform"},
    {lfo1Amplitude, "lfoAmplitude"},
    {lfo2Index, "lfo2Waveform"},
    {lfo2Amplitude, "lfo2Amplitude"},
    {cutoffLFO, "cutoffLFO"},
    {resonanceLFO, "resonanceLFO"},
    {oscMixLFO, "oscMixLFO"},
    {reverbMixLFO, "reverbMixLFO"},
    {decayLFO, "decayLFO"},
    {noiseLFO, "noiseLFO"},
    {fmLFO, "fmLFO"},
    {detuneLFO, "detuneLFO"},
    {filterEnvLFO, "filterEnvLFO"},
    {pitchLFO, "pitchLFO"},
    {bitcrushLFO, "bitcrushLFO"},
    {tremoloLFO, "tremoloLFO"},
    // misc
    {monoIsLegato, "isLegato"},
    {compressorMasterThreshold, "compressorMasterThreshold"},
    {compressorMasterRatio, "compressorMasterRatio"},
    {compressorMasterAttack, "compressorMasterAttack"},
    {compressorMasterRelease, "compressorMasterRelease"},
    {compressorMasterMakeupGain, "compressorMasterMakeupGain"},
    {pitchbendMinSemitones, "pitchbendMinSemitones"},
    {pitchbendMaxSemitones, "pitchbendMaxSemitones"},
    {frequencyA4, "frequencyA4"},
    {oscBandlimitEnable, "oscBandlimitEnable"},
    {transpose, "transpose"},
    {adsrPitchTracking, "adsrPitchTracking"},
};

} // namespace

Engine::Engine() {
    for (int i = 0; i < 128; ++i) mCcToParameter[i].store(-1, std::memory_order_relaxed);
    for (int i = 0; i < 128; ++i) mDeviceDefaultCc[i].store(-1, std::memory_order_relaxed);
    for (int i = 0; i < S1Parameter::S1ParameterCount; ++i) mLearnTaper[i] = 1.0f;
}

Engine::~Engine() {
    if (mKernel) {
        mKernel->destroy();
    }
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

bool Engine::start(double sampleRate, int channels, const std::string &resourceDir,
                   std::string &error) {
    mSampleRate = sampleRate;
    mChannels = channels;
    mResourceDir = resourceDir;

    mAudioUnit = std::make_unique<S1AudioUnit>();
    mKernel = std::make_unique<S1DSPKernel>(channels, sampleRate);
    mKernel->audioUnit = mAudioUnit.get();

    if (!loadWavetables(resourceDir, error)) {
        return false;
    }

    // Wavetables must be uploaded before any voice is initialised, because
    // S1NoteState::init() hands ft_array to its oscillators.
    mKernel->updateWavetableIncrementValuesForCurrentSampleRate();
    mKernel->initializeNoteStates();
    return true;
}

void Engine::setSampleRate(double newRate) {
    if (!mKernel || newRate <= 0.0 || newRate == mSampleRate) return;

    // Mirrors S1AudioUnit.mm's allocateRenderResourcesAndReturnError:, the
    // upstream path CoreAudio drives on a format/sample-rate change: snapshot
    // parameters and the tuning table, reinitialize the kernel, then restore
    // both. destroy()/init() don't touch ft_array (the wavetables), so those
    // don't need re-uploading -- only the increment values that depend on sr.
    const DSPParameters parameters = mKernel->parameters;
    float tuning[128];
    for (int i = 0; i < 128; ++i) tuning[i] = mKernel->getTuningTableFrequency(i);

    mKernel->init(mChannels, newRate);
    mKernel->reset();
    mKernel->restoreValues(parameters);
    mKernel->updateWavetableIncrementValuesForCurrentSampleRate();
    mKernel->initializeNoteStates();

    for (int i = 0; i < 128; ++i) mKernel->setTuningTable(tuning[i], i);
    mKernel->setTuningTableNPO(mNotesPerOctave);

    mSampleRate = newRate;
}

bool Engine::loadWavetables(const std::string &resourceDir, std::string &error) {
    const fs::path tableDir = fs::path(resourceDir) / "DSP" / "BandlimitedWavetables";

    JsonValue names;
    if (!JsonValue::parseFile((tableDir / "bandlimitedWaveforms.json").string(), names, error)) {
        return false;
    }
    if (!names.isArray() || names.arrayValue.size() != static_cast<size_t>(kNumWaveforms * kNumBands)) {
        error = "bandlimitedWaveforms.json should list " +
                std::to_string(kNumWaveforms * kNumBands) + " tables";
        return false;
    }

    JsonValue freqs;
    if (!JsonValue::parseFile((tableDir / "bandlimitedWaveformFrequencies.json").string(), freqs,
                              error)) {
        return false;
    }
    const JsonValue &freqContent = freqs["content"];
    if (!freqContent.isArray() || freqContent.arrayValue.size() < static_cast<size_t>(kNumBands)) {
        error = "bandlimitedWaveformFrequencies.json is missing its content array";
        return false;
    }

    const auto byStem = indexJson(tableDir);

    // Upload in the same order as AKSynthOne.swift: table index is
    // band * kNumWaveforms + waveform, matching bandlimitedWaveforms.json.
    for (int band = 0; band < kNumBands; ++band) {
        mKernel->setBandlimitFrequency(static_cast<uint32_t>(band),
                                       freqContent.arrayValue[band].asFloat());

        for (int wave = 0; wave < kNumWaveforms; ++wave) {
            const int tableIndex = band * kNumWaveforms + wave;
            const std::string stem = names.arrayValue[tableIndex].asString();

            auto found = byStem.find(stem);
            if (found == byStem.end()) {
                error = "missing wavetable " + stem + ".json under " + tableDir.string();
                return false;
            }

            JsonValue table;
            if (!JsonValue::parseFile(found->second.string(), table, error)) {
                return false;
            }
            const JsonValue &content = table["content"];
            if (!content.isArray() || content.arrayValue.size() != static_cast<size_t>(kFtableSize)) {
                error = "wavetable " + stem + " should hold " + std::to_string(kFtableSize) +
                        " samples";
                return false;
            }

            mKernel->setupWaveform(static_cast<uint32_t>(tableIndex), kFtableSize);
            for (int k = 0; k < kFtableSize; ++k) {
                mKernel->setWaveformValue(static_cast<uint32_t>(tableIndex),
                                          static_cast<uint32_t>(k),
                                          content.arrayValue[k].asFloat());
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------------

void Engine::render(float *left, float *right, uint32_t frames) {
    if (!mKernel) return;

    AudioBufferList bufferList;
    bufferList.mNumberBuffers = 2;
    bufferList.mBuffers[0].mNumberChannels = 1;
    bufferList.mBuffers[0].mDataByteSize = frames * sizeof(float);
    bufferList.mBuffers[0].mData = left;
    bufferList.mBuffers[1].mNumberChannels = 1;
    bufferList.mBuffers[1].mDataByteSize = frames * sizeof(float);
    bufferList.mBuffers[1].mData = right;

    mKernel->setBuffer(&bufferList);
    mKernel->process(frames, 0);
}

// ---------------------------------------------------------------------------
// Control thread
// ---------------------------------------------------------------------------

void Engine::noteOn(int noteNumber, int velocity) {
    if (mKernel) mKernel->startNote(noteNumber, velocity);
}

void Engine::noteOff(int noteNumber) {
    if (mKernel) mKernel->stopNote(noteNumber);
}

void Engine::allNotesOff() {
    if (mKernel) mKernel->stopAllNotes();
}

void Engine::panic() {
    if (mKernel) mKernel->resetDSP();
}

void Engine::handleMidi(const uint8_t *data, int length) {
    if (!mKernel || length <= 0) return;

    AUMIDIEvent event{};
    event.eventSampleTime = 0;
    event.length = static_cast<uint8_t>(std::min(length, 3));
    for (int i = 0; i < event.length; ++i) {
        event.data[i] = data[i];
    }

    // The kernel only acts on 3-byte channel messages; pitch bend and learned
    // CCs are applied through the parameter interface instead.
    if (event.length == 3) {
        const uint8_t status = data[0] & 0xF0;
        if (status == 0xE0) {
            const int bend = (static_cast<int>(data[2]) << 7) | static_cast<int>(data[1]);
            setParameter(pitchbend, static_cast<float>(bend));
            return;
        }
        if (status == 0xB0) {
            const int cc = data[1] & 0x7F;
            const int value = data[2] & 0x7F;

            // All-notes-off still belongs to the kernel.
            if (cc != 123) {
                const int armed = mLearnTarget.load(std::memory_order_acquire);
                if (armed >= 0 && armed < S1Parameter::S1ParameterCount) {
                    mCcToParameter[cc].store(armed, std::memory_order_release);
                    mLearnTarget.store(-1, std::memory_order_release);
                    return;
                }
                const int mapped = mCcToParameter[cc].load(std::memory_order_acquire);
                if (mapped >= 0 && mapped < S1Parameter::S1ParameterCount) {
                    const S1Parameter parameter = static_cast<S1Parameter>(mapped);
                    const float position = static_cast<float>(value) / 127.0f;
                    mKernel->setSynthParameter(
                        parameter, positionToValue(parameter, position, mLearnTaper[mapped]));
                    return;
                }

                // No user MIDI-learn binding for this CC -- fall back to a
                // connected controller driver's factory default, if any.
                const int deviceDefault = mDeviceDefaultCc[cc].load(std::memory_order_acquire);
                if (deviceDefault >= 0 && deviceDefault < S1Parameter::S1ParameterCount) {
                    const S1Parameter parameter = static_cast<S1Parameter>(deviceDefault);
                    const float position = static_cast<float>(value) / 127.0f;
                    mKernel->setSynthParameter(
                        parameter, positionToValue(parameter, position, mLearnTaper[deviceDefault]));
                    return;
                }
            }
        }
    }
    mKernel->handleMIDIEvent(event);
}

void Engine::setParameter(S1Parameter parameter, float value) {
    if (mKernel) mKernel->setSynthParameter(parameter, value);
}

float Engine::getParameter(S1Parameter parameter) const {
    return mKernel ? mKernel->getSynthParameter(parameter) : 0.f;
}

float Engine::minimum(S1Parameter parameter) const {
    return mKernel ? mKernel->minimum(parameter) : 0.f;
}

float Engine::maximum(S1Parameter parameter) const {
    return mKernel ? mKernel->maximum(parameter) : 0.f;
}

float Engine::defaultValue(S1Parameter parameter) const {
    return mKernel ? mKernel->defaultValue(parameter) : 0.f;
}

void Engine::setDependentParameter(S1Parameter parameter, float value01, int payload) {
    if (mKernel) mKernel->setDependentParameter(parameter, value01, payload);
}

float Engine::getDependentParameter(S1Parameter parameter) const {
    return mKernel ? mKernel->getDependentParameter(parameter) : 0.f;
}

float Engine::positionToValue(S1Parameter parameter, float position, float taper) const {
    const float lo = minimum(parameter);
    const float hi = maximum(parameter);
    if (position < 0.f) position = 0.f;
    if (position > 1.f) position = 1.f;
    if (taper <= 0.f) taper = 1.f;
    return lo + (hi - lo) * std::pow(position, taper);
}

float Engine::valueToPosition(S1Parameter parameter, float value, float taper) const {
    const float lo = minimum(parameter);
    const float hi = maximum(parameter);
    if (hi - lo < 1e-9f) return 0.f;
    if (taper <= 0.f) taper = 1.f;
    float norm = (value - lo) / (hi - lo);
    if (norm < 0.f) norm = 0.f;
    if (norm > 1.f) norm = 1.f;
    return std::pow(norm, 1.f / taper);
}

std::string Engine::parameterName(S1Parameter parameter) const {
    return mKernel ? mKernel->presetKey(parameter) : std::string();
}

bool Engine::parameterNamed(const std::string &name, S1Parameter &out) const {
    if (!mKernel) return false;
    for (int i = 0; i < S1Parameter::S1ParameterCount; ++i) {
        const S1Parameter p = static_cast<S1Parameter>(i);
        if (mKernel->presetKey(p) == name) {
            out = p;
            return true;
        }
    }
    return false;
}

std::vector<std::string> Engine::parameterNames() const {
    std::vector<std::string> names;
    if (!mKernel) return names;
    names.reserve(S1Parameter::S1ParameterCount);
    for (int i = 0; i < S1Parameter::S1ParameterCount; ++i) {
        names.push_back(mKernel->presetKey(static_cast<S1Parameter>(i)));
    }
    return names;
}

void Engine::setObserver(S1Protocol *observer) {
    if (mAudioUnit) mAudioUnit->s1Delegate = observer;
}

int Engine::drainNotifications() {
    return mAudioUnit ? mAudioUnit->drainMessageQueue() : 0;
}

// ---------------------------------------------------------------------------
// MIDI learn
// ---------------------------------------------------------------------------

void Engine::armMidiLearn(S1Parameter parameter) {
    const int value = (parameter >= S1Parameter::S1ParameterCount)
                          ? -1
                          : static_cast<int>(parameter);
    mLearnTarget.store(value, std::memory_order_release);
}

bool Engine::midiLearnArmed() const {
    return mLearnTarget.load(std::memory_order_acquire) >= 0;
}

S1Parameter Engine::midiLearnTarget() const {
    const int value = mLearnTarget.load(std::memory_order_acquire);
    return value < 0 ? S1Parameter::S1ParameterCount : static_cast<S1Parameter>(value);
}

int Engine::ccForParameter(S1Parameter parameter) const {
    for (int cc = 0; cc < 128; ++cc) {
        if (mCcToParameter[cc].load(std::memory_order_acquire) == static_cast<int>(parameter)) {
            return cc;
        }
    }
    return -1;
}

void Engine::clearMidiLearn(S1Parameter parameter) {
    for (int cc = 0; cc < 128; ++cc) {
        if (mCcToParameter[cc].load(std::memory_order_acquire) == static_cast<int>(parameter)) {
            mCcToParameter[cc].store(-1, std::memory_order_release);
        }
    }
}

void Engine::clearAllMidiLearn() {
    for (int cc = 0; cc < 128; ++cc) mCcToParameter[cc].store(-1, std::memory_order_release);
    mLearnTarget.store(-1, std::memory_order_release);
}

void Engine::setMidiLearnTaper(S1Parameter parameter, float taper) {
    if (parameter < S1Parameter::S1ParameterCount) {
        mLearnTaper[static_cast<int>(parameter)] = taper > 0.f ? taper : 1.f;
    }
}

S1Parameter Engine::deviceDefaultForCc(int cc) const {
    if (cc < 0 || cc >= 128) return S1Parameter::S1ParameterCount;
    const int mapped = mDeviceDefaultCc[cc].load(std::memory_order_acquire);
    return (mapped >= 0 && mapped < S1Parameter::S1ParameterCount)
               ? static_cast<S1Parameter>(mapped) : S1Parameter::S1ParameterCount;
}

void Engine::setDeviceDefaultCc(int cc, S1Parameter parameter) {
    if (cc < 0 || cc >= 128 || parameter >= S1Parameter::S1ParameterCount) return;
    mDeviceDefaultCc[cc].store(static_cast<int>(parameter), std::memory_order_release);
}

void Engine::clearDeviceDefaults() {
    for (int cc = 0; cc < 128; ++cc) mDeviceDefaultCc[cc].store(-1, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Tuning
// ---------------------------------------------------------------------------

float Engine::tuningTableFrequency(int noteNumber) const {
    return mKernel ? mKernel->getTuningTableFrequency(noteNumber) : 0.f;
}

void Engine::setTuningTableFrequency(int noteNumber, float frequency) {
    if (mKernel) mKernel->setTuningTable(frequency, noteNumber);
}

void Engine::setTuningNotesPerOctave(int npo) {
    if (npo < 1) npo = 1;
    mNotesPerOctave = npo;
    if (mKernel) mKernel->setTuningTableNPO(npo);
}

void Engine::setTuning12ET() {
    if (!mKernel) return;
    const float a4 = mKernel->getSynthParameter(frequencyA4);
    for (int i = 0; i < 128; ++i) {
        mKernel->setTuningTable(a4 * std::exp2((i - 69) / 12.f), i);
    }
    setTuningNotesPerOctave(12);
    // Not a library entry -- clears the tunings list's selection highlight and
    // (per applyPreset) leaves tuning out of the next preset save, rather than
    // aliasing library index 0.
    mCurrentTuning = -1;
}

bool Engine::loadTunings(const std::string &path, std::string &error) {
    JsonValue doc;
    if (!JsonValue::parseFile(path, doc, error)) return false;

    const JsonValue &list = doc["tunings"];
    if (!list.isArray()) {
        error = "tunings.json has no 'tunings' array";
        return false;
    }

    mTunings.clear();
    for (const auto &entry : list.arrayValue) {
        const JsonValue &master = entry["masterSet"];
        if (!master.isArray() || master.arrayValue.empty()) continue;

        Tuning tuning;
        tuning.name = entry["name"].asString("(unnamed)");
        tuning.masterSet.reserve(master.arrayValue.size());
        for (const auto &v : master.arrayValue) tuning.masterSet.push_back(v.asDouble());
        mTunings.push_back(std::move(tuning));
    }

    if (mTunings.empty()) {
        error = "no usable tunings in " + path;
        return false;
    }
    return true;
}

void Engine::applyMasterSet(const std::vector<double> &masterSet, const std::string &) {
    if (!mKernel || masterSet.empty()) return;

    // Octave-reduce every ratio into [1,2), then sort -- AKTuningTable's
    // tuningTable(fromFrequencies:).
    std::vector<double> ratios;
    ratios.reserve(masterSet.size());
    for (double f : masterSet) {
        if (!(f > 0.0)) continue;
        while (f < 1.0) f *= 2.0;
        while (f >= 2.0) f /= 2.0;
        ratios.push_back(f);
    }
    if (ratios.empty()) return;

    std::sort(ratios.begin(), ratios.end());
    ratios.erase(std::unique(ratios.begin(), ratios.end(),
                             [](double a, double b) { return std::fabs(a - b) < 1e-12; }),
                 ratios.end());

    const int npo = static_cast<int>(ratios.size());

    // Middle C anchors the table, derived from the A4 master tuning exactly as
    // the Tunings panel does:
    //   middleCFrequency = frequencyA4 * exp2((60 - 69) / 12)
    const double middleC = mKernel->getSynthParameter(frequencyA4) * std::exp2((60.0 - 69.0) / 12.0);
    constexpr int kMiddleCNoteNumber = 60;

    for (int note = 0; note < 128; ++note) {
        const double index = static_cast<double>(note - kMiddleCNoteNumber) / npo;
        const double octave = std::floor(index);
        const int degree = note - kMiddleCNoteNumber - static_cast<int>(octave) * npo;
        const int safeDegree = std::clamp(degree, 0, npo - 1);
        const double frequency = middleC * std::pow(2.0, octave) * ratios[safeDegree];
        mKernel->setTuningTable(static_cast<float>(frequency), note);
    }

    setTuningNotesPerOctave(npo);
}

bool Engine::applyTuning(int index) {
    if (index < 0 || index >= static_cast<int>(mTunings.size())) return false;
    applyMasterSet(mTunings[index].masterSet, mTunings[index].name);
    mCurrentTuning = index;
    return true;
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

std::string Engine::defaultResourceDir() {
    // A Windows build is distributed as a folder, not installed behind wrapper
    // scripts the way install.sh does it on Linux, so resources sit next to the
    // executable. The compiled-in path is a build-machine path, useful only when
    // running straight out of the build tree.
    const std::string exeDir = executableDir();
    if (!exeDir.empty()) {
        std::error_code ec;
        const fs::path bundled = fs::path(exeDir) / "resources";
        if (fs::exists(bundled / "Presets", ec)) return bundled.string();
    }
    return S1_DEFAULT_RESOURCE_DIR;
}

std::string Engine::defaultTuningsPath() {
    const std::string exeDir = executableDir();
    if (!exeDir.empty()) {
        std::error_code ec;
        const fs::path bundled = fs::path(exeDir) / "data" / "tunings.json";
        if (fs::exists(bundled, ec)) return bundled.string();
    }
    return S1_TUNINGS_JSON;
}

std::string Engine::defaultUserDataDir() {
#ifdef _WIN32
    // The roaming profile is where a small user-authored document like a preset
    // bank belongs; LOCALAPPDATA is for things that need not follow the user
    // between machines.
    if (const char *appData = std::getenv("APPDATA")) {
        if (*appData != '\0') return (fs::path(appData) / "SynthOne" / "presets").string();
    }
    if (const char *profile = std::getenv("USERPROFILE")) {
        if (*profile != '\0') {
            return (fs::path(profile) / "AppData" / "Roaming" / "SynthOne" / "presets").string();
        }
    }
#else
    if (const char *xdg = std::getenv("XDG_DATA_HOME")) {
        if (*xdg != '\0') return (fs::path(xdg) / "synthone" / "presets").string();
    }
    if (const char *home = std::getenv("HOME")) {
        if (*home != '\0') {
            return (fs::path(home) / ".local" / "share" / "synthone" / "presets").string();
        }
    }
#endif
    return "./synthone-presets";
}

bool Engine::bankIsWritable(const std::string &bank) const {
    for (const auto &b : mBanks) {
        if (b.name == bank) return b.isUser;
    }
    return true; // a bank that does not exist yet will be created in the user dir
}

void Engine::loadBanksFrom(const std::string &dirName, bool isUser) {
    const fs::path dir(dirName);
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;

    std::vector<fs::path> files;
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (const auto &file : files) {
        JsonValue doc;
        std::string parseError;
        if (!JsonValue::parseFile(file.string(), doc, parseError) || !doc.isArray()) {
            // A malformed bank should not sink the others.
            continue;
        }

        Bank bank;
        bank.name = file.stem().string();
        bank.isUser = isUser;
        for (const auto &preset : doc.arrayValue) {
            if (!preset.isObject()) continue;
            PresetInfo info;
            info.position = preset["position"].asInt();
            info.name = preset["name"].asString("Untitled");
            info.bank = preset["bank"].asString(bank.name);
            info.category = preset["category"].asString();
            bank.info.push_back(info);
            bank.presets.push_back(preset);
        }
        if (bank.presets.empty()) continue;

        bool replaced = false;
        for (auto &existing : mBanks) {
            if (existing.name == bank.name) {
                existing = std::move(bank);
                replaced = true;
                break;
            }
        }
        if (!replaced) mBanks.push_back(std::move(bank));
    }
}

bool Engine::loadBanks(std::string &error) {
    const fs::path factoryDir = fs::path(mResourceDir) / "Presets" / "Data";
    std::error_code ec;
    if (!fs::exists(factoryDir, ec)) {
        error = "no preset directory at " + factoryDir.string();
        return false;
    }

    loadBanksFrom(factoryDir.string(), false);
    // User banks are optional; the directory may not exist yet.
    loadBanksFrom(mUserDir, true);

    if (mBanks.empty()) {
        error = "no readable preset banks in " + factoryDir.string();
        return false;
    }
    return true;
}

namespace {

/// Minimal JSON string escaping; preset names and user text are free-form.
std::string jsonEscape(const std::string &in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

/// Serialises a parsed preset object back out. Keys the loader does not know
/// about are preserved verbatim so round-tripping does not lose data.
void writeJsonValue(std::ostringstream &os, const JsonValue &v) {
    switch (v.type) {
        case JsonValue::Type::Null:   os << "null"; break;
        case JsonValue::Type::Bool:   os << (v.boolValue ? "true" : "false"); break;
        case JsonValue::Type::Number: {
            std::ostringstream num;
            num.precision(17);
            num << v.numberValue;
            os << num.str();
            break;
        }
        case JsonValue::Type::String: os << '"' << jsonEscape(v.stringValue) << '"'; break;
        case JsonValue::Type::Array: {
            os << '[';
            for (size_t i = 0; i < v.arrayValue.size(); ++i) {
                if (i) os << ',';
                writeJsonValue(os, v.arrayValue[i]);
            }
            os << ']';
            break;
        }
        case JsonValue::Type::Object: {
            os << '{';
            bool first = true;
            for (const auto &kv : v.objectValue) {
                if (!first) os << ',';
                first = false;
                os << "\n\t\t\"" << jsonEscape(kv.first) << "\": ";
                writeJsonValue(os, kv.second);
            }
            os << "\n\t}";
            break;
        }
    }
}

JsonValue makeNumber(double v) {
    JsonValue j;
    j.type = JsonValue::Type::Number;
    j.numberValue = v;
    return j;
}

JsonValue makeBool(bool v) {
    JsonValue j;
    j.type = JsonValue::Type::Bool;
    j.boolValue = v;
    return j;
}

JsonValue makeString(const std::string &v) {
    JsonValue j;
    j.type = JsonValue::Type::String;
    j.stringValue = v;
    return j;
}

} // namespace

bool Engine::savePreset(const std::string &bankName, int position, const std::string &name,
                        std::string &error) {
    if (!mKernel) {
        error = "engine not started";
        return false;
    }

    Bank *bank = nullptr;
    for (auto &b : mBanks) {
        if (b.name == bankName) { bank = &b; break; }
    }
    if (bank == nullptr) {
        Bank created;
        created.name = bankName;
        created.isUser = true;
        mBanks.push_back(std::move(created));
        bank = &mBanks.back();
    }

    // Saving into a factory bank promotes it to a user bank: the whole bank is
    // written to the user directory and the shipped file is left alone.
    bank->isUser = true;

    // Start from the existing preset when overwriting, so unknown keys survive.
    JsonValue preset;
    int existing = -1;
    for (size_t i = 0; i < bank->info.size(); ++i) {
        if (bank->info[i].position == position) { existing = static_cast<int>(i); break; }
    }
    if (existing >= 0) {
        preset = bank->presets[existing];
    } else {
        preset.type = JsonValue::Type::Object;
    }

    for (const auto &m : kMappings) {
        preset.objectValue[m.key] = makeNumber(mKernel->getSynthParameter(m.parameter));
    }

    // The 16-step arrays keep their original types: ints for transpose, bools
    // for the two toggles.
    JsonValue patternNotes, octBoosts, noteOns;
    patternNotes.type = octBoosts.type = noteOns.type = JsonValue::Type::Array;
    for (int i = 0; i < 16; ++i) {
        patternNotes.arrayValue.push_back(makeNumber(
            std::lround(mKernel->getSynthParameter(
                static_cast<S1Parameter>(sequencerPattern00 + i)))));
        octBoosts.arrayValue.push_back(makeBool(
            mKernel->getSynthParameter(static_cast<S1Parameter>(sequencerOctBoost00 + i)) > 0.5f));
        noteOns.arrayValue.push_back(makeBool(
            mKernel->getSynthParameter(static_cast<S1Parameter>(sequencerNoteOn00 + i)) > 0.5f));
    }
    preset.objectValue["seqPatternNote"] = patternNotes;
    preset.objectValue["seqOctBoost"] = octBoosts;
    preset.objectValue["seqNoteOn"] = noteOns;

    // Tuning: only when the current tuning is a named library entry -- 12-ET
    // (mCurrentTuning == -1 at startup, or after a raw master set applied
    // outside the library) has no name worth inventing, so the key is left
    // out entirely rather than saving something misleading.
    if (mCurrentTuning >= 0 && mCurrentTuning < static_cast<int>(mTunings.size())) {
        const Tuning &tuning = mTunings[mCurrentTuning];
        preset.objectValue["tuningName"] = makeString(tuning.name);
        JsonValue masterSet;
        masterSet.type = JsonValue::Type::Array;
        for (double ratio : tuning.masterSet) masterSet.arrayValue.push_back(makeNumber(ratio));
        preset.objectValue["tuningMasterSet"] = masterSet;
    } else {
        preset.objectValue.erase("tuningName");
        preset.objectValue.erase("tuningMasterSet");
    }

    preset.objectValue["name"] = makeString(name);
    preset.objectValue["bank"] = makeString(bankName);
    preset.objectValue["position"] = makeNumber(position);
    if (!preset.contains("uid")) {
        // Not a real UUID, but unique per save and only used as an identifier.
        char buf[64];
        std::snprintf(buf, sizeof(buf), "linux-%ld-%d",
                      static_cast<long>(std::time(nullptr)), position);
        preset.objectValue["uid"] = makeString(buf);
    }
    if (!preset.contains("isUser")) preset.objectValue["isUser"] = makeBool(true);

    PresetInfo info;
    info.position = position;
    info.name = name;
    info.bank = bankName;

    if (existing >= 0) {
        bank->presets[existing] = preset;
        bank->info[existing] = info;
    } else {
        bank->presets.push_back(preset);
        bank->info.push_back(info);
    }

    // Write the whole bank out to the user directory -- never into the source
    // tree's factory banks.
    const fs::path dataDir(mUserDir);
    std::error_code ec;
    fs::create_directories(dataDir, ec);
    if (ec) {
        error = "cannot create " + dataDir.string() + ": " + ec.message();
        return false;
    }
    const fs::path file = dataDir / (bankName + ".json");

    std::ostringstream os;
    os << "[\n";
    for (size_t i = 0; i < bank->presets.size(); ++i) {
        if (i) os << ",\n";
        os << "\t";
        writeJsonValue(os, bank->presets[i]);
    }
    os << "\n]\n";

    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot write " + file.string();
        return false;
    }
    out << os.str();
    if (!out) {
        error = "failed while writing " + file.string();
        return false;
    }
    return true;
}

std::vector<std::string> Engine::bankNames() const {
    std::vector<std::string> names;
    names.reserve(mBanks.size());
    for (const auto &bank : mBanks) names.push_back(bank.name);
    return names;
}

std::vector<PresetInfo> Engine::presetsInBank(const std::string &bank) const {
    for (const auto &b : mBanks) {
        if (b.name == bank) return b.info;
    }
    return {};
}

bool Engine::applyPreset(const std::string &bankName, int position, std::string &error) {
    if (!mKernel) {
        error = "engine not started";
        return false;
    }

    const Bank *bank = nullptr;
    for (const auto &b : mBanks) {
        if (b.name == bankName) { bank = &b; break; }
    }
    if (bank == nullptr) {
        error = "no bank named '" + bankName + "'";
        return false;
    }

    const JsonValue *preset = nullptr;
    for (size_t i = 0; i < bank->presets.size(); ++i) {
        if (bank->info[i].position == position) {
            preset = &bank->presets[i];
            break;
        }
    }
    if (preset == nullptr) {
        error = "bank '" + bankName + "' has no preset at position " + std::to_string(position);
        return false;
    }


    for (const auto &m : kMappings) {
        // Older presets predate some parameters; fall back to the DSP default
        // rather than leaving the previous preset's value in place.
        const JsonValue &v = (*preset)[m.key];
        const float value = v.isNull() ? mKernel->defaultValue(m.parameter)
                                       : v.asFloat();
        mKernel->setSynthParameter(m.parameter, value);
    }

    // 16-step sequencer arrays
    const JsonValue &patternNotes = (*preset)["seqPatternNote"];
    const JsonValue &octBoosts = (*preset)["seqOctBoost"];
    const JsonValue &noteOns = (*preset)["seqNoteOn"];
    for (int i = 0; i < 16; ++i) {
        if (patternNotes.isArray() && i < static_cast<int>(patternNotes.arrayValue.size())) {
            mKernel->setSynthParameter(static_cast<S1Parameter>(sequencerPattern00 + i),
                                       patternNotes.arrayValue[i].asFloat());
        }
        if (octBoosts.isArray() && i < static_cast<int>(octBoosts.arrayValue.size())) {
            mKernel->setSynthParameter(static_cast<S1Parameter>(sequencerOctBoost00 + i),
                                       octBoosts.arrayValue[i].asBool() ? 1.f : 0.f);
        }
        if (noteOns.isArray() && i < static_cast<int>(noteOns.arrayValue.size())) {
            mKernel->setSynthParameter(static_cast<S1Parameter>(sequencerNoteOn00 + i),
                                       noteOns.arrayValue[i].asBool() ? 1.f : 0.f);
        }
    }

    mKernel->resetSequencer();

    // The render loop clears all voices the first time it sees isMono differ
    // from its own shadow copy (S1DSPKernel+process.mm) -- upstream behaviour
    // that's invisible on iOS because the engine has been running continuously
    // since long before playback starts. Do that reconciliation here instead,
    // synchronously, so a preset that changes mono/poly can't silence a note
    // started between this load and the next render callback.
    mKernel->reconcileMonoPolyOnLoad();

    // Tuning travels with the preset if it was saved with one; absent, the
    // tuning already loaded (from the app or a previous preset) is left as is.
    const JsonValue &tuningName = (*preset)["tuningName"];
    const JsonValue &tuningMasterSet = (*preset)["tuningMasterSet"];
    if (tuningName.isString()) {
        int found = -1;
        for (size_t i = 0; i < mTunings.size(); ++i) {
            if (mTunings[i].name == tuningName.stringValue) { found = static_cast<int>(i); break; }
        }
        if (found >= 0) {
            applyTuning(found);
        } else if (tuningMasterSet.isArray()) {
            std::vector<double> masterSet;
            masterSet.reserve(tuningMasterSet.arrayValue.size());
            for (const auto &v : tuningMasterSet.arrayValue) masterSet.push_back(v.asDouble());
            // A raw set with no matching library entry doesn't correspond to any
            // index -- leave mCurrentTuning unset so a later save doesn't claim
            // an unrelated library name for it.
            applyMasterSet(masterSet, tuningName.stringValue);
            mCurrentTuning = -1;
        }
    } else if (tuningMasterSet.isArray()) {
        std::vector<double> masterSet;
        masterSet.reserve(tuningMasterSet.arrayValue.size());
        for (const auto &v : tuningMasterSet.arrayValue) masterSet.push_back(v.asDouble());
        applyMasterSet(masterSet, "");
        mCurrentTuning = -1;
    }

    return true;
}

} // namespace s1
