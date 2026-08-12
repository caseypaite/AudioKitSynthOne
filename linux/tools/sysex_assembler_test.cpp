//
//  sysex_assembler_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Standalone exercise of SysExAssembler/SysExQueue (host/MidiSysEx.h)
//  against synthetic byte sequences, since neither ALSA nor WinMM hardware
//  is required to validate the reassembly logic itself -- only the platform
//  glue that feeds it real bytes needs real hardware. Prints PASS/FAIL per
//  case and exits non-zero on any failure, so it can be run as a smoke test.
//

#include "MidiSysEx.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int gFailures = 0;

void check(bool condition, const char *what) {
    if (condition) {
        std::printf("  PASS  %s\n", what);
    } else {
        std::printf("  FAIL  %s\n", what);
        ++gFailures;
    }
}

/// Feeds `bytes` through `assembler` split into chunks of `chunkSize`,
/// simulating a message fragmented across several ALSA sequencer events or
/// several WinMM MIM_LONGDATA buffer refills. Returns the last append()
/// result and fills `out` if a message completed.
bool feedChunked(s1::SysExAssembler &assembler, const std::vector<uint8_t> &bytes,
                 size_t chunkSize, s1::SysExMessage &out) {
    bool complete = false;
    for (size_t i = 0; i < bytes.size(); i += chunkSize) {
        const size_t n = std::min(chunkSize, bytes.size() - i);
        if (assembler.append(bytes.data() + i, n, out)) complete = true;
    }
    return complete;
}

} // namespace

int main() {
    // -- a message delivered whole -------------------------------------
    {
        s1::SysExAssembler assembler;
        const std::vector<uint8_t> msg = {0xF0, 0x47, 0x00, 0x49, 0x66, 0xF7};
        s1::SysExMessage out;
        const bool complete = feedChunked(assembler, msg, msg.size(), out);
        check(complete, "whole message completes");
        check(out.length == msg.size(), "whole message length matches");
        check(std::memcmp(out.data, msg.data(), msg.size()) == 0, "whole message bytes match");
    }

    // -- fragmented one byte at a time, as WinMM might refill a buffer --
    {
        s1::SysExAssembler assembler;
        const std::vector<uint8_t> msg = {0xF0, 0x47, 0x7f, 0x49, 0x66, 0x00,
                                          0x01, 0x00, 0xF7};
        s1::SysExMessage out;
        const bool complete = feedChunked(assembler, msg, 1, out);
        check(complete, "byte-at-a-time message completes");
        check(out.length == msg.size(), "byte-at-a-time length matches");
        check(std::memcmp(out.data, msg.data(), msg.size()) == 0, "byte-at-a-time bytes match");
    }

    // -- fragmented across a chunk boundary that splits mid-message -----
    {
        s1::SysExAssembler assembler;
        const std::vector<uint8_t> msg(20, 0x10); // won't parse as anything meaningful, that's fine
        std::vector<uint8_t> wrapped = {0xF0};
        wrapped.insert(wrapped.end(), msg.begin(), msg.end());
        wrapped.push_back(0xF7);
        s1::SysExMessage out;
        const bool complete = feedChunked(assembler, wrapped, 7, out);
        check(complete, "multi-chunk message completes");
        check(out.length == wrapped.size(), "multi-chunk length matches");
        check(std::memcmp(out.data, wrapped.data(), wrapped.size()) == 0,
             "multi-chunk bytes match");
    }

    // -- a dropped terminator followed by a fresh message resyncs on 0xF0 --
    {
        s1::SysExAssembler assembler;
        s1::SysExMessage out;
        const std::vector<uint8_t> truncated = {0xF0, 0x01, 0x02, 0x03}; // no F7
        bool sawComplete = assembler.append(truncated.data(), truncated.size(), out);
        check(!sawComplete, "truncated message does not complete");

        const std::vector<uint8_t> fresh = {0xF0, 0x7f, 0xF7};
        sawComplete = assembler.append(fresh.data(), fresh.size(), out);
        check(sawComplete, "message after a dropped terminator completes");
        check(out.length == fresh.size(), "resynced message length matches");
        check(std::memcmp(out.data, fresh.data(), fresh.size()) == 0,
             "resynced message bytes match");
    }

    // -- realtime bytes interleaved inside a live stream are skipped ----
    {
        s1::SysExAssembler assembler;
        // 0xF8 (clock) appears mid-stream and must not become payload.
        const std::vector<uint8_t> msg = {0xF0, 0x01, 0xF8, 0x02, 0xF7};
        s1::SysExMessage out;
        const bool complete = feedChunked(assembler, msg, msg.size(), out);
        check(complete, "message with an interleaved realtime byte completes");
        const std::vector<uint8_t> expected = {0xF0, 0x01, 0x02, 0xF7};
        check(out.length == expected.size(), "realtime byte was not counted");
        check(std::memcmp(out.data, expected.data(), expected.size()) == 0,
             "realtime byte was excluded from payload");
    }

    // -- overflow: a message longer than kMaxLength is dropped, not corrupted --
    {
        s1::SysExAssembler assembler;
        std::vector<uint8_t> oversized;
        oversized.push_back(0xF0);
        oversized.insert(oversized.end(), s1::SysExMessage::kMaxLength + 16, 0x10);
        oversized.push_back(0xF7);
        s1::SysExMessage out;
        const bool complete = feedChunked(assembler, oversized, 64, out);
        check(!complete, "oversized message never completes");
        check(assembler.overflowCount() == 1, "overflow was counted");

        // The assembler must still work correctly afterward.
        const std::vector<uint8_t> fresh = {0xF0, 0x11, 0x22, 0xF7};
        const bool recovered = assembler.append(fresh.data(), fresh.size(), out);
        check(recovered, "assembler recovers after an overflow");
    }

    // -- SysExQueue: push/pop round-trip and capacity behaviour ---------
    {
        s1::SysExQueue queue;
        s1::SysExMessage in;
        in.length = 3;
        in.data[0] = 0xF0;
        in.data[1] = 0x7f;
        in.data[2] = 0xF7;
        in.sourceId = 42;
        check(queue.push(in), "queue push succeeds");

        s1::SysExMessage out;
        check(queue.pop(out), "queue pop succeeds");
        check(out.sourceId == 42, "queue round-trip preserves sourceId");
        check(out.length == 3 && out.data[1] == 0x7f, "queue round-trip preserves payload");

        s1::SysExMessage empty;
        check(!queue.pop(empty), "queue pop on empty queue fails");

        // Fill to capacity - 1 (one slot always kept empty to distinguish
        // full from empty), then confirm the next push is rejected.
        for (size_t i = 0; i < s1::SysExQueue::kCapacity - 1; ++i) {
            check(queue.push(in), "queue push while not full succeeds");
        }
        check(!queue.push(in), "queue push when full fails");
    }

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
