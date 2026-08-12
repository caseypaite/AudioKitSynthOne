//
//  latency_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Round-trip audio latency, measured rather than reported. The README's
//  PortAudio latency figures (per --host-api, per buffer size) are what the
//  driver *claims*; this plays a short click out one device and listens for
//  its echo on another, so it needs a loopback path to mean anything -- a
//  physical cable from line-out to line-in, or a software loopback device
//  (snd-aloop on Linux, a virtual audio cable on Windows). Run it with
//  nothing connected and it reports "no echo detected", not a number.
//
//  A duplex PortAudio stream, not the AudioBackend used elsewhere in this
//  port: AudioBackend is output-only (what the synth engine needs), and
//  cross-correlating an echo needs the exact sample alignment a single
//  callback receiving both input and output gives for free.
//
//    latency_test                          # default in/out devices
//    latency_test --list-devices
//    latency_test --input-device 2 --output-device 1
//    latency_test --dump echo.wav          # inspect the captured click
//

#include <portaudio.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

// Half a second of lead-in so the stream has settled (device warm-up, ring
// buffer fill) before the click plays, then a second and a half of listening
// -- generously more than any real interface's round trip.
constexpr double kLeadInSec = 0.5;
constexpr double kListenSec = 1.5;
constexpr double kClickSec = 0.005; // 5 ms

/// A half-cosine-windowed 1 kHz burst: compact in time (good for finding a
/// sample-accurate echo) but not a bare impulse, which a lot of interfaces'
/// anti-aliasing filters smear across several samples anyway.
std::vector<float> makeClick(double sampleRate) {
    const size_t n = static_cast<size_t>(kClickSec * sampleRate);
    std::vector<float> click(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(n);
        const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * t); // Hann
        const double tone = std::sin(2.0 * kPi * 1000.0 * i / sampleRate);
        click[i] = static_cast<float>(window * tone);
    }
    return click;
}

void writeWav(const std::string &path, const std::vector<float> &mono, int sampleRate) {
    const uint32_t frames = static_cast<uint32_t>(mono.size());
    const uint16_t bitsPerSample = 16;
    const uint32_t byteRate = static_cast<uint32_t>(sampleRate) * bitsPerSample / 8;
    const uint16_t blockAlign = bitsPerSample / 8;
    const uint32_t dataBytes = frames * blockAlign;

    std::ofstream out(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char *>(&v), 2); };

    out.write("RIFF", 4);
    u32(36 + dataBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    u32(16);
    u16(1); // PCM
    u16(1); // mono
    u32(static_cast<uint32_t>(sampleRate));
    u32(byteRate);
    u16(blockAlign);
    u16(bitsPerSample);
    out.write("data", 4);
    u32(dataBytes);

    for (float sample : mono) {
        if (sample > 1.f) sample = 1.f;
        if (sample < -1.f) sample = -1.f;
        const int16_t s = static_cast<int16_t>(sample * 32767.f);
        out.write(reinterpret_cast<const char *>(&s), 2);
    }
}

struct StreamState {
    std::vector<float> playback; // what gets sent out, click included
    std::vector<float> capture;  // what comes back in, same length
    std::atomic<size_t> framesDone{0};
};

int callback(const void *input, void *output, unsigned long frameCount,
            const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *userData) {
    auto *state = static_cast<StreamState *>(userData);
    auto *out = static_cast<float *>(output);
    const auto *in = static_cast<const float *>(input);

    const size_t start = state->framesDone.load(std::memory_order_relaxed);
    const size_t total = state->playback.size();

    for (unsigned long i = 0; i < frameCount; ++i) {
        const size_t pos = start + i;
        out[i] = pos < total ? state->playback[pos] : 0.0f;
        if (pos < total) state->capture[pos] = (in != nullptr) ? in[i] : 0.0f;
    }

    const size_t end = start + frameCount;
    state->framesDone.store(end, std::memory_order_relaxed);
    return end >= total ? paComplete : paContinue;
}

/// Best-match lag of `needle` inside `haystack`, searched from `searchFrom`
/// (the click's own emission point -- an echo cannot arrive before it plays)
/// onward. Returns {lagInSamples, peakCorrelation}; peakCorrelation is
/// normalised by the click's own energy, so 1.0 is a perfect (silent-room,
/// unity-gain loopback) match and near 0 means nothing was found.
std::pair<size_t, double> findEcho(const std::vector<float> &haystack,
                                   const std::vector<float> &needle, size_t searchFrom) {
    double needleEnergy = 0.0;
    for (float v : needle) needleEnergy += static_cast<double>(v) * v;
    if (needleEnergy <= 0.0) return {searchFrom, 0.0};

    size_t bestLag = searchFrom;
    double bestScore = -1.0;

    for (size_t lag = searchFrom; lag + needle.size() <= haystack.size(); ++lag) {
        double dot = 0.0, energy = 0.0;
        for (size_t i = 0; i < needle.size(); ++i) {
            const double h = haystack[lag + i];
            dot += h * needle[i];
            energy += h * h;
        }
        // Normalised cross-correlation: robust to the echo coming back at a
        // different gain than it was sent, which a real interface's input
        // trim makes the common case rather than the exception.
        const double score = (energy > 1e-12) ? dot / std::sqrt(energy * needleEnergy) : 0.0;
        if (score > bestScore) {
            bestScore = score;
            bestLag = lag;
        }
    }
    return {bestLag, bestScore};
}

[[noreturn]] void usage() {
    std::cerr <<
        "usage: latency_test [options]\n"
        "\n"
        "Measures round-trip audio latency: a click goes out, its echo comes back\n"
        "in, and the time between the two is the answer. Needs a loopback path --\n"
        "a cable from line-out to line-in, or a software loopback device -- or\n"
        "there is nothing to measure and the tool says so rather than guessing.\n"
        "\n"
        "  --list-devices        list PortAudio devices, then exit\n"
        "  --input-device N      capture device index (default: system default)\n"
        "  --output-device N     playback device index (default: system default)\n"
        "  --rate HZ             sample rate (default: the device default)\n"
        "  --buffer FRAMES       callback buffer size (default: 256)\n"
        "  --dump FILE.wav       save the captured (input) signal for inspection\n"
        "  --self-test           verify the echo-detection algorithm against a\n"
        "                        synthesised signal, no audio hardware touched\n";
    std::exit(2);
}

/// Exercises findEcho() against a signal built the same way the real capture
/// buffer is (silence, then a delayed/scaled/noisy copy of the click) so the
/// correlation logic is verified without any audio hardware. Prints PASS/FAIL
/// per case and returns true only if every case recovered its known delay to
/// within one sample.
bool selfTest() {
    constexpr double kSampleRate = 44100.0;
    const size_t clickStart = static_cast<size_t>(kLeadInSec * kSampleRate);
    const std::vector<float> click = makeClick(kSampleRate);

    struct Case { size_t delaySamples; float gain; float noise; };
    const Case cases[] = {
        {200, 1.0f, 0.0f},    // near-instant, unity gain, silent room
        {2000, 1.0f, 0.0f},   // ~45 ms, a plausible real round trip
        {2000, 0.3f, 0.0f},   // quiet input trim -- normalised correlation must not care
        {2000, 0.3f, 0.02f},  // and a bit of noise on top
    };

    bool allPassed = true;
    for (const Case &c : cases) {
        const size_t total = clickStart + c.delaySamples + click.size() + 1000;
        std::vector<float> capture(total, 0.0f);
        for (size_t i = 0; i < click.size(); ++i) {
            capture[clickStart + c.delaySamples + i] = click[i] * c.gain;
        }
        if (c.noise > 0.0f) {
            // Deterministic "noise": not random, so the test is reproducible,
            // just decorrelated from the click itself.
            for (size_t i = 0; i < total; ++i) {
                capture[i] += c.noise * static_cast<float>(std::sin(0.37 * i));
            }
        }

        const auto [lag, score] = findEcho(capture, click, clickStart);
        const size_t recovered = lag - clickStart;
        const bool pass = recovered == c.delaySamples && score > 0.3;
        std::cout << "  [self-test] delay=" << c.delaySamples << " gain=" << c.gain
                  << " noise=" << c.noise << "  ->  recovered=" << recovered
                  << " score=" << score << (pass ? "  PASS" : "  FAIL") << "\n";
        allPassed = allPassed && pass;
    }
    return allPassed;
}

} // namespace

int main(int argc, char **argv) {
    int inputDevice = -1, outputDevice = -1;
    double sampleRate = 0.0;
    unsigned long bufferFrames = 256;
    std::string dumpPath;
    bool listDevices = false;
    bool selfTestOnly = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) usage();
            return argv[++i];
        };
        if (arg == "--list-devices") listDevices = true;
        else if (arg == "--input-device") inputDevice = std::stoi(next());
        else if (arg == "--output-device") outputDevice = std::stoi(next());
        else if (arg == "--rate") sampleRate = std::stod(next());
        else if (arg == "--buffer") bufferFrames = std::stoul(next());
        else if (arg == "--dump") dumpPath = next();
        else if (arg == "--self-test") selfTestOnly = true;
        else usage();
    }

    if (selfTestOnly) return selfTest() ? 0 : 1;

    if (Pa_Initialize() != paNoError) {
        std::cerr << "error: Pa_Initialize failed\n";
        return 1;
    }

    if (listDevices) {
        const int count = Pa_GetDeviceCount();
        for (int i = 0; i < count; ++i) {
            const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
            if (info == nullptr) continue;
            std::cout << i << "  " << info->name << "  (in:" << info->maxInputChannels
                     << " out:" << info->maxOutputChannels << ")\n";
        }
        Pa_Terminate();
        return 0;
    }

    if (inputDevice < 0) inputDevice = Pa_GetDefaultInputDevice();
    if (outputDevice < 0) outputDevice = Pa_GetDefaultOutputDevice();
    if (inputDevice == paNoDevice || outputDevice == paNoDevice) {
        std::cerr << "error: no default input/output device (use --list-devices)\n";
        Pa_Terminate();
        return 1;
    }

    const PaDeviceInfo *inInfo = Pa_GetDeviceInfo(inputDevice);
    const PaDeviceInfo *outInfo = Pa_GetDeviceInfo(outputDevice);
    if (inInfo == nullptr || outInfo == nullptr) {
        std::cerr << "error: invalid device index\n";
        Pa_Terminate();
        return 1;
    }
    if (sampleRate <= 0.0) sampleRate = outInfo->defaultSampleRate;

    StreamState state;
    const size_t totalFrames =
        static_cast<size_t>((kLeadInSec + kListenSec) * sampleRate);
    const size_t clickStart = static_cast<size_t>(kLeadInSec * sampleRate);
    std::vector<float> click = makeClick(sampleRate);

    state.playback.assign(totalFrames, 0.0f);
    state.capture.assign(totalFrames, 0.0f);
    for (size_t i = 0; i < click.size() && clickStart + i < totalFrames; ++i) {
        state.playback[clickStart + i] = click[i];
    }

    PaStreamParameters inParams{};
    inParams.device = inputDevice;
    inParams.channelCount = 1;
    inParams.sampleFormat = paFloat32;
    inParams.suggestedLatency = inInfo->defaultLowInputLatency;

    PaStreamParameters outParams{};
    outParams.device = outputDevice;
    outParams.channelCount = 1;
    outParams.sampleFormat = paFloat32;
    outParams.suggestedLatency = outInfo->defaultLowOutputLatency;

    PaStream *stream = nullptr;
    PaError rc = Pa_OpenStream(&stream, &inParams, &outParams, sampleRate, bufferFrames,
                               paClipOff, &callback, &state);
    if (rc != paNoError) {
        std::cerr << "error: Pa_OpenStream: " << Pa_GetErrorText(rc) << "\n";
        Pa_Terminate();
        return 1;
    }

    std::cout << "  [in]   " << inInfo->name << "\n"
              << "  [out]  " << outInfo->name << "\n"
              << "  [rate] " << sampleRate << " Hz, buffer " << bufferFrames << "\n"
              << "  measuring...\n";

    rc = Pa_StartStream(stream);
    if (rc != paNoError) {
        std::cerr << "error: Pa_StartStream: " << Pa_GetErrorText(rc) << "\n";
        Pa_CloseStream(stream);
        Pa_Terminate();
        return 1;
    }

    while (Pa_IsStreamActive(stream) == 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    if (!dumpPath.empty()) {
        writeWav(dumpPath, state.capture, static_cast<int>(sampleRate));
        std::cout << "  wrote " << dumpPath << "\n";
    }

    const auto [lag, score] = findEcho(state.capture, click, clickStart);
    // A silent room and no loopback still correlates with *something*
    // (input noise floor); 0.3 is comfortably above what that produces and
    // comfortably below a real electrical or acoustic echo.
    if (score < 0.3) {
        std::cout << "  no echo detected (best match score " << score << ") -- "
                     "check the loopback connection\n";
        return 1;
    }

    const double roundTripMs =
        (static_cast<double>(lag) - static_cast<double>(clickStart)) / sampleRate * 1000.0;
    std::cout << "  round-trip latency: " << roundTripMs << " ms  (match score "
              << score << ")\n";
    return 0;
}
