//
//  Engine.h
//  AudioKitSynthOne - Linux port
//
//  Host-facing facade over S1DSPKernel. Takes the place of AKSynthOne.swift +
//  Conductor.swift: owns the kernel and its S1AudioUnit, uploads the shipped
//  bandlimited wavetable bank, loads factory preset banks, and renders.
//

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Json.h"
#include "S1AudioUnit.h"
#include "S1Parameter.h"

class S1DSPKernel;

namespace s1 {

/// One entry of a factory bank (Presets/Data/*.json).
struct PresetInfo {
    int         position = 0;
    std::string name;
    std::string bank;
    std::string category;
};

class Engine {
public:
    Engine();
    ~Engine();

    /// Create the DSP kernel and upload the wavetable bank.
    /// `resourceDir` is the AudioKitSynthOne/ source directory (the one holding
    /// DSP/ and Presets/). Returns false and fills `error` on failure.
    bool start(double sampleRate, int channels, const std::string &resourceDir, std::string &error);

    // -- audio thread ------------------------------------------------------

    /// Render `frames` samples into two non-interleaved buffers.
    void render(float *left, float *right, uint32_t frames);

    // -- control thread ----------------------------------------------------

    void noteOn(int noteNumber, int velocity);
    void noteOff(int noteNumber);
    void allNotesOff();
    void panic();

    /// Feed a raw MIDI message (3 bytes: status, data1, data2).
    void handleMidi(const uint8_t *data, int length);

    void  setParameter(S1Parameter parameter, float value);
    float getParameter(S1Parameter parameter) const;
    float minimum(S1Parameter parameter) const;
    float maximum(S1Parameter parameter) const;
    float defaultValue(S1Parameter parameter) const;

    /// Tempo-syncable parameters (lfo1Rate, lfo2Rate, autoPanFrequency,
    /// delayTime, arpSeqTempoMultiplier) are driven as normalised [0,1] values
    /// and may be rewritten by the DSP when arp/tempo-sync changes. `payload`
    /// identifies the originating control so it can ignore its own echo.
    void  setDependentParameter(S1Parameter parameter, float value01, int payload);
    float getDependentParameter(S1Parameter parameter) const;

    /// Knob position <-> parameter value, using the same algebraic taper as
    /// the iOS Knob control (value = min + (max-min) * pos^taper).
    float positionToValue(S1Parameter parameter, float position, float taper) const;
    float valueToPosition(S1Parameter parameter, float value, float taper) const;

    /// Look a parameter up by its preset key ("cutoff", "morph1Volume", ...).
    /// Returns false if the name is unknown.
    bool parameterNamed(const std::string &name, S1Parameter &out) const;
    std::vector<std::string> parameterNames() const;
    std::string parameterName(S1Parameter parameter) const;

    // -- MIDI learn --------------------------------------------------------

    /// Arm learn for `parameter`; the next CC received binds to it.
    /// Pass S1ParameterCount to disarm.
    void  armMidiLearn(S1Parameter parameter);
    bool  midiLearnArmed() const;
    S1Parameter midiLearnTarget() const;

    /// -1 when the parameter has no CC bound.
    int   ccForParameter(S1Parameter parameter) const;
    void  clearMidiLearn(S1Parameter parameter);
    void  clearAllMidiLearn();

    /// Taper used when a CC drives a parameter, so learned knobs track the UI.
    void  setMidiLearnTaper(S1Parameter parameter, float taper);

    // -- tuning ------------------------------------------------------------

    /// The DSP holds a 128-entry midi-note -> frequency table.
    float tuningTableFrequency(int noteNumber) const;
    void  setTuningTableFrequency(int noteNumber, float frequency);
    int   tuningNotesPerOctave() const { return mNotesPerOctave; }
    void  setTuningNotesPerOctave(int npo);

    /// Rebuild the table as 12-tone equal temperament around the current
    /// frequencyA4, matching AKPolyphonicNode.tuningTable.defaultTuning().
    void setTuning12ET();

    /// A scale from the curated microtonal library: a "master set" of ratios
    /// spanning one octave.
    struct Tuning {
        std::string         name;
        std::vector<double> masterSet;
    };

    /// Loads linux/data/tunings.json (extracted from the iOS Tunings library).
    bool loadTunings(const std::string &path, std::string &error);
    const std::vector<Tuning> &tunings() const { return mTunings; }
    int  currentTuningIndex() const { return mCurrentTuning; }

    /// Applies a tuning by index, rebuilding all 128 table entries.
    bool applyTuning(int index);

    /// Applies an arbitrary master set, as AKTuningTable does: octave-reduce
    /// each ratio into [1,2), sort, then repeat across the keyboard.
    void applyMasterSet(const std::vector<double> &masterSet, const std::string &name);

    // -- presets -----------------------------------------------------------

    /// Default location of the shipped resources (the AudioKitSynthOne source
    /// directory holding Presets/ and BandlimitedWavetables/) and of the
    /// tunings library. On Linux these are the paths compiled in at configure
    /// time; on Windows a `resources`/`data` folder beside the executable wins,
    /// since a Windows build ships as a self-contained folder.
    static std::string defaultResourceDir();
    static std::string defaultTuningsPath();

    /// Where user-created presets are written. Defaults to
    /// $XDG_DATA_HOME/synthone/presets (or ~/.local/share/synthone/presets) on
    /// Linux, and %APPDATA%\SynthOne\presets on Windows.
    /// The factory banks under <resourceDir>/Presets/Data are never written to.
    static std::string defaultUserDataDir();
    void setUserDataDir(const std::string &dir) { mUserDir = dir; }
    const std::string &userDataDir() const { return mUserDir; }

    /// Loads the read-only factory banks from `<resourceDir>/Presets/Data`, then
    /// the writable user banks from userDataDir(). A user bank of the same name
    /// replaces the factory one. Safe to call once after start(); banks are
    /// indexed but no preset is applied.
    bool loadBanks(std::string &error);

    /// True when the named bank lives in the user directory and can be written.
    bool bankIsWritable(const std::string &bank) const;
    std::vector<std::string> bankNames() const;
    std::vector<PresetInfo> presetsInBank(const std::string &bank) const;

    /// Apply a preset by bank name and position. Returns false if not found.
    bool applyPreset(const std::string &bank, int position, std::string &error);

    /// Capture the current DSP state as a preset and write it into `bank`,
    /// replacing the preset at `position` if one exists. Always writes to
    /// userDataDir()/<bank>.json -- saving into a factory bank name copies that
    /// bank into the user directory first, leaving the shipped file untouched.
    bool savePreset(const std::string &bank, int position, const std::string &name,
                    std::string &error);

    /// Deliver queued DSP notifications to the observer. Call periodically from
    /// the control thread; never from the audio thread.
    int drainNotifications();
    void setObserver(S1Protocol *observer);

    double sampleRate() const { return mSampleRate; }
    int    polyphony() const { return S1_MAX_POLYPHONY; }

    /// Reinitializes the kernel against a new sample rate -- e.g. a JACK
    /// server that was reconfigured while running -- preserving current
    /// parameters and tuning. Call only while the audio backend is stopped;
    /// the kernel allocates/frees DSP objects here, so this must never run
    /// concurrently with render(). Not needed for PortAudio, which negotiates
    /// a fixed rate at open() and never changes it later.
    void setSampleRate(double newRate);

private:
    bool loadWavetables(const std::string &resourceDir, std::string &error);

    /// Reads every *.json bank in `dir`. A bank whose name is already loaded is
    /// replaced, so calling this for the user directory after the factory one
    /// lets a user bank shadow a shipped one.
    void loadBanksFrom(const std::string &dir, bool isUser);

    std::unique_ptr<S1DSPKernel> mKernel;
    std::unique_ptr<S1AudioUnit> mAudioUnit;

    double mSampleRate = 44100.0;
    int    mChannels = 2;
    int    mNotesPerOctave = 12;
    std::string mResourceDir;
    std::string mUserDir = defaultUserDataDir();

    struct Bank {
        std::string name;
        // Presets are kept as parsed JSON so applyPreset can consult whichever
        // keys a given (possibly older) preset actually carries, falling back
        // to the DSP default for anything absent.
        std::vector<JsonValue> presets;
        std::vector<PresetInfo> info;
        bool isUser = false; // lives in userDataDir(), so writable
    };
    std::vector<Bank> mBanks;

    std::vector<Tuning> mTunings;
    int                 mCurrentTuning = -1;

    // MIDI learn. Touched from the audio thread (CC dispatch) and the UI
    // thread (arming/clearing), so kept lock-free.
    std::atomic<int>   mLearnTarget{-1};
    std::atomic<int>   mCcToParameter[128];
    float              mLearnTaper[S1Parameter::S1ParameterCount];
};

} // namespace s1
