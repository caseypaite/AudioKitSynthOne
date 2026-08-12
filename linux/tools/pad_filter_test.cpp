//
//  pad_filter_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises PadFilter/PadButtonQueue (host/PadFilter.h) against synthetic
//  data, and the Akai MPK Mini mk3 driver's pad-table parsing against a
//  hand-built synthetic SysEx reply -- no MIDI hardware, no audio backend,
//  no started Engine needed. Prints PASS/FAIL per case and exits non-zero
//  on any failure, mirroring tools/sysex_assembler_test.cpp.
//

#include "PadFilter.h"
#include "Engine.h"
#include "ctrldev/AkaiMpkMiniMk3.h"

#include <cstdio>
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

/// Builds a synthetic, envelope-correct 254-byte Akai MPK Mini mk3
/// CMD_INCOMING_DATA reply (see docs/midi-controller-developer-guide.md for
/// the confirmed byte layout) with the first 8 pad-table notes set to
/// `padNotes[0..7]`, everything else zeroed.
std::vector<uint8_t> buildSyntheticReply(const uint8_t padNotes[8]) {
    std::vector<uint8_t> msg(254, 0);
    msg[0] = 0xF0;
    msg[1] = 0x47; // manufacturer (Akai)
    msg[2] = 0x00; // direction: device -> host
    msg[3] = 0x49; // product (MPK Mini mk3)
    msg[4] = 0x67; // command: incoming data (reply)
    msg[5] = 0x01; // length (unchecked beyond overall message length)
    msg[6] = 0x76;
    msg[7] = 0x00; // program slot

    // Body starts at msg[1]; pad table at body offset 43 -> msg[44],
    // 3 bytes per pad (note, program-change, CC).
    constexpr int kPadTableMsgOffset = 1 + 43;
    for (int i = 0; i < 8; ++i) {
        msg[static_cast<size_t>(kPadTableMsgOffset + i * 3)] = padNotes[i];
    }

    msg[253] = 0xF7;
    return msg;
}

} // namespace

int main() {
    // -- PadFilter: claim/unclaim/isPadNote, channel wildcard -----------
    {
        s1::PadFilter filter;
        check(!filter.isPadNote(0, 60), "fresh filter claims nothing");

        filter.claimNote(60);
        check(filter.isPadNote(0, 60), "claimed note matches on channel 0");
        check(filter.isPadNote(9, 60), "claimed note matches on any channel by default");
        check(!filter.isPadNote(0, 61), "unclaimed note on the same channel doesn't match");

        filter.claimChannel(9);
        check(filter.isPadNote(9, 60), "claimed note matches its restricted channel");
        check(!filter.isPadNote(0, 60), "claimed note no longer matches other channels once restricted");

        filter.unclaimNote(60);
        check(!filter.isPadNote(9, 60), "unclaimed note no longer matches");

        filter.claimNote(10);
        filter.claimNote(127);
        filter.claimChannel(-1);
        check(filter.isPadNote(3, 10), "second claimed note matches (low word)");
        check(filter.isPadNote(3, 127), "claimed note 127 matches (high word boundary)");
        check(!filter.isPadNote(3, 128), "out-of-range note never matches"); // guarded internally

        filter.clear();
        check(!filter.isPadNote(3, 10), "clear() unclaims everything");
        check(!filter.isPadNote(3, 127), "clear() unclaims the high-word note too");
    }

    // -- PadButtonQueue: push/pop round-trip and capacity ---------------
    {
        s1::PadButtonQueue queue;
        s1::PadButtonMessage in;
        in.channel = 9;
        in.note = 40;
        in.isNoteOn = true;
        in.sourceId = 7;
        check(queue.push(in), "pad queue push succeeds");

        s1::PadButtonMessage out;
        check(queue.pop(out), "pad queue pop succeeds");
        check(out.channel == 9 && out.note == 40 && out.isNoteOn && out.sourceId == 7,
             "pad queue round-trip preserves all fields");

        s1::PadButtonMessage empty;
        check(!queue.pop(empty), "pad queue pop on empty queue fails");

        for (size_t i = 0; i < s1::PadButtonQueue::kCapacity - 1; ++i) {
            check(queue.push(in), "pad queue push while not full succeeds");
        }
        check(!queue.push(in), "pad queue push when full fails");
    }

    // -- AkaiMpkMiniMk3: pad-table parsing end to end --------------------
    {
        s1::Engine engine; // never started -- setDeviceDefaultCc/Engine
                           // accessors used here all guard on mKernel
        s1::PadFilter padFilter;
        s1::CcFilter ccFilter;
        auto driver = s1::ctrldev::makeAkaiMpkMiniMk3();

        // nullptr MidiOutput: init() won't call sendQuery() itself, so this
        // drives discovery entirely through a hand-built onSysEx() reply.
        driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter, ccFilter);

        const uint8_t padNotes[8] = {40, 41, 42, 43, 44, 45, 46, 47};
        const std::vector<uint8_t> reply = buildSyntheticReply(padNotes);
        driver->onSysEx(reply.data(), reply.size());

        bool allClaimed = true;
        for (uint8_t note : padNotes) {
            if (!padFilter.isPadNote(0, note)) allClaimed = false;
        }
        check(allClaimed, "all 8 synthetic pad notes are claimed after a valid reply");
        check(!padFilter.isPadNote(0, 48), "a note outside the synthetic table stays unclaimed");

        // Bottom row (table indices 0-3, notes 40-43): reported outward,
        // not acted on directly.
        s1::ctrldev::PadReport report = driver->onPadButton(0, 41, /*isDown=*/true);
        check(report.reported && report.row == s1::ctrldev::PadRow::Bottom && report.index == 1,
             "bottom-row pad (note 41, table index 1) resolves to Bottom/1");

        // Top row (table indices 4-7, notes 44-47): handled internally,
        // never reported outward. Calling this must not crash even though
        // `engine` was never started -- see the mKernel guards checked
        // above.
        report = driver->onPadButton(0, 46, /*isDown=*/true);
        check(!report.reported, "top-row pad (note 46, table index 6) is handled internally, not reported");

        report = driver->onPadButton(0, 99, /*isDown=*/true);
        check(!report.reported, "an unclaimed note handed to onPadButton degrades to unreported, not a crash");

        // A mode switch to an undesigned program clears every claim rather
        // than keeping a stale one. midiOut is still nullptr, so no query
        // is actually sent -- onProgramChange() itself must still clear.
        driver->onProgramChange(8);
        check(!padFilter.isPadNote(0, 40), "onProgramChange() clears prior pad claims immediately");
    }

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
