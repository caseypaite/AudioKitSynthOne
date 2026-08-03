//
//  Engine.cpp
//  AudioKitSynthOne - Linux port
//

#include "Engine.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <sstream>

#include "AEMessageQueue.h"
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

} // namespace

Engine::Engine() = default;

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

    // The kernel only acts on 3-byte channel messages; pitch bend is applied
    // through the parameter interface instead.
    if (event.length == 3) {
        const uint8_t status = data[0] & 0xF0;
        if (status == 0xE0) {
            const int bend = (static_cast<int>(data[2]) << 7) | static_cast<int>(data[1]);
            setParameter(pitchbend, static_cast<float>(bend));
            return;
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
// Presets
// ---------------------------------------------------------------------------

bool Engine::loadBanks(std::string &error) {
    const fs::path dataDir = fs::path(mResourceDir) / "Presets" / "Data";
    std::error_code ec;
    if (!fs::exists(dataDir, ec)) {
        error = "no preset directory at " + dataDir.string();
        return false;
    }

    std::vector<fs::path> files;
    for (const auto &entry : fs::directory_iterator(dataDir, ec)) {
        if (entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
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
        if (!bank.presets.empty()) {
            mBanks.push_back(std::move(bank));
        }
    }

    if (mBanks.empty()) {
        error = "no readable preset banks in " + dataDir.string();
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

    // Preset key -> DSP parameter. Transcribed from Manager+PresetDataManager's
    // loadPreset(); the preset format keeps the original VCO-era names for a
    // number of parameters the DSP has since renamed.
    struct Mapping { S1Parameter parameter; const char *key; };
    static const Mapping kMappings[] = {
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
    return true;
}

} // namespace s1
