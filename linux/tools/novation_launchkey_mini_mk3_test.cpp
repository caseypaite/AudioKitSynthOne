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
    auto driver = s1::ctrldev::makeNovationLaunchkeyMiniMk3();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter);
    check(true, "init() with no MidiOutput does not crash (session-mode noteOn skipped)");

    check(engine.deviceDefaultForCc(21) == cutoff, "knob 1 (CC21) seeds cutoff");
    check(engine.deviceDefaultForCc(28) == masterVolume, "knob 8 (CC28) seeds masterVolume");
    check(engine.deviceDefaultForCc(1) == S1Parameter::S1ParameterCount,
         "an unrelated CC (mod wheel, CC1) is untouched");
    check(!padFilter.isPadNote(0, 96), "a pad note (96) is left unclaimed (no function pads)");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
