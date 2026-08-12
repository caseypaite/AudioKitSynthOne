//
//  PadFilter.h
//  AudioKitSynthOne - Linux / Windows port
//
//  Lets a controller driver claim specific (channel, note) pairs as
//  "function pads" -- buttons that should stop playing notes and trigger
//  something else instead. The MIDI reader thread (AlsaMidi.cpp's run(),
//  WinMidi.cpp's enqueuePacked()) consults PadFilter synchronously, before a
//  note-on/off is ever pushed to MidiQueue, and diverts a claimed one to
//  PadButtonQueue instead.
//
//  This has to happen in the reader thread itself, at the point of
//  reception -- not later. MidiQueue (the note/CC path) is drained inside
//  the audio render callback, which runs far more often (every audio
//  buffer, millisecond-scale) than the control-thread poll loop that drains
//  SysExQueue/ProgramChangeQueue (MidiSysEx.h) and dispatches to a driver.
//  A design that let a claimed note reach MidiQueue and relied on the
//  driver to retroactively tell Engine to ignore it would lose that race
//  and let the note audibly sound before the "ignore this" instruction
//  could possibly arrive. Filtering at the reader thread is the only point
//  where a note can be suppressed before it's too late.
//
//  PadFilter's publish/consult shape mirrors Engine::mDeviceDefaultCc: a
//  driver (control thread) calls claimNote()/unclaimNote()/clear() --
//  release stores -- and the reader thread calls isPadNote() -- acquire
//  loads, wait-free, no locks, no allocation on either side.
//

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace s1 {

class PadFilter {
public:
    PadFilter() {
        for (auto &w : mNoteBits) w.store(0, std::memory_order_relaxed);
        mClaimedChannel.store(-1, std::memory_order_relaxed);
    }

    /// Restricts claimed notes to one MIDI channel (0-15). -1 (the default)
    /// matches any channel -- used when a driver has no confirmed channel
    /// for its pads and would rather match broadly than miss a claim.
    void claimChannel(int channel) {
        mClaimedChannel.store(channel, std::memory_order_release);
    }

    /// Claims `note` (0-127) as a function pad. Out-of-range is silently
    /// ignored, matching this codebase's "implausible input is a no-op, not
    /// an error" style elsewhere in the driver framework.
    void claimNote(int note) {
        if (note < 0 || note > 127) return;
        mNoteBits[static_cast<size_t>(note) >> 5].fetch_or(
            1u << (note & 31), std::memory_order_release);
    }

    void unclaimNote(int note) {
        if (note < 0 || note > 127) return;
        mNoteBits[static_cast<size_t>(note) >> 5].fetch_and(
            ~(1u << (note & 31)), std::memory_order_release);
    }

    /// Unclaims every note and resets the channel restriction. Call this
    /// before re-populating (e.g. a mode switch might reassign which notes
    /// the pad row sends) so a stale claim from a previous state can never
    /// outlive it -- the same reasoning as Engine::clearDeviceDefaults().
    void clear() {
        for (auto &w : mNoteBits) w.store(0, std::memory_order_release);
        mClaimedChannel.store(-1, std::memory_order_release);
    }

    /// Called from the MIDI reader thread. Wait-free: a couple of relaxed-
    /// cost atomic loads and a bit test, no CAS loop, no blocking.
    bool isPadNote(int channel, int note) const {
        if (note < 0 || note > 127) return false;
        const uint32_t word =
            mNoteBits[static_cast<size_t>(note) >> 5].load(std::memory_order_acquire);
        if ((word & (1u << (note & 31))) == 0) return false;
        const int claimedChannel = mClaimedChannel.load(std::memory_order_acquire);
        return claimedChannel < 0 || claimedChannel == channel;
    }

private:
    std::atomic<uint32_t> mNoteBits[4]; // 128 bits, notes 0-127
    std::atomic<int>      mClaimedChannel;
};

/// A note-on/off PadFilter diverted away from MidiQueue. Mirrors
/// ProgramChangeMessage's shape (MidiSysEx.h).
struct PadButtonMessage {
    int  channel  = 0;
    int  note     = 0;
    // A note-on with velocity 0 is the MIDI running-status idiom for
    // note-off; this is normalised to false at the point of capture, the
    // same way Engine::handleMidi normalises it elsewhere.
    bool isNoteOn = false;
    // Same meaning as SysExMessage::sourceId: ALSA client id, or a WinMM
    // device index. -1 = unknown.
    int  sourceId = -1;
};

/// Single-producer (MIDI reader thread) / single-consumer (control thread)
/// ring for PadButtonMessage, mirroring ProgramChangeQueue at the same
/// small capacity -- a button press is a rare, control-rate event.
class PadButtonQueue {
public:
    static constexpr size_t kCapacity = 16; // power of two

    bool push(const PadButtonMessage &m) {
        const size_t head = writeIndex.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & (kCapacity - 1);
        if (next == readIndex.load(std::memory_order_acquire)) return false;
        buffer[head] = m;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    bool pop(PadButtonMessage &out) {
        const size_t tail = readIndex.load(std::memory_order_relaxed);
        if (tail == writeIndex.load(std::memory_order_acquire)) return false;
        out = buffer[tail];
        readIndex.store((tail + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

private:
    PadButtonMessage     buffer[kCapacity];
    std::atomic<size_t>  writeIndex{0};
    std::atomic<size_t>  readIndex{0};
};

} // namespace s1
