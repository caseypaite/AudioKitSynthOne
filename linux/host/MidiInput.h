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

#include "MidiSysEx.h"

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

    /// Sources actually subscribed to by the most recent connect() call (more
    /// than one when spec=="all"). Empty if connect() was never called,
    /// failed entirely, or was given an empty spec.
    std::vector<MidiSource> connectedSources() const { return mConnected; }

    /// `sysexQueue`/`programChangeQueue` are optional -- pass nullptr to drop
    /// SysEx/Program Change on the floor (SysEx: the pre-existing behaviour;
    /// Program Change: previously delivered piggybacked on `queue` on
    /// Windows only, and dropped entirely on Linux -- see enqueuePacked()
    /// and AlsaMidi.cpp's run()) rather than routing it to a controller
    /// driver.
    void start(MidiQueue *queue, SysExQueue *sysexQueue = nullptr,
              ProgramChangeQueue *programChangeQueue = nullptr);
    void stop();

    /// Enumerate readable MIDI sources on the system.
    static std::vector<MidiSource> listSources();

    std::string portName() const { return mPortName; }

#ifdef _WIN32
    /// Called from the WinMM callback thread with a packed short message.
    /// Public only because that callback is a free function. `handle` is the
    /// HMIDIIN it arrived on, used only to tag a Program Change message's
    /// sourceId (see enqueueSysExFragment for the same lookup on the SysEx
    /// path).
    void enqueuePacked(uint32_t packed, void *handle);

    /// Called from the WinMM callback thread with a MIM_LONGDATA (SysEx)
    /// buffer. Public for the same reason as enqueuePacked. `hdr` is the
    /// MIDIHDR* WinMM handed back; `handle` is the HMIDIIN it arrived on,
    /// used to tag SysExMessage::sourceId and to re-post the buffer.
    void enqueueSysExFragment(void *hdr, void *handle);
#endif

private:
    std::string              mPortName;
    MidiQueue                *mQueue = nullptr;
    SysExQueue                *mSysExQueue = nullptr;
    ProgramChangeQueue        *mProgramChangeQueue = nullptr;
    SysExAssembler             mSysExAssembler;
    std::vector<MidiSource>   mConnected;
    std::atomic<bool>         mRunning{false};

#ifdef _WIN32
    // HMIDIIN, kept as void* so this header stays free of <windows.h>.
    std::vector<void *> mHandles;
    // MIDIHDR* posted for SysEx reception, one entry per buffer; mSysExHeaderOwners
    // holds the HMIDIIN each one was prepared/must be unprepared against
    // (same index). One SysEx assembler per handle would be needed if
    // sourceId must stay accurate across interleaved devices; in practice
    // WinMM serializes callbacks, so a single mSysExAssembler is sufficient.
    std::vector<void *> mSysExHeaders;
    std::vector<void *> mSysExHeaderOwners;
#else
    void run();

    void       *mSeq = nullptr; // snd_seq_t*
    int         mPort = -1;
    std::thread mThread;
#endif
};

} // namespace s1
