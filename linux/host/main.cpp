//
//  main.cpp
//  AudioKitSynthOne - Linux port
//
//  Standalone Synth One host. Audio out over JACK or PortAudio; MIDI in over
//  the ALSA sequencer on Linux and WinMM on Windows.
//

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "MidiInput.h"
#include "AudioBackend.h"
#include "Engine.h"

namespace {

std::atomic<bool> gRunning{true};

void handleSignal(int) { gRunning.store(false); }

[[noreturn]] void usage() {
    std::cerr <<
        "AudioKit Synth One (Linux port)\n"
        "\n"
        "usage: synthone [options]\n"
        "\n"
        "  --backend NAME     audio backend: jack | portaudio (default: first available)\n"
        "  --host-api NAME    PortAudio driver family (wasapi, directsound, mme, alsa...);\n"
        "                     default prefers WASAPI on Windows, the system default elsewhere\n"
        "  --device SPEC      output device: an index from --list-devices, part of a\n"
        "                     device name, or 'auto' (default: let the backend choose)\n"
        "  --rate HZ          preferred sample rate (PortAudio only; JACK dictates its own)\n"
        "  --buffer FRAMES    preferred buffer size (PortAudio only)\n"
        "  --latency MS       output latency to aim for (PortAudio only;\n"
        "                     default: two buffers' worth)\n"
        "  --resources DIR    AudioKitSynthOne source dir (default: built-in)\n"
        "  --bank NAME        preset bank to load\n"
        "  --preset N         preset position within the bank (default: 0)\n"
#ifdef _WIN32
        "  --midi PORT        MIDI device index, or 'all' (default: all)\n"
#else
        "  --midi PORT        ALSA source as CLIENT:PORT, or 'all' (default: all)\n"
#endif
        "  --set KEY=VALUE    override a synth parameter (repeatable)\n"
        "  --test-note N      play MIDI note N on a loop, to check audio without a controller\n"
        "  --list             list preset banks and presets, then exit\n"
        "  --list-params      list settable parameter names, then exit\n"
        "  --list-midi        list MIDI sources, then exit\n"
        "  --list-devices     list audio output devices, then exit\n"
        "  --quiet            do not print the running status line\n"
        "\n"
        "Notes are played over MIDI. Ctrl-C to quit.\n";
    std::exit(2);
}

/// Prints DSP notifications; the iOS app drives its UI from these.
class StatusObserver : public S1Protocol {
public:
    void heldNotesDidChange(HeldNotes notes) override {
        heldCount.store(notes.heldNotesCount, std::memory_order_relaxed);
    }
    void arpBeatCounterDidChange(S1ArpBeatCounter counter) override {
        beat.store(counter.beatCounter, std::memory_order_relaxed);
    }
    void playingNotesDidChange(PlayingNotes notes) override {
        int voices = 0;
        for (int i = 0; i < notes.polyphony; ++i) {
            if (notes.playingNotes[i].noteNumber >= 0) ++voices;
        }
        playing.store(voices, std::memory_order_relaxed);
    }

    std::atomic<int> heldCount{0};
    std::atomic<int> beat{0};
    std::atomic<int> playing{0};
};

} // namespace

int main(int argc, char **argv) {
    std::string backendName;
    std::string hostApi;
    std::string resourceDir = s1::Engine::defaultResourceDir();
    std::string userDir;
    std::string bank;
    std::string midiSpec = "all";
    int presetPosition = 0;
    double requestedRate = 0;
    uint32_t requestedFrames = 0;
    double requestedLatencySec = 0.0;
    std::string deviceSpec;
    bool listPresets = false, listParams = false, listMidi = false, quiet = false;
    bool listDevices = false;
    int testNote = -1;
    std::vector<std::pair<std::string, float>> overrides;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) usage();
            return argv[++i];
        };
        if (arg == "--backend") backendName = next();
        else if (arg == "--host-api") hostApi = next();
        else if (arg == "--rate") requestedRate = std::stod(next());
        else if (arg == "--buffer") requestedFrames = static_cast<uint32_t>(std::stoul(next()));
        else if (arg == "--latency") requestedLatencySec = std::stod(next()) / 1000.0;
        else if (arg == "--resources") resourceDir = next();
        else if (arg == "--user-dir") userDir = next();
        else if (arg == "--bank") bank = next();
        else if (arg == "--preset") presetPosition = std::stoi(next());
        else if (arg == "--midi") midiSpec = next();
        else if (arg == "--list") listPresets = true;
        else if (arg == "--list-params") listParams = true;
        else if (arg == "--list-midi") listMidi = true;
        else if (arg == "--device") deviceSpec = next();
        else if (arg == "--list-devices") listDevices = true;
        else if (arg == "--quiet") quiet = true;
        else if (arg == "--test-note") testNote = std::stoi(next());
        else if (arg == "--set") {
            const std::string kv = next();
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) usage();
            overrides.emplace_back(kv.substr(0, eq), std::stof(kv.substr(eq + 1)));
        }
        else usage();
    }

    if (listMidi) {
        for (const auto &source : s1::MidiInput::listSources()) {
            std::cout << source.id << "  " << source.name << "\n";
        }
        return 0;
    }

    if (listDevices) {
        std::string listBackend = backendName;
        if (listBackend.empty()) {
            const auto backends = s1::availableBackends();
            if (backends.empty()) {
                std::cerr << "error: this build has no audio backend compiled in\n";
                return 1;
            }
            listBackend = backends.front();
        }
        for (const auto &device : s1::availableOutputDevices(listBackend)) {
            std::printf("  %3d  %-44s %-10s %6.0f Hz  %dch%s\n", device.index, device.name.c_str(),
                        device.hostApi.c_str(), device.defaultSampleRate, device.maxChannels,
                        device.isDefault ? "  [default]" : "");
        }
        return 0;
    }

    // -- audio backend -----------------------------------------------------
    //
    // Listing presets or parameters needs no audio hardware, so the backend is
    // skipped entirely for those -- otherwise `--list` would fail on a machine
    // with no sound card or no running JACK server.

    const bool listingOnly = listPresets || listParams;

    std::string error;
    std::unique_ptr<s1::AudioBackend> backend;
    double sampleRate = 44100.0;

    if (!listingOnly) {
        const auto backends = s1::availableBackends();
        if (backends.empty()) {
            std::cerr << "error: this build has no audio backend compiled in\n";
            return 1;
        }
        if (backendName.empty()) backendName = backends.front();

        int deviceIndex = s1::kAutoDevice;
        if (!deviceSpec.empty() &&
            !s1::resolveOutputDevice(deviceSpec, s1::availableOutputDevices(backendName),
                                     deviceIndex, error)) {
            std::cerr << "error: " << error << "\n";
            return 1;
        }

        backend = s1::makeBackend(backendName, hostApi, deviceIndex, error);
        if (!backend) {
            std::cerr << "error: " << error << "\n";
            return 1;
        }
        if (!backend->open(requestedRate, requestedFrames, requestedLatencySec, error)) {
            std::cerr << "error: " << error << "\n";
            return 1;
        }
        sampleRate = backend->sampleRate();
    }

    // -- engine ------------------------------------------------------------

    s1::Engine engine;
    if (!engine.start(sampleRate, 2, resourceDir, error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }
    if (!userDir.empty()) engine.setUserDataDir(userDir);
    if (!engine.loadBanks(error)) {
        std::cerr << "warning: " << error << "\n";
    }

    if (listParams) {
        for (const auto &name : engine.parameterNames()) std::cout << name << "\n";
        return 0;
    }
    if (listPresets) {
        for (const auto &b : engine.bankNames()) {
            std::cout << b << "\n";
            for (const auto &p : engine.presetsInBank(b)) {
                std::cout << "  " << p.position << ": " << p.name << "\n";
            }
        }
        return 0;
    }

    std::string presetName;
    if (!bank.empty()) {
        if (!engine.applyPreset(bank, presetPosition, error)) {
            std::cerr << "error: " << error << "\n";
            return 1;
        }
        for (const auto &p : engine.presetsInBank(bank)) {
            if (p.position == presetPosition) presetName = p.name;
        }
    }

    for (const auto &kv : overrides) {
        S1Parameter parameter;
        if (!engine.parameterNamed(kv.first, parameter)) {
            std::cerr << "error: unknown parameter '" << kv.first << "' (try --list-params)\n";
            return 1;
        }
        engine.setParameter(parameter, kv.second);
    }

    StatusObserver observer;
    engine.setObserver(&observer);

    // -- MIDI --------------------------------------------------------------

    s1::MidiQueue midiQueue;
    s1::MidiInput midi;
    std::string midiStatus;
    if (!midi.open("SynthOne", error)) {
        midiStatus = "unavailable (" + error + ")";
    } else {
        std::string connectError;
        if (!midi.connect(midiSpec, connectError)) {
            midiStatus = midi.portName() + " (" + connectError + ")";
        } else {
            midiStatus = midi.portName() +
                         (midiSpec == "all" ? " <- all sources" : " <- " + midiSpec);
        }
        midi.start(&midiQueue);
    }

    // -- render ------------------------------------------------------------

    // MIDI is applied at the top of the render callback so note handling and
    // process() stay on one thread, as they effectively are on iOS.
    auto render = [&engine, &midiQueue](float *left, float *right, uint32_t frames) {
        s1::MidiMessage m;
        while (midiQueue.pop(m)) {
            engine.handleMidi(m.data, m.length);
        }
        engine.render(left, right, frames);
    };

    if (!backend->start(render, error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::cout << "  [engine]  " << backend->sampleRate() << " Hz / " << backend->bufferFrames()
              << " frames / poly " << engine.polyphony() << "\n"
              << "  [audio]   " << backend->name() << "  " << backend->description() << "\n"
              << "  [midi]    " << midiStatus << "\n";
    if (!presetName.empty()) {
        std::cout << "  [preset]  " << presetPosition << ": " << presetName << " [" << bank << "]\n";
    }
    std::cout << "  ...playing, Ctrl-C to quit\n";
    std::cout.flush();

    // Test note, injected through the same queue as real MIDI so it exercises
    // the whole path rather than a shortcut.
    int testPhase = 0;
    auto sendTestNote = [&](bool on) {
        s1::MidiMessage m;
        m.length = 3;
        m.data[0] = on ? 0x90 : 0x80;
        m.data[1] = static_cast<uint8_t>(testNote);
        m.data[2] = on ? 100 : 0;
        midiQueue.push(m);
    };

    while (gRunning.load()) {
        engine.drainNotifications();

        if (testNote >= 0) {
            // 1 s on, 0.5 s off, at the 50 ms tick below.
            if (testPhase == 0) sendTestNote(true);
            if (testPhase == 20) sendTestNote(false);
            if (++testPhase >= 30) testPhase = 0;
        }

        if (!quiet) {
            std::cout << "\r  voices " << std::setw(2) << observer.playing.load()
                      << "   held " << std::setw(3) << observer.heldCount.load()
                      << "   beat " << std::setw(3) << observer.beat.load() << "    "
                      << std::flush;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "\nstopping\n";
    midi.stop();
    engine.allNotesOff();
    // Let the release stage run out before tearing the stream down.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    backend->stop();
    return 0;
}
