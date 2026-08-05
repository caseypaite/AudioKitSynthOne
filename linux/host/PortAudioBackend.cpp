//
//  PortAudioBackend.cpp
//  AudioKitSynthOne - Linux port
//
//  PortAudio output. More forgiving than the JACK backend when the local
//  PipeWire/JACK setup varies, at the cost of a little latency.
//

#include "AudioBackend.h"

#include <portaudio.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace s1 {
namespace {

/// The driver families PortAudio can wrap, by the name --host-api accepts.
/// Listed for every platform: which ones are actually compiled in varies, and
/// Pa_HostApiTypeIdToHostApiIndex answers that at runtime.
struct HostApiName {
    const char      *name;
    PaHostApiTypeId  type;
};

constexpr HostApiName kHostApis[] = {
    {"wasapi",      paWASAPI},
    {"directsound", paDirectSound},
    {"mme",         paMME},
    {"wdmks",       paWDMKS},
    {"asio",        paASIO},
    {"alsa",        paALSA},
    {"jack",        paJACK},
    {"oss",         paOSS},
    {"coreaudio",   paCoreAudio},
};

std::string knownHostApis() {
    std::string out;
    for (const auto &entry : kHostApis) {
        if (Pa_HostApiTypeIdToHostApiIndex(entry.type) >= 0) {
            if (!out.empty()) out += " ";
            out += entry.name;
        }
    }
    return out.empty() ? "none" : out;
}

/// The default output device of the named host API, or paNoDevice.
PaDeviceIndex deviceForHostApi(PaHostApiTypeId type) {
    const PaHostApiIndex api = Pa_HostApiTypeIdToHostApiIndex(type);
    if (api < 0) return paNoDevice;
    const PaHostApiInfo *info = Pa_GetHostApiInfo(api);
    return info != nullptr ? info->defaultOutputDevice : paNoDevice;
}

/// Output devices to try, best first.
///
/// With no explicit choice, Pa_GetDefaultOutputDevice() answers MME on Windows.
/// MME is the 1991 API: it is emulated on top of the modern audio engine, its
/// timing is the least dependable of the four, and the latency it reports is
/// the buffer chain PortAudio set up rather than what the driver stack
/// actually adds. WASAPI is the current API and the one to build on, so it is
/// preferred, with DirectSound behind it.
///
/// Note this is a choice of API, not a measured latency win: on the machine
/// this was written on WASAPI *reports* 22 ms against MME's 5.8 ms, and no
/// round-trip measurement was made to settle which is truly lower. --host-api
/// exists precisely so that judgement can be overridden per machine.
///
/// The default is still appended, so a machine where neither opens keeps
/// working.
///
/// On Linux the default (ALSA, or PipeWire's ALSA emulation) is already the
/// right answer and the list is just that one entry.
std::vector<PaDeviceIndex> candidateOutputDevices(const std::string &hostApi,
                                                  std::string &error) {
    std::vector<PaDeviceIndex> devices;

    if (!hostApi.empty()) {
        for (const auto &entry : kHostApis) {
            if (hostApi != entry.name) continue;
            const PaDeviceIndex device = deviceForHostApi(entry.type);
            if (device == paNoDevice) {
                error = "host API '" + hostApi +
                        "' has no output device in this build; available:" " " + knownHostApis();
                return {};
            }
            return {device};
        }
        error = "unknown host API '" + hostApi + "'; this build has: " + knownHostApis();
        return {};
    }

#ifdef _WIN32
    for (PaHostApiTypeId type : {paWASAPI, paDirectSound}) {
        const PaDeviceIndex device = deviceForHostApi(type);
        if (device != paNoDevice) devices.push_back(device);
    }
#endif

    const PaDeviceIndex fallback = Pa_GetDefaultOutputDevice();
    if (fallback != paNoDevice &&
        std::find(devices.begin(), devices.end(), fallback) == devices.end()) {
        devices.push_back(fallback);
    }
    return devices;
}

} // namespace

class PortAudioBackend : public AudioBackend {
public:
    explicit PortAudioBackend(std::string hostApi) : mHostApi(std::move(hostApi)) {}

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

        const std::vector<PaDeviceIndex> candidates = candidateOutputDevices(mHostApi, error);
        if (candidates.empty()) {
            if (error.empty()) error = "PortAudio found no default output device";
            return false;
        }

        // Try each in turn: a device the host API advertises can still refuse
        // the stream (WASAPI shared mode is particular about sample rates), and
        // falling through to the next one beats failing outright.
        PaError rc = paNoDevice;
        for (const PaDeviceIndex device : candidates) {
            const PaDeviceInfo *info = Pa_GetDeviceInfo(device);
            if (info == nullptr) continue;

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

            rc = Pa_OpenStream(&mStream, nullptr, &params, mSampleRate, mBufferFrames,
                               paClipOff, &PortAudioBackend::callbackTrampoline, this);
            if (rc != paNoError) {
                mStream = nullptr;
                continue;
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

        error = std::string("Pa_OpenStream: ") + Pa_GetErrorText(rc);
        return false;
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
    std::string    mHostApi;
    double         mOutputLatencySec = 0.0;
};

std::unique_ptr<AudioBackend> makePortAudioBackend(const std::string &hostApi) {
    return std::make_unique<PortAudioBackend>(hostApi);
}

} // namespace s1
