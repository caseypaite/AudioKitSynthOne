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

class AudioBackend {
public:
    virtual ~AudioBackend() = default;

    /// Connect to the audio server and negotiate the stream format. On success
    /// sampleRate() and bufferFrames() are valid.
    virtual bool open(double requestedRate, uint32_t requestedFrames, std::string &error) = 0;

    /// Begin calling `render` on the realtime thread.
    virtual bool start(RenderCallback render, std::string &error) = 0;

    virtual void stop() = 0;

    virtual const char *name() const = 0;
    virtual double sampleRate() const = 0;
    virtual uint32_t bufferFrames() const = 0;

    /// Backend-specific detail for the status line (JACK ports, PA device...).
    virtual std::string description() const { return {}; }
};

/// Names of the backends compiled into this binary, best first.
std::vector<std::string> availableBackends();

/// Create a backend by name. Returns nullptr and fills `error` if unavailable.
std::unique_ptr<AudioBackend> makeBackend(const std::string &name, std::string &error);

} // namespace s1
