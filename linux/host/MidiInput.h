//
//  MidiInput.h
//  AudioKitSynthOne - Linux / Windows port
//
//  MIDI input, and the lock-free ring that carries events to the audio thread.
//
//  One class, two implementations: ALSA sequencer on Linux (AlsaMidi.cpp) and
//  WinMM on Windows (WinMidi.cpp). Both honour the same contract -- events are
//  produced on some other thread and handed to the audio thread through
//  MidiQueue, so all note handling happens in one place (the render callback)
//  and never races with process().
//

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#ifndef _WIN32
#include <thread>
#endif

namespace s1 {

struct MidiMessage {
    uint8_t data[3] = {0, 0, 0};
    uint8_t length = 0;
};

/// Single-producer (MIDI thread) / single-consumer (audio thread) ring.
class MidiQueue {
public:
    static constexpr size_t kCapacity = 1024; // power of two

    bool push(const MidiMessage &m) {
        const size_t head = writeIndex.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & (kCapacity - 1);
        if (next == readIndex.load(std::memory_order_acquire)) return false;
        buffer[head] = m;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    bool pop(MidiMessage &out) {
        const size_t tail = readIndex.load(std::memory_order_relaxed);
        if (tail == writeIndex.load(std::memory_order_acquire)) return false;
        out = buffer[tail];
        readIndex.store((tail + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

private:
    MidiMessage         buffer[kCapacity];
    std::atomic<size_t> writeIndex{0};
    std::atomic<size_t> readIndex{0};
};

struct MidiSource {
    /// The spec that selects this source on the command line, in whatever form
    /// the platform names its ports: "CLIENT:PORT" on ALSA, a device index on
    /// WinMM. Print this rather than assembling one from the fields below.
    std::string id;
    std::string name;

    // ALSA addressing. On Windows `client` is the WinMM device index and
    // `port` is always 0.
    int client = 0;
    int port = 0;
};

class MidiInput {
public:
    ~MidiInput();

    /// Open the input. `clientName` is the name this synth advertises to other
    /// applications; WinMM has no such concept and ignores it.
    bool open(const std::string &clientName, std::string &error);

    /// Subscribe to the source named by `spec`, or to every available MIDI
    /// source when `spec` is "all". Empty `spec` subscribes to nothing, leaving
    /// the port available for manual connection.
    bool connect(const std::string &spec, std::string &error);

    void start(MidiQueue *queue);
    void stop();

    /// Enumerate readable MIDI sources on the system.
    static std::vector<MidiSource> listSources();

    std::string portName() const { return mPortName; }

#ifdef _WIN32
    /// Called from the WinMM callback thread with a packed short message.
    /// Public only because that callback is a free function.
    void enqueuePacked(uint32_t packed);
#endif

private:
    std::string       mPortName;
    MidiQueue        *mQueue = nullptr;
    std::atomic<bool> mRunning{false};

#ifdef _WIN32
    // HMIDIIN, kept as void* so this header stays free of <windows.h>.
    std::vector<void *> mHandles;
#else
    void run();

    void       *mSeq = nullptr; // snd_seq_t*
    int         mPort = -1;
    std::thread mThread;
#endif
};

} // namespace s1
