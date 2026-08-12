//
//  novation_launchkey_mini_mk3_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Novation Launchkey Mini mk3 driver
//  (host/ctrldev/NovationLaunchkeyMiniMk3.cpp) against an unstarted Engine --
//  no MIDI hardware, no audio backend needed. Confirms init() doesn't crash
//  sending the session-mode Note On with no MidiOutput connected, and checks
//  the 8 knob CC defaults. Prints PASS/FAIL per case and exits non-zero on
//  any failure, mirroring tools/akai_midimix_test.cpp.
//

#include "Engine.h"
#include "PadFilter.h"
#include "ctrldev/NovationLaunchkeyMiniMk3.h"

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
    s1::Engine engine; // never started -- setDeviceDefaultCc guards on mKernel
    s1::PadFilter padFilter;
    s1::CcFilter ccFilter;
    auto driver = s1::ctrldev::makeNovationLaunchkeyMiniMk3();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter, ccFilter);
    check(true, "init() with no MidiOutput does not crash (session-mode noteOn skipped)");

    check(engine.deviceDefaultForCc(21) == cutoff, "knob 1 (CC21) seeds cutoff");
    check(engine.deviceDefaultForCc(28) == masterVolume, "knob 8 (CC28) seeds masterVolume");
    check(engine.deviceDefaultForCc(1) == S1Parameter::S1ParameterCount,
         "an unrelated CC (mod wheel, CC1) is untouched");
    check(!padFilter.isPadNote(0, 96), "a pad note (96) is left unclaimed (no function pads)");

    // -- The 2 claimed transport CCs, on the confirmed session channel 15 --
    check(ccFilter.isClaimedCc(15, 0x73), "PLAY (CC 0x73, channel 15) is claimed");
    check(ccFilter.isClaimedCc(15, 0x75), "RECORD (CC 0x75, channel 15) is claimed");
    check(!ccFilter.isClaimedCc(0, 0x73), "the claimed channel is restricted, not a wildcard");

    const float before = engine.getParameter(arpIsOn);
    driver->onCC(15, 0x73, 127);
    const float after = engine.getParameter(arpIsOn);
    check(before == after, "arpIsOn toggle is a no-op against an unstarted Engine (guarded on mKernel), not a crash");
    driver->onCC(15, 0x66, 127);
    check(true, "an unclaimed CC handed to onCC() does not crash");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
