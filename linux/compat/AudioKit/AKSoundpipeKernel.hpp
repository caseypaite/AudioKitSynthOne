//
//  AKSoundpipeKernel.hpp
//  AudioKitSynthOne - Linux port
//
//  Replacement for AudioKit's AKDSPKernel / AKSoundpipeKernel / AKOutputBuffered
//  hierarchy. Provides exactly the surface S1DSPKernel derives from and calls
//  into: a Soundpipe context (`sp`), an output buffer pointer, and the virtual
//  hooks the kernel overrides.
//

#pragma once

#include "../AppleTypes.h"
#include "../SoundpipeCompat.h"

// ---------------------------------------------------------------------------
// AKDSPKernel: sample rate / channel bookkeeping plus the virtuals S1DSPKernel
// overrides.
// ---------------------------------------------------------------------------

class AKDSPKernel {
public:
    AKDSPKernel() : channels(2), sampleRateHz(44100.0) {}
    AKDSPKernel(int channelCount, double sampleRate)
        : channels(channelCount), sampleRateHz(sampleRate) {}
    virtual ~AKDSPKernel() = default;

    virtual void init(int channelCount, double sampleRate) {
        channels = channelCount;
        sampleRateHz = sampleRate;
    }

    /// Render `frameCount` frames starting at `bufferOffset` into the output buffer.
    virtual void process(AUAudioFrameCount frameCount, AUAudioFrameCount bufferOffset) = 0;

    /// Parameter ramping. Synth One smooths internally, so the default is a no-op.
    virtual void startRamp(AUParameterAddress address, AUValue value, AUAudioFrameCount duration) {
        (void)address; (void)value; (void)duration;
    }

    virtual void handleMIDIEvent(AUMIDIEvent const &midiEvent) { (void)midiEvent; }

    int    channels;
    double sampleRateHz;
};

// ---------------------------------------------------------------------------
// AKOutputBuffered: owns the pointer the render loop writes through.
// ---------------------------------------------------------------------------

class AKOutputBuffered {
public:
    virtual ~AKOutputBuffered() = default;

    void setBuffer(AudioBufferList *outBufferList) { outBufferListPtr = outBufferList; }

    AudioBufferList *outBufferListPtr = nullptr;
};

// ---------------------------------------------------------------------------
// AKSoundpipeKernel: owns the sp_data context.
// ---------------------------------------------------------------------------

class AKSoundpipeKernel : public AKDSPKernel {
public:
    AKSoundpipeKernel(int channelCount, double sampleRate)
        : AKDSPKernel(channelCount, sampleRate) {
        sp_create(&sp);
        sp->sr = static_cast<int>(sampleRate);
        sp->nchan = channelCount;
    }

    ~AKSoundpipeKernel() override {
        if (sp != nullptr) {
            sp_destroy(&sp);
            sp = nullptr;
        }
    }

    void init(int channelCount, double sampleRate) override {
        AKDSPKernel::init(channelCount, sampleRate);
        if (sp != nullptr) {
            sp->sr = static_cast<int>(sampleRate);
            sp->nchan = channelCount;
        }
    }

protected:
    sp_data *sp = nullptr;
};
