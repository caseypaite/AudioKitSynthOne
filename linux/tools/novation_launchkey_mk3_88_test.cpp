//
//  novation_launchkey_mk3_88_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Novation Launchkey MK3 88 driver
//  (host/ctrldev/NovationLaunchkeyMk3.cpp) against an unstarted Engine -- no
//  MIDI hardware needed. Checks the 8 knob, 8 fader and master fader CC
//  defaults. Prints PASS/FAIL per case and exits non-zero on any failure.
//

#include "Engine.h"
#include "PadFilter.h"
#include "ctrldev/NovationLaunchkeyMk3.h"

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
    auto driver = s1::ctrldev::makeNovationLaunchkeyMk3_88();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter);
    check(true, "init() with no MidiOutput does not crash (session-mode noteOn skipped)");

    check(engine.deviceDefaultForCc(21) == morph1Volume, "knob 1 (CC21) seeds morph1Volume");
    check(engine.deviceDefaultForCc(28) == morphBalance, "knob 8 (CC28) seeds morphBalance");
    check(engine.deviceDefaultForCc(53) == cutoff, "fader 1 (CC53) seeds cutoff");
    check(engine.deviceDefaultForCc(60) == arpRate, "fader 8 (CC60) seeds arpRate");
    check(engine.deviceDefaultForCc(61) == masterVolume, "master fader (CC61) seeds masterVolume");
    check(engine.deviceDefaultForCc(1) == S1Parameter::S1ParameterCount,
         "an unrelated CC (mod wheel, CC1) is untouched");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
