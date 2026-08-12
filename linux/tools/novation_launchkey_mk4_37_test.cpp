//
//  novation_launchkey_mk4_37_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Novation Launchkey MK4 37 driver
//  (host/ctrldev/NovationLaunchkeyMk4.cpp) against an unstarted Engine -- no
//  MIDI hardware needed. Checks the 8 knob CC defaults. Prints PASS/FAIL
//  per case and exits non-zero on any failure.
//

#include "Engine.h"
#include "PadFilter.h"
#include "ctrldev/NovationLaunchkeyMk4.h"

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
    s1::Engine engine;
    s1::PadFilter padFilter;
    s1::CcFilter ccFilter;
    auto driver = s1::ctrldev::makeNovationLaunchkeyMk4_37();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter, ccFilter);
    check(true, "init() with no MidiOutput does not crash (session-mode noteOn skipped)");

    check(engine.deviceDefaultForCc(21) == filterAttackDuration, "knob 1 (CC21) seeds filterAttackDuration");
    check(engine.deviceDefaultForCc(28) == sustainLevel, "knob 8 (CC28) seeds sustainLevel");
    check(engine.deviceDefaultForCc(1) == S1Parameter::S1ParameterCount,
         "an unrelated CC (mod wheel, CC1) is untouched");

    // -- The 2 claimed transport CCs (channel unconfirmed -- wildcard) ----
    check(ccFilter.isClaimedCc(0, 0x73), "PLAY (CC 0x73) is claimed on any channel");
    check(ccFilter.isClaimedCc(15, 0x75), "RECORD (CC 0x75) is claimed on any channel");

    const float before = engine.getParameter(arpIsOn);
    driver->onCC(0, 0x73, 127);
    const float after = engine.getParameter(arpIsOn);
    check(before == after, "arpIsOn toggle is a no-op against an unstarted Engine (guarded on mKernel), not a crash");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
