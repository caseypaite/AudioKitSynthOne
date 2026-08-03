//
//  synthone_offline.cpp
//  AudioKitSynthOne - Linux port
//
//  Renders a preset to a WAV file with no audio hardware involved. Useful as a
//  smoke test for the DSP port and for rendering examples on headless machines.
//
//    synthone-offline --bank "Starter Bank" --preset 0 --note 60 out.wav
//

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Engine.h"

namespace {

void writeWav(const std::string &path, const std::vector<float> &interleaved, int sampleRate,
              int channels) {
    const uint32_t frames = static_cast<uint32_t>(interleaved.size() / channels);
    const uint16_t bitsPerSample = 16;
    const uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
    const uint16_t blockAlign = static_cast<uint16_t>(channels * bitsPerSample / 8);
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
    u16(static_cast<uint16_t>(channels));
    u32(static_cast<uint32_t>(sampleRate));
    u32(byteRate);
    u16(blockAlign);
    u16(bitsPerSample);
    out.write("data", 4);
    u32(dataBytes);

    for (float sample : interleaved) {
        if (sample > 1.f) sample = 1.f;
        if (sample < -1.f) sample = -1.f;
        const int16_t s = static_cast<int16_t>(sample * 32767.f);
        out.write(reinterpret_cast<const char *>(&s), 2);
    }
}

[[noreturn]] void usage() {
    std::cerr <<
        "usage: synthone-offline [options] <out.wav>\n"
        "  --resources DIR   AudioKitSynthOne source dir (default: built-in)\n"
        "  --bank NAME       preset bank (default: first available)\n"
        "  --preset N        preset position within the bank (default: 0)\n"
        "  --note N          MIDI note to play (default: 60, repeatable)\n"
        "  --velocity N      note velocity (default: 100)\n"
        "  --seconds S       total render length (default: 4.0)\n"
        "  --hold S          how long to hold the note (default: seconds*0.6)\n"
        "  --rate HZ         sample rate (default: 44100)\n"
        "  --set KEY=VALUE   override a synth parameter after loading the preset\n"
        "  --tuning N        apply tuning N from the library\n"
        "  --list-tunings    list the tuning library, then exit\n"
        "  --save-preset B:P:NAME  save current state to bank B at position P\n"
        "  --learn PARAM=CC  bind a MIDI CC to a parameter, then drive it\n"
        "  --cc CC=VALUE     send a MIDI CC (0-127)\n"
        "  --list            list banks and presets, then exit\n"
        "  --list-params     list settable parameter names, then exit\n";
    std::exit(2);
}

} // namespace

int main(int argc, char **argv) {
    std::string resourceDir = S1_DEFAULT_RESOURCE_DIR;
    std::string bank;
    std::string outPath;
    int presetPosition = 0;
    std::vector<int> notes;
    int velocity = 100;
    double seconds = 4.0;
    double hold = -1.0;
    int sampleRate = 44100;
    bool listOnly = false;
    bool listParams = false;
    bool listTunings = false;
    int  tuningIndex = -1;
    std::string savePresetSpec;
    std::vector<std::pair<std::string,int>> learnBindings;
    std::vector<std::pair<int,int>> ccMessages;
    std::vector<std::pair<std::string, float>> overrides;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) usage();
            return argv[++i];
        };
        if (arg == "--resources") resourceDir = next();
        else if (arg == "--bank") bank = next();
        else if (arg == "--preset") presetPosition = std::stoi(next());
        else if (arg == "--note") notes.push_back(std::stoi(next()));
        else if (arg == "--velocity") velocity = std::stoi(next());
        else if (arg == "--seconds") seconds = std::stod(next());
        else if (arg == "--hold") hold = std::stod(next());
        else if (arg == "--rate") sampleRate = std::stoi(next());
        else if (arg == "--list") listOnly = true;
        else if (arg == "--list-params") listParams = true;
        else if (arg == "--list-tunings") listTunings = true;
        else if (arg == "--tuning") tuningIndex = std::stoi(next());
        else if (arg == "--save-preset") savePresetSpec = next();
        else if (arg == "--learn") {
            const std::string kv = next(); const size_t eq = kv.find('=');
            if (eq == std::string::npos) usage();
            learnBindings.emplace_back(kv.substr(0,eq), std::stoi(kv.substr(eq+1)));
        }
        else if (arg == "--cc") {
            const std::string kv = next(); const size_t eq = kv.find('=');
            if (eq == std::string::npos) usage();
            ccMessages.emplace_back(std::stoi(kv.substr(0,eq)), std::stoi(kv.substr(eq+1)));
        }
        else if (arg == "--set") {
            const std::string kv = next();
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) usage();
            overrides.emplace_back(kv.substr(0, eq), std::stof(kv.substr(eq + 1)));
        }
        else if (arg == "-h" || arg == "--help") usage();
        else if (!arg.empty() && arg[0] == '-') usage();
        else outPath = arg;
    }

    if (notes.empty()) notes.push_back(60);
    if (hold < 0) hold = seconds * 0.6;
    if (!listOnly && !listParams && !listTunings && outPath.empty()) usage();

    s1::Engine engine;
    std::string error;
    if (!engine.start(sampleRate, 2, resourceDir, error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }
    if (!engine.loadBanks(error)) {
        std::cerr << "warning: " << error << "\n";
    }

    {
        std::string tuningError;
        if (!engine.loadTunings(S1_TUNINGS_JSON, tuningError)) {
            std::cerr << "warning: tunings: " << tuningError << "\n";
        }
    }

    if (listTunings) {
        const auto &ts = engine.tunings();
        for (size_t i = 0; i < ts.size(); ++i) {
            std::cout << i << ": " << ts[i].name << "  (" << ts[i].masterSet.size() << ")\n";
        }
        return 0;
    }

    if (listParams) {
        for (const auto &name : engine.parameterNames()) std::cout << name << "\n";
        return 0;
    }

    if (listOnly) {
        for (const auto &b : engine.bankNames()) {
            std::cout << b << "\n";
            for (const auto &p : engine.presetsInBank(b)) {
                std::cout << "  " << p.position << ": " << p.name << "\n";
            }
        }
        return 0;
    }

    if (bank.empty()) {
        const auto banks = engine.bankNames();
        if (!banks.empty()) bank = banks.front();
    }
    if (!bank.empty()) {
        if (!engine.applyPreset(bank, presetPosition, error)) {
            std::cerr << "warning: " << error << " (using defaults)\n";
        } else {
            for (const auto &p : engine.presetsInBank(bank)) {
                if (p.position == presetPosition) {
                    std::cout << "preset " << p.position << ": " << p.name << " [" << bank << "]\n";
                }
            }
        }
    }

    for (const auto &kv : overrides) {
        S1Parameter parameter;
        if (!engine.parameterNamed(kv.first, parameter)) {
            std::cerr << "error: unknown parameter '" << kv.first
                      << "' (try --list-params)\n";
            return 1;
        }
        engine.setParameter(parameter, kv.second);
    }

    if (tuningIndex >= 0) {
        if (!engine.applyTuning(tuningIndex)) {
            std::cerr << "error: no tuning " << tuningIndex << "\n";
            return 1;
        }
        std::cout << "tuning " << tuningIndex << ": "
                  << engine.tunings()[tuningIndex].name
                  << "  npo=" << engine.tuningNotesPerOctave() << "\n";
        for (int i = 0; i < std::min(engine.tuningNotesPerOctave(), 12); ++i) {
            std::cout << "   note " << (60+i) << "  " << engine.tuningTableFrequency(60+i) << " Hz\n";
        }
    }

    // MIDI learn: arm the parameter, then bind it with one CC message.
    for (const auto &b : learnBindings) {
        S1Parameter parameter;
        if (!engine.parameterNamed(b.first, parameter)) {
            std::cerr << "error: unknown parameter '" << b.first << "'\n";
            return 1;
        }
        engine.armMidiLearn(parameter);
        const uint8_t msg[3] = {0xB0, static_cast<uint8_t>(b.second), 64};
        engine.handleMidi(msg, 3);
        std::cout << "learned CC " << b.second << " -> " << b.first
                  << " (cc now " << engine.ccForParameter(parameter) << ")\n";
    }
    for (const auto &cc : ccMessages) {
        const uint8_t msg[3] = {0xB0, static_cast<uint8_t>(cc.first),
                                static_cast<uint8_t>(cc.second)};
        engine.handleMidi(msg, 3);
    }

    if (!savePresetSpec.empty()) {
        const size_t c1 = savePresetSpec.find(':');
        const size_t c2 = savePresetSpec.find(':', c1 + 1);
        if (c1 == std::string::npos || c2 == std::string::npos) usage();
        const std::string b = savePresetSpec.substr(0, c1);
        const int pos = std::stoi(savePresetSpec.substr(c1 + 1, c2 - c1 - 1));
        const std::string nm = savePresetSpec.substr(c2 + 1);
        std::string saveError;
        if (!engine.savePreset(b, pos, nm, saveError)) {
            std::cerr << "error: " << saveError << "\n";
            return 1;
        }
        std::cout << "saved '" << nm << "' to bank " << b << " at " << pos << "\n";
    }

    const uint32_t block = 256;
    const uint32_t totalFrames = static_cast<uint32_t>(seconds * sampleRate);
    const uint32_t holdFrames = static_cast<uint32_t>(hold * sampleRate);

    std::vector<float> left(block), right(block);
    std::vector<float> interleaved;
    interleaved.reserve(totalFrames * 2);

    // Settle before playing. The kernel clears all voices on the first render
    // after mono/poly changes, so a preset that differs from the DSP default
    // would silence a note started beforehand. On iOS the engine is always
    // running and this happens long before the user plays; here it has to be
    // done explicitly.
    for (uint32_t i = 0; i < 4; ++i) {
        std::fill(left.begin(), left.end(), 0.f);
        std::fill(right.begin(), right.end(), 0.f);
        engine.render(left.data(), right.data(), block);
        engine.drainNotifications();
    }

    for (int n : notes) engine.noteOn(n, velocity);
    bool released = false;

    double peak = 0.0, sumSquares = 0.0;
    for (uint32_t pos = 0; pos < totalFrames; pos += block) {
        if (!released && pos >= holdFrames) {
            for (int n : notes) engine.noteOff(n);
            released = true;
        }

        const uint32_t frames = std::min(block, totalFrames - pos);
        std::fill(left.begin(), left.end(), 0.f);
        std::fill(right.begin(), right.end(), 0.f);
        engine.render(left.data(), right.data(), frames);
        engine.drainNotifications();

        for (uint32_t i = 0; i < frames; ++i) {
            interleaved.push_back(left[i]);
            interleaved.push_back(right[i]);
            peak = std::max(peak, static_cast<double>(std::fabs(left[i])));
            peak = std::max(peak, static_cast<double>(std::fabs(right[i])));
            sumSquares += static_cast<double>(left[i]) * left[i];
        }
    }

    writeWav(outPath, interleaved, sampleRate, 2);

    const double rms = std::sqrt(sumSquares / std::max<size_t>(1, interleaved.size() / 2));
    std::cout << "wrote " << outPath << "  " << seconds << "s @ " << sampleRate << " Hz"
              << "  peak=" << peak << "  rms=" << rms << "\n";

    if (peak < 1e-6) {
        std::cerr << "error: output is silent\n";
        return 1;
    }
    return 0;
}
