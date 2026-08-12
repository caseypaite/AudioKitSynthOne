//
//  AudioBackend.h
//  AudioKitSynthOne - Linux port
//
//  Runtime-selectable audio output. Both backends are compiled in when their
//  libraries are present; --backend picks one at startup.
//

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace s1 {

/// Fills two non-interleaved buffers with `frames` samples. Runs on the
/// backend's realtime thread.
using RenderCallback = std::function<void(float *left, float *right, uint32_t frames)>;

/// Fires when the server's sample rate changes out from under an open stream.
/// Only JACK can do this (a running server can be reconfigured by another
/// client); PortAudio negotiates a fixed rate at open() and never revisits it.
/// Runs on the backend's notification thread, which -- like the render thread
/// -- must not block or allocate, so this must do no more than record the new
/// rate for the control thread to act on.
using SampleRateChangeCallback = std::function<void(double newRate)>;

class AudioBackend {
public:
    virtual ~AudioBackend() = default;

    /// Connect to the audio server and negotiate the stream format. On success
    /// sampleRate() and bufferFrames() are valid.
    ///
    /// `requestedLatencySec` is the output latency to aim for; 0 means "derive
    /// it from the buffer size". It matters more than it looks: PortAudio
    /// treats the latency hint as the real request and the buffer size as
    /// advisory, so passing a device default there makes the buffer size have
    /// no effect on latency at all. JACK dictates its own and ignores this.
    virtual bool open(double requestedRate, uint32_t requestedFrames,
                      double requestedLatencySec, std::string &error) = 0;

    /// Begin calling `render` on the realtime thread.
    virtual bool start(RenderCallback render, std::string &error) = 0;

    virtual void stop() = 0;

    virtual const char *name() const = 0;
    virtual double sampleRate() const = 0;
    virtual uint32_t bufferFrames() const = 0;

    /// Install a rate-change notification. Backends that can't change rate
    /// mid-stream simply never call it -- the default does nothing.
    virtual void setSampleRateChangeCallback(SampleRateChangeCallback) {}

    /// Backend-specific detail for the status line (JACK ports, PA device...).
    virtual std::string description() const { return {}; }
};

/// Names of the backends compiled into this binary, best first.
std::vector<std::string> availableBackends();

/// One output a backend can be pointed at.
///
/// `index` is the backend's own device id, meaningful only to the backend that
/// listed it. kAutoDevice means "whatever the backend would have picked on its
/// own", which is what every host used before the device could be chosen.
struct OutputDevice {
    int         index = -1;
    std::string name;
    std::string hostApi;              ///< driver family, for grouping in a picker
    double      defaultSampleRate = 0.0;
    int         maxChannels = 0;
    bool        isDefault = false;    ///< the driver's own default output
};

constexpr int kAutoDevice = -1;

/// Outputs the named backend can open. Never includes an "automatic" entry --
/// callers that offer one should present kAutoDevice themselves, so the list
/// stays a straight report of what the driver found.
///
/// Safe to call while a backend is open: PortAudio's init is reference counted.
std::vector<OutputDevice> availableOutputDevices(const std::string &backend);

/// Resolve what a user typed for --device against a device list: either a
/// numeric index, or a case-insensitive substring of a device name. Returns
/// false and fills `error` when nothing matches or a name is ambiguous.
///
/// "auto" (or an empty spec) selects kAutoDevice.
bool resolveOutputDevice(const std::string &spec, const std::vector<OutputDevice> &devices,
                         int &out, std::string &error);

/// Create a backend by name. Returns nullptr and fills `error` if unavailable.
///
/// `hostApi` narrows PortAudio to one of the driver families it wraps
/// ("wasapi", "directsound", "mme", "alsa", ...); empty means "choose", which
/// on Windows prefers WASAPI over the MME device PortAudio would otherwise
/// default to. It is ignored by backends that wrap a single driver, i.e. JACK.
///
/// `deviceIndex` pins the output to one device from availableOutputDevices();
/// kAutoDevice keeps the old behaviour of letting the backend choose. An
/// explicit device overrides `hostApi`, since the device already implies one.
///
/// `wasapiExclusive` (Windows only) asks PortAudio's WASAPI stream to bypass
/// the shared-mode mixer -- the one route to genuinely low latency on
/// Windows. Ignored outside WASAPI: a device on another host API, or another
/// backend entirely, opens exactly as it would without the flag.
std::unique_ptr<AudioBackend> makeBackend(const std::string &name, const std::string &hostApi,
                                          int deviceIndex, bool wasapiExclusive,
                                          std::string &error);

} // namespace s1
