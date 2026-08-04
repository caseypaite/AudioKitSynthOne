//
//  PortAudioBackend.cpp
//  AudioKitSynthOne - Linux port
//
//  PortAudio output. More forgiving than the JACK backend when the local
//  PipeWire/JACK setup varies, at the cost of a little latency.
//

#include "AudioBackend.h"

#include <portaudio.h>
#include <cstdio>

#include <cstring>
#include <vector>

namespace s1 {

class PortAudioBackend : public AudioBackend {
public:
    ~PortAudioBackend() override {
        stop();
        if (mStream != nullptr) {
            Pa_CloseStream(mStream);
            mStream = nullptr;
        }
        if (mInitialised) {
            Pa_Terminate();
            mInitialised = false;
        }
    }

    bool open(double requestedRate, uint32_t requestedFrames, double requestedLatencySec,
              std::string &error) override {
        const PaError initRc = Pa_Initialize();
        if (initRc != paNoError) {
            error = std::string("Pa_Initialize: ") + Pa_GetErrorText(initRc);
            return false;
        }
        mInitialised = true;

        const PaDeviceIndex device = Pa_GetDefaultOutputDevice();
        if (device == paNoDevice) {
            error = "PortAudio found no default output device";
            return false;
        }

        const PaDeviceInfo *info = Pa_GetDeviceInfo(device);
        mSampleRate = requestedRate > 0 ? requestedRate : info->defaultSampleRate;
        mBufferFrames = requestedFrames > 0 ? requestedFrames : 256;

        PaStreamParameters params{};
        params.device = device;
        params.channelCount = 2;
        params.sampleFormat = paFloat32 | paNonInterleaved;
        // PortAudio takes this as the request and the buffer size as a hint,
        // so passing the device default made --buffer irrelevant to latency:
        // 64 and 128 frames both came back at 8.71ms on the machine this was
        // written on. One period is the floor a callback API can offer and
        // measured never worse than the old default at any buffer size:
        //
        //   buffer   was      now
        //     64    8.71ms   1.45ms
        //    128    8.71ms   2.90ms
        //    256   11.61ms   5.80ms
        //   1024   23.22ms  23.22ms
        //
        // Asking for two periods instead is not a safer middle ground -- it
        // rounds to the same value as 1.5 and is worse than the old default
        // above 256 frames. Use --latency if you want slack for a busy box.
        const double periodSec = static_cast<double>(mBufferFrames) / mSampleRate;
        params.suggestedLatency =
            requestedLatencySec > 0.0 ? requestedLatencySec : periodSec;
        params.hostApiSpecificStreamInfo = nullptr;

        const PaError rc = Pa_OpenStream(&mStream, nullptr, &params, mSampleRate, mBufferFrames,
                                         paClipOff, &PortAudioBackend::callbackTrampoline, this);
        if (rc != paNoError) {
            error = std::string("Pa_OpenStream: ") + Pa_GetErrorText(rc);
            return false;
        }

        if (const PaStreamInfo *streamInfo = Pa_GetStreamInfo(mStream)) {
            mSampleRate = streamInfo->sampleRate;
            mOutputLatencySec = streamInfo->outputLatency;
        }
        mDeviceName = info->name ? info->name : "default";
        if (const PaHostApiInfo *api = Pa_GetHostApiInfo(info->hostApi)) {
            mDeviceName = std::string(api->name) + " / " + mDeviceName;
        }
        return true;
    }

    bool start(RenderCallback render, std::string &error) override {
        mRender = std::move(render);
        const PaError rc = Pa_StartStream(mStream);
        if (rc != paNoError) {
            error = std::string("Pa_StartStream: ") + Pa_GetErrorText(rc);
            return false;
        }
        mActive = true;
        return true;
    }

    void stop() override {
        if (mActive && mStream != nullptr) {
            Pa_StopStream(mStream);
            mActive = false;
        }
    }

    const char *name() const override { return "portaudio"; }
    double sampleRate() const override { return mSampleRate; }
    uint32_t bufferFrames() const override { return mBufferFrames; }
    std::string description() const override {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "  (%.2f ms out)", mOutputLatencySec * 1000.0);
        return mDeviceName + buf;
    }

private:
    static int callbackTrampoline(const void *, void *output, unsigned long frames,
                                  const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags,
                                  void *userData) {
        return static_cast<PortAudioBackend *>(userData)->callback(output,
                                                                   static_cast<uint32_t>(frames));
    }

    int callback(void *output, uint32_t frames) {
        auto **channels = static_cast<float **>(output);
        float *left = channels[0];
        float *right = channels[1];

        if (mRender) {
            mRender(left, right, frames);
        } else {
            std::memset(left, 0, frames * sizeof(float));
            std::memset(right, 0, frames * sizeof(float));
        }
        return paContinue;
    }

    PaStream      *mStream = nullptr;
    RenderCallback mRender;
    double         mSampleRate = 44100.0;
    uint32_t       mBufferFrames = 256;
    bool           mInitialised = false;
    bool           mActive = false;
    std::string    mDeviceName;
    double         mOutputLatencySec = 0.0;
};

std::unique_ptr<AudioBackend> makePortAudioBackend() {
    return std::make_unique<PortAudioBackend>();
}

} // namespace s1
