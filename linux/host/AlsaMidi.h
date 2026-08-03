//
//  AlsaMidi.h
//  AudioKitSynthOne - Linux port
//
//  ALSA sequencer MIDI input. Events are read on a dedicated thread and handed
//  to the audio thread through a lock-free ring, so all note handling happens
//  in one place (the render callback) and never races with process().
//

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

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
    int client = 0;
    int port = 0;
    std::string name;
};

class AlsaMidiInput {
public:
    ~AlsaMidiInput();

    /// Open a sequencer client named `clientName` with one writable port.
    bool open(const std::string &clientName, std::string &error);

    /// Subscribe to "client:port" (e.g. "24:0"), or to every readable MIDI
    /// source when `spec` is "all". Empty `spec` subscribes to nothing and
    /// leaves the port available for manual connection.
    bool connect(const std::string &spec, std::string &error);

    void start(MidiQueue *queue);
    void stop();

    /// Enumerate readable MIDI sources on the system.
    static std::vector<MidiSource> listSources();

    std::string portName() const { return mPortName; }

private:
    void run();

    void       *mSeq = nullptr; // snd_seq_t*
    int         mPort = -1;
    std::string mPortName;
    MidiQueue  *mQueue = nullptr;
    std::thread mThread;
    std::atomic<bool> mRunning{false};
};

} // namespace s1
