//
//  novation_launchpad_pro_mk2_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Novation Launchpad Pro mk2 driver
//  (host/ctrldev/NovationLaunchpadProMk2.cpp) against an unstarted Engine --
//  no MIDI hardware, no audio backend needed. Confirms init() doesn't crash
//  sending the DAW-mode SysEx handshake with no MidiOutput connected, then
//  checks the 3 claimed transport buttons' onPadButton() behavior. Prints
//  PASS/FAIL per case and exits non-zero on any failure, mirroring
//  tools/akai_apc_key25_test.cpp.
//

#include "Engine.h"
#include "PadFilter.h"
#include "ctrldev/NovationLaunchpadProMk2.h"

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
    auto driver = s1::ctrldev::makeNovationLaunchpadProMk2();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter);
    check(true, "init() with no MidiOutput does not crash (DAW-mode handshake skipped)");

    // -- The 3 claimed transport buttons ----------------------------------
    check(padFilter.isPadNote(0, 66), "STOP (note 66) is claimed");
    check(padFilter.isPadNote(0, 67), "PLAY (note 67) is claimed");
    check(padFilter.isPadNote(0, 65), "RECORD (note 65) is claimed");
    check(!padFilter.isPadNote(0, 11), "a clip-launch grid pad (note 11) is left unclaimed");

    // onPadButton() must not crash even though `engine` was never started
    // (Engine::panic()/setParameter()/getParameter() all guard on mKernel).
    s1::ctrldev::PadReport report = driver->onPadButton(0, 66, /*isDown=*/true);
    check(!report.reported, "STOP press is handled internally, not reported outward");

    report = driver->onPadButton(0, 11, /*isDown=*/true);
    check(!report.reported, "an unclaimed note handed to onPadButton degrades to unreported, not a crash");

    const float before = engine.getParameter(arpIsOn);
    driver->onPadButton(0, 67, /*isDown=*/true);
    const float after = engine.getParameter(arpIsOn);
    check(before == after, "arpIsOn toggle is a no-op against an unstarted Engine (guarded on mKernel), not a crash");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
