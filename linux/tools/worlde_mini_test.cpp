//
//  worlde_mini_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the WORLDE MINI driver (host/ctrldev/WorldeMini.cpp) against an
//  unstarted Engine -- no MIDI hardware needed. This device has no SysEx:
//  init() claims 4 pad notes directly, no query/reply round trip. Checks
//  the claimed notes' channel restriction and onPadButton() behavior.
//  Prints PASS/FAIL per case and exits non-zero on any failure, mirroring
//  tools/akai_apc_key25_test.cpp.
//

#include "Engine.h"
#include "PadFilter.h"
#include "ctrldev/WorldeMini.h"

#include <cstdio>

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

} // namespace

int main() {
    s1::Engine engine; // never started -- Engine accessors used here all
                       // guard on mKernel
    s1::PadFilter padFilter;
    s1::CcFilter ccFilter;
    auto driver = s1::ctrldev::makeWorldeMini();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter, ccFilter);
    check(true, "init() with no MidiOutput does not crash");

    // -- The 4 claimed pads, on the confirmed channel 9 only ---------------
    check(padFilter.isPadNote(9, 36), "PANIC pad (note 36, channel 9) is claimed");
    check(padFilter.isPadNote(9, 39), "ARP/SEQ pad (note 39, channel 9) is claimed");
    check(!padFilter.isPadNote(0, 36), "the claimed channel is restricted, not a wildcard");
    check(!padFilter.isPadNote(9, 40), "pad 40 (outside the claimed 4) is left unclaimed");

    // onPadButton() must not crash even though `engine` was never started
    // (Engine::panic()/allNotesOff()/setParameter()/getParameter() all
    // guard on mKernel).
    s1::ctrldev::PadReport report = driver->onPadButton(9, 36, /*isDown=*/true);
    check(!report.reported, "PANIC press is handled internally, not reported outward");

    report = driver->onPadButton(9, 40, /*isDown=*/true);
    check(!report.reported, "an unclaimed note handed to onPadButton degrades to unreported, not a crash");

    const float before = engine.getParameter(arpIsOn);
    driver->onPadButton(9, 38, /*isDown=*/true);
    const float after = engine.getParameter(arpIsOn);
    check(before == after, "arpIsOn toggle is a no-op against an unstarted Engine (guarded on mKernel), not a crash");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
