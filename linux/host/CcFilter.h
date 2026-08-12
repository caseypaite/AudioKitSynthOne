//
//  CcFilter.h
//  AudioKitSynthOne - Linux / Windows port
//
//  Lets a controller driver claim specific (channel, CC) pairs it wants to
//  react to directly -- e.g. a transport button that arrives as a Control
//  Change rather than a Note (see ControllerDriver.h's onCC()). The MIDI
//  reader thread (AlsaMidi.cpp's run(), WinMidi.cpp's enqueuePacked())
//  consults CcFilter synchronously, at the same point it already decodes a
//  CC message, and pushes a copy to CcQueue when claimed.
//
//  Unlike PadFilter (see PadFilter.h), this is observe-only: a claimed CC
//  still reaches MidiQueue and Engine::handleMidi() exactly as before,
//  unsuppressed. That's a deliberate difference, not an oversight -- a Note
//  claimed via PadFilter has to be suppressed because letting it through
//  would audibly sound a pitch; a CC has no equivalent hazard; whether it
//  does anything at all depends entirely on Engine::mCcToParameter/
//  mDeviceDefaultCc having something bound for that CC number, and a driver
//  claiming a CC via CcFilter never also binds that same CC through
//  setDeviceDefaultCc() (there would be nothing meaningful to bind a
//  transport button's raw 0-127 value to). So the extra push here is purely
//  additive, the same shape as ProgramChangeQueue's "also feed this to the
//  driver, the pre-existing path is untouched" pattern -- no diversion, no
//  suppression, no new hazard on the note/CC hot path.
//
//  CcFilter's publish/consult shape mirrors PadFilter: a driver (control
//  thread) calls claimCc()/unclaimCc()/claimChannel()/clear() -- release
//  stores -- and the reader thread calls isClaimedCc() -- acquire loads,
//  wait-free, no locks, no allocation on either side.
//

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace s1 {

class CcFilter {
public:
    CcFilter() {
        for (auto &w : mCcBits) w.store(0, std::memory_order_relaxed);
        mClaimedChannel.store(-1, std::memory_order_relaxed);
    }

    /// Restricts claimed CCs to one MIDI channel (0-15). -1 (the default)
    /// matches any channel -- used when a driver has no confirmed channel
    /// for its claimed CCs and would rather match broadly than miss one.
    void claimChannel(int channel) {
        mClaimedChannel.store(channel, std::memory_order_release);
    }

    /// Claims `cc` (0-127) for direct dispatch via ControllerDriver::onCC().
    /// Out-of-range is silently ignored, matching this codebase's
    /// "implausible input is a no-op, not an error" style elsewhere in the
    /// driver framework.
    void claimCc(int cc) {
        if (cc < 0 || cc > 127) return;
        mCcBits[static_cast<size_t>(cc) >> 5].fetch_or(
            1u << (cc & 31), std::memory_order_release);
    }

    void unclaimCc(int cc) {
        if (cc < 0 || cc > 127) return;
        mCcBits[static_cast<size_t>(cc) >> 5].fetch_and(
            ~(1u << (cc & 31)), std::memory_order_release);
    }

    /// Unclaims every CC and resets the channel restriction. Call this
    /// before re-populating (e.g. a mode switch might reassign which CCs
    /// matter) so a stale claim from a previous state can never outlive it
    /// -- the same reasoning as PadFilter::clear().
    void clear() {
        for (auto &w : mCcBits) w.store(0, std::memory_order_release);
        mClaimedChannel.store(-1, std::memory_order_release);
    }

    /// Called from the MIDI reader thread. Wait-free: a couple of relaxed-
    /// cost atomic loads and a bit test, no CAS loop, no blocking.
    bool isClaimedCc(int channel, int cc) const {
        if (cc < 0 || cc > 127) return false;
        const uint32_t word =
            mCcBits[static_cast<size_t>(cc) >> 5].load(std::memory_order_acquire);
        if ((word & (1u << (cc & 31))) == 0) return false;
        const int claimedChannel = mClaimedChannel.load(std::memory_order_acquire);
        return claimedChannel < 0 || claimedChannel == channel;
    }

private:
    std::atomic<uint32_t> mCcBits[4]; // 128 bits, CCs 0-127
    std::atomic<int>      mClaimedChannel;
};

/// A CC event whose (channel, cc) was claimed in a CcFilter, forwarded to
/// ControllerDriverManager::dispatchCc() -> ControllerDriver::onCC() in
/// addition to (not instead of) the ordinary MidiQueue path -- see the file
/// header for why no suppression is needed here.
struct CcMessage {
    int channel  = 0;
    int cc       = 0;
    int value    = 0;
    // Same meaning as SysExMessage::sourceId: ALSA client id, or a WinMM
    // device index. -1 = unknown.
    int sourceId = -1;
};

/// Single-producer (MIDI reader thread) / single-consumer (control thread)
/// ring for CcMessage, mirroring PadButtonQueue at the same small capacity
/// -- a claimed button-style CC is a rare, control-rate event, not a knob
/// sweep (a driver only ever claims a handful of specific CCs, never a
/// whole knob range).
class CcQueue {
public:
    static constexpr size_t kCapacity = 16; // power of two

    bool push(const CcMessage &m) {
        const size_t head = writeIndex.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & (kCapacity - 1);
        if (next == readIndex.load(std::memory_order_acquire)) return false;
        buffer[head] = m;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    bool pop(CcMessage &out) {
        const size_t tail = readIndex.load(std::memory_order_relaxed);
        if (tail == writeIndex.load(std::memory_order_acquire)) return false;
        out = buffer[tail];
        readIndex.store((tail + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

private:
    CcMessage            buffer[kCapacity];
    std::atomic<size_t>  writeIndex{0};
    std::atomic<size_t>  readIndex{0};
};

} // namespace s1
