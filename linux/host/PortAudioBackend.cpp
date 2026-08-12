//
//  PortAudioBackend.cpp
//  AudioKitSynthOne - Linux port
//
//  PortAudio output. More forgiving than the JACK backend when the local
//  PipeWire/JACK setup varies, at the cost of a little latency.
//

#include "AudioBackend.h"

#include <portaudio.h>
#ifdef _WIN32
#include <pa_win_wasapi.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef _WIN32
#  include <pthread.h>
#  include <sched.h>
#endif

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
std::vector<PaDeviceIndex> candidateOutputDevices(const std::string &hostApi, int deviceIndex,
                                                  std::string &error) {
    std::vector<PaDeviceIndex> devices;

    // An explicit device answers the question outright. It is deliberately not
    // followed by fallbacks: someone who picked a device wants to be told it
    // failed, not to be moved silently to another one.
    if (deviceIndex != kAutoDevice) {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(static_cast<PaDeviceIndex>(deviceIndex));
        if (info == nullptr || info->maxOutputChannels < 2) {
            error = "device " + std::to_string(deviceIndex) +
                    " is not a stereo output on this machine";
            return {};
        }
        return {static_cast<PaDeviceIndex>(deviceIndex)};
    }

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

/// Holds PortAudio's global init for as long as it is in scope. Pa_Initialize
/// is reference counted, so enumerating devices while a stream is open is safe
/// and leaves that stream alone.
struct PortAudioSession {
    bool ok = false;
    PortAudioSession() { ok = Pa_Initialize() == paNoError; }
    ~PortAudioSession() { if (ok) Pa_Terminate(); }
    PortAudioSession(const PortAudioSession &) = delete;
    PortAudioSession &operator=(const PortAudioSession &) = delete;
};

} // namespace

std::vector<OutputDevice> portAudioOutputDevices() {
    PortAudioSession session;
    if (!session.ok) return {};

    const PaDeviceIndex fallback = Pa_GetDefaultOutputDevice();
    const PaDeviceIndex count = Pa_GetDeviceCount();

    std::vector<OutputDevice> devices;
    for (PaDeviceIndex i = 0; i < count; ++i) {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
        // Stereo out or nothing: the synth renders two channels and has no
        // mixdown, so a mono or capture-only device would only fail later.
        if (info == nullptr || info->maxOutputChannels < 2) continue;

        OutputDevice device;
        device.index = static_cast<int>(i);
        device.name = info->name != nullptr ? info->name : "output";
        device.defaultSampleRate = info->defaultSampleRate;
        device.maxChannels = info->maxOutputChannels;
        device.isDefault = (i == fallback);
        if (const PaHostApiInfo *api = Pa_GetHostApiInfo(info->hostApi)) {
            device.hostApi = api->name != nullptr ? api->name : "";
        }
        devices.push_back(std::move(device));
    }
    return devices;
}

class PortAudioBackend : public AudioBackend {
public:
    PortAudioBackend(std::string hostApi, int deviceIndex, bool wasapiExclusive)
        : mHostApi(std::move(hostApi)), mDeviceIndex(deviceIndex),
          mWasapiExclusive(wasapiExclusive) {}

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

        const std::vector<PaDeviceIndex> candidates =
            candidateOutputDevices(mHostApi, mDeviceIndex, error);
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

#ifdef _WIN32
            // Exclusive mode is what actually cuts latency -- shared mode (the
            // default) always goes through Windows' own mixer. It is also
            // pickier about the stream format, so a device that refuses it
            // below still falls through to the next candidate rather than
            // failing outright.
            PaWasapiStreamInfo wasapiInfo{};
            const PaHostApiInfo *deviceApi = Pa_GetHostApiInfo(info->hostApi);
            const bool isWasapi = deviceApi != nullptr && deviceApi->type == paWASAPI;
            if (mWasapiExclusive && isWasapi) {
                wasapiInfo.size = sizeof(PaWasapiStreamInfo);
                wasapiInfo.hostApiType = paWASAPI;
                wasapiInfo.version = 1;
                wasapiInfo.flags = paWinWasapiExclusive;
                params.hostApiSpecificStreamInfo = &wasapiInfo;
            }
#endif

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
        // Escalate to real-time scheduling on the first call, which is the
        // first time we are certain we are on the audio callback thread.
        // SCHED_FIFO 95 matches the limit in /etc/security/limits.d/synthone-audio.conf
        // and the LimitRTPRIO= in the kiosk unit. Fails silently when the
        // system has not been configured for it (non-kiosk installs).
#ifndef _WIN32
        if (!mRtSet.exchange(true, std::memory_order_relaxed)) {
            struct sched_param sp{};
            sp.sched_priority = 95;
            const int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
            if (rc != 0) {
                std::fprintf(stderr, "[audio] SCHED_FIFO: %s (run install.sh --kiosk or add audio limits)\n",
                             strerror(rc));
            }
        }
#endif

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
    int            mDeviceIndex = kAutoDevice;
    bool           mWasapiExclusive = false;
    double         mOutputLatencySec = 0.0;
#ifndef _WIN32
    std::atomic<bool> mRtSet{false};
#endif
};

std::unique_ptr<AudioBackend> makePortAudioBackend(const std::string &hostApi, int deviceIndex,
                                                   bool wasapiExclusive) {
    return std::make_unique<PortAudioBackend>(hostApi, deviceIndex, wasapiExclusive);
}

} // namespace s1
