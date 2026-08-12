//
//  MidiSysEx.h
//  AudioKitSynthOne - Linux / Windows port
//
//  SysEx transport shared by both platforms: a reassembled message, the
//  lock-free ring that carries one from the MIDI reader thread to the control
//  thread, and the fragment accumulator both AlsaMidi.cpp and WinMidi.cpp feed
//  raw bytes through. Kept separate from MidiQueue/MidiMessage (MidiInput.h) --
//  that ring is sized and shaped for the per-sample note/CC path; SysEx is
//  rare, bursty (a handful of messages at startup) and variable-length, so
//  bloating every note/CC slot to fit it, or storing heap pointers in that
//  lock-free ring, would only complicate the hot path for a case that isn't on
//  it.
//
//  Also carries ProgramChangeMessage/Queue: a controller driver's hook for
//  detecting a hardware-native "mode" switch (e.g. the Akai MPK Mini mk3's
//  PROG SELECT button choosing a different onboard program). Program Change
//  is a plain 2-byte channel-voice message and could in principle ride the
//  existing MidiQueue, but driver dispatch happens on the control thread (see
//  ControllerDriverManager::dispatchProgramChange), not the render thread --
//  same reasoning as SysEx above, so it gets its own small queue rather than
//  competing with note/CC traffic or forcing driver logic onto the render
//  thread.
//

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s1 {

/// A complete, reassembled SysEx message (0xF0 ... 0xF7 inclusive).
struct SysExMessage {
    // Comfortably covers the Akai MPK Mini mk3's 254-byte program dump with
    // headroom; a message that overflows this is dropped (see
    // SysExAssembler::append) rather than growing dynamically, since this
    // path exists for device-identification handshakes, not as a
    // general-purpose SysEx passthrough.
    static constexpr size_t kMaxLength = 512;
    uint8_t data[kMaxLength] = {0};
    size_t  length = 0;
    // Identifies which input port this arrived on: ALSA client id, or a
    // WinMM device index. -1 = unknown. Lets a driver manager route a reply
    // to the driver that's waiting on it without per-port queues.
    int sourceId = -1;
};

/// Single-producer (MIDI reader thread) / single-consumer (control thread)
/// ring for SysExMessage. Mirrors MidiQueue's design (MidiInput.h) at a
/// smaller capacity, appropriate for rare, bursty traffic rather than a
/// per-sample stream.
class SysExQueue {
public:
    static constexpr size_t kCapacity = 16; // power of two

    bool push(const SysExMessage &m) {
        const size_t head = writeIndex.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & (kCapacity - 1);
        if (next == readIndex.load(std::memory_order_acquire)) return false;
        buffer[head] = m;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    bool pop(SysExMessage &out) {
        const size_t tail = readIndex.load(std::memory_order_relaxed);
        if (tail == writeIndex.load(std::memory_order_acquire)) return false;
        out = buffer[tail];
        readIndex.store((tail + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

private:
    SysExMessage         buffer[kCapacity];
    std::atomic<size_t> writeIndex{0};
    std::atomic<size_t> readIndex{0};
};

/// A Program Change message (status 0xC0|channel, one data byte).
struct ProgramChangeMessage {
    int channel = 0;
    int program = 0;
    // Same meaning as SysExMessage::sourceId: ALSA client id, or a WinMM
    // device index. -1 = unknown.
    int sourceId = -1;
};

/// Single-producer/single-consumer ring for ProgramChangeMessage, mirroring
/// SysExQueue at a smaller capacity still -- a mode switch is a rarer event
/// than even a SysEx handshake.
class ProgramChangeQueue {
public:
    static constexpr size_t kCapacity = 8; // power of two

    bool push(const ProgramChangeMessage &m) {
        const size_t head = writeIndex.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & (kCapacity - 1);
        if (next == readIndex.load(std::memory_order_acquire)) return false;
        buffer[head] = m;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    bool pop(ProgramChangeMessage &out) {
        const size_t tail = readIndex.load(std::memory_order_relaxed);
        if (tail == writeIndex.load(std::memory_order_acquire)) return false;
        out = buffer[tail];
        readIndex.store((tail + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

private:
    ProgramChangeMessage buffer[kCapacity];
    std::atomic<size_t>  writeIndex{0};
    std::atomic<size_t>  readIndex{0};
};

/// Accumulates fragmented SysEx bytes into complete messages. Shared logic for
/// AlsaMidi.cpp (one logical message can in principle arrive split across
/// several SND_SEQ_EVENT_SYSEX events) and WinMidi.cpp (one logical message
/// can arrive split across several MIM_LONGDATA buffer refills, if it's
/// longer than a single buffer). Not thread-safe by itself -- one instance
/// per reader thread/callback, matching how MidiInput already owns one queue
/// per platform.
class SysExAssembler {
public:
    /// Feed raw bytes as received. Returns true and fills `out` when `bytes`
    /// completed a message (ends in 0xF7); otherwise returns false and the
    /// bytes are held for the next call.
    bool append(const uint8_t *bytes, size_t len, SysExMessage &out) {
        for (size_t i = 0; i < len; ++i) {
            const uint8_t b = bytes[i];

            if (b == 0xF0) {
                // A start byte always (re)starts accumulation -- defends
                // against a dropped terminator from an earlier, unrelated
                // message leaving the buffer in a stale state.
                mLen = 0;
                mBuf[mLen++] = b;
                continue;
            }
            if (mLen == 0) continue; // haven't seen a start byte yet; ignore

            // System Realtime bytes (0xF8-0xFF, except 0xF7 which terminates)
            // may legally appear interleaved inside a live SysEx stream per
            // the MIDI spec; skip them rather than treating them as payload.
            if (b >= 0xF8 && b != 0xF7) continue;

            if (mLen < SysExMessage::kMaxLength) {
                mBuf[mLen++] = b;
            } else {
                // Overflow: drop this message and resync on the next 0xF0.
                mLen = 0;
                ++mOverflowCount;
                continue;
            }

            if (b == 0xF7) {
                out.length = mLen;
                std::memcpy(out.data, mBuf, mLen);
                mLen = 0;
                return true;
            }
        }
        return false;
    }

    unsigned overflowCount() const { return mOverflowCount; }

private:
    uint8_t  mBuf[SysExMessage::kMaxLength] = {0};
    size_t   mLen = 0;
    unsigned mOverflowCount = 0;
};

} // namespace s1
