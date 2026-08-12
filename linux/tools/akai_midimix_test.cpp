//
//  akai_midimix_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Akai MIDI Mix driver (host/ctrldev/AkaiMidiMix.cpp) against
//  an unstarted Engine -- no MIDI hardware, no audio backend needed. Unlike
//  the MPK Mini mk3, this device has no SysEx to synthesize: init() seeds
//  Engine's device-default CC table directly from a fixed table, so this
//  test drives init() then checks deviceDefaultForCc() and the 3 claimed
//  global buttons' onPadButton() behavior. Prints PASS/FAIL per case and
//  exits non-zero on any failure, mirroring tools/pad_filter_test.cpp.
//

#include "Engine.h"
#include "PadFilter.h"
#include "ctrldev/AkaiMidiMix.h"

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
    s1::Engine engine; // never started -- setDeviceDefaultCc/Engine accessors
                       // used here all guard on mKernel
    s1::PadFilter padFilter;
    auto driver = s1::ctrldev::makeAkaiMidiMix();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter);

    // -- Fixed CC defaults, no SysEx round trip needed -------------------
    check(engine.deviceDefaultForCc(16) == morph1Volume,
         "knob row 1 strip 1 (CC16) seeds morph1Volume");
    check(engine.deviceDefaultForCc(58) == morphBalance,
         "knob row 1 strip 8 (CC58) seeds morphBalance");
    check(engine.deviceDefaultForCc(17) == filterAttackDuration,
         "knob row 2 strip 1 (CC17) seeds filterAttackDuration");
    check(engine.deviceDefaultForCc(18) == lfo1Amplitude,
         "knob row 3 strip 1 (CC18) seeds lfo1Amplitude");
    check(engine.deviceDefaultForCc(19) == cutoff,
         "fader strip 1 (CC19) seeds cutoff");
    check(engine.deviceDefaultForCc(61) == arpRate,
         "fader strip 8 (CC61) seeds arpRate");
    check(engine.deviceDefaultForCc(62) == masterVolume,
         "master fader (CC62) seeds masterVolume");
    check(engine.deviceDefaultForCc(1) == S1Parameter::S1ParameterCount,
         "an unrelated CC (mod wheel, CC1) is untouched");

    // -- The 3 claimed global buttons --------------------------------------
    check(padFilter.isPadNote(0, 25), "BANK LEFT (note 25) is claimed");
    check(padFilter.isPadNote(0, 26), "BANK RIGHT (note 26) is claimed");
    check(padFilter.isPadNote(0, 27), "SOLO (note 27) is claimed");
    check(!padFilter.isPadNote(0, 1), "a per-strip MUTE button (note 1) is left unclaimed");
    check(!padFilter.isPadNote(0, 3), "a per-strip REC button (note 3) is left unclaimed");

    // onPadButton() must not crash even though `engine` was never started
    // (Engine::panic()/setParameter()/getParameter() all guard on mKernel).
    s1::ctrldev::PadReport report = driver->onPadButton(0, 27, /*isDown=*/true);
    check(!report.reported, "SOLO press is handled internally, not reported outward");

    const float before = engine.getParameter(arpIsOn);
    driver->onPadButton(0, 25, /*isDown=*/true);
    const float after = engine.getParameter(arpIsOn);
    check(before == after, "arpIsOn toggle is a no-op against an unstarted Engine (guarded on mKernel), not a crash");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
