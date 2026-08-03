//
//  Engine.h
//  AudioKitSynthOne - Linux port
//
//  Host-facing facade over S1DSPKernel. Takes the place of AKSynthOne.swift +
//  Conductor.swift: owns the kernel and its S1AudioUnit, uploads the shipped
//  bandlimited wavetable bank, loads factory preset banks, and renders.
//

#pragma once

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

    /// Look a parameter up by its preset key ("cutoff", "morph1Volume", ...).
    /// Returns false if the name is unknown.
    bool parameterNamed(const std::string &name, S1Parameter &out) const;
    std::vector<std::string> parameterNames() const;
    std::string parameterName(S1Parameter parameter) const;

    // -- presets -----------------------------------------------------------

    /// Load every bank in `<resourceDir>/Presets/Data`. Safe to call once after
    /// start(); banks are indexed but no preset is applied.
    bool loadBanks(std::string &error);
    std::vector<std::string> bankNames() const;
    std::vector<PresetInfo> presetsInBank(const std::string &bank) const;

    /// Apply a preset by bank name and position. Returns false if not found.
    bool applyPreset(const std::string &bank, int position, std::string &error);

    /// Deliver queued DSP notifications to the observer. Call periodically from
    /// the control thread; never from the audio thread.
    int drainNotifications();
    void setObserver(S1Protocol *observer);

    double sampleRate() const { return mSampleRate; }
    int    polyphony() const { return S1_MAX_POLYPHONY; }

private:
    bool loadWavetables(const std::string &resourceDir, std::string &error);

    std::unique_ptr<S1DSPKernel> mKernel;
    std::unique_ptr<S1AudioUnit> mAudioUnit;

    double mSampleRate = 44100.0;
    int    mChannels = 2;
    std::string mResourceDir;

    struct Bank {
        std::string name;
        // Presets are kept as parsed JSON so applyPreset can consult whichever
        // keys a given (possibly older) preset actually carries, falling back
        // to the DSP default for anything absent.
        std::vector<JsonValue> presets;
        std::vector<PresetInfo> info;
    };
    std::vector<Bank> mBanks;
};

} // namespace s1
