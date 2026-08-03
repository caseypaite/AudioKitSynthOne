//
//  AEMessageQueue.h
//  AudioKitSynthOne - Linux port
//
//  Portable replacement for TheAmazingAudioEngine's AEMessageQueue. Upstream
//  marshals an Objective-C selector plus a struct argument from the render
//  thread to the main thread; here the four notifications Synth One actually
//  sends become a tagged struct pushed onto a bounded, allocation-free
//  single-producer/single-consumer ring.
//
//  The render thread calls push(); the host drains it from its own thread via
//  S1AudioUnit::drainMessageQueue().
//

#pragma once

#include <atomic>
#include <cstddef>

#include "S1AudioUnit.h"

struct S1Message {
    enum Kind {
        kNone = 0,
        kDependentParameterDidChange,
        kArpBeatCounterDidChange,
        kHeldNotesDidChange,
        kPlayingNotesDidChange
    };

    Kind kind = kNone;
    union {
        DependentParameter dependentParameter;
        S1ArpBeatCounter   arpBeatCounter;
        HeldNotes          heldNotes;
        PlayingNotes       playingNotes;
    };

    S1Message() : kind(kNone), heldNotes{} {}
};

class AEMessageQueue {
public:
    static constexpr size_t kCapacity = 256; // power of two

    /// Render thread. Drops the message if the consumer has fallen behind --
    /// these are all "latest state wins" notifications, so dropping is safe and
    /// preferable to blocking the audio thread.
    bool push(const S1Message &message) {
        const size_t head = writeIndex.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & (kCapacity - 1);
        if (next == readIndex.load(std::memory_order_acquire)) {
            return false; // full
        }
        buffer[head] = message;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    /// Consumer thread.
    bool pop(S1Message &out) {
        const size_t tail = readIndex.load(std::memory_order_relaxed);
        if (tail == writeIndex.load(std::memory_order_acquire)) {
            return false; // empty
        }
        out = buffer[tail];
        readIndex.store((tail + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

private:
    S1Message           buffer[kCapacity];
    std::atomic<size_t> writeIndex{0};
    std::atomic<size_t> readIndex{0};
};
