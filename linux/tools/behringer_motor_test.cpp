//
//  behringer_motor_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Behringer MOTÖR61/49 driver
//  (host/ctrldev/BehringerMotor.cpp) against an unstarted Engine -- no MIDI
//  hardware needed. Checks the fader/encoder CC defaults across all 4
//  banks, and the 4 claimed pads' channel restriction and onPadButton()
//  behavior. Prints PASS/FAIL per case and exits non-zero on any failure,
//  mirroring tools/akai_apc40_mk2_test.cpp.
//

#include "Engine.h"
#include "PadFilter.h"
#include "ctrldev/BehringerMotor.h"

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
    auto driver = s1::ctrldev::makeBehringerMotor();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter, ccFilter);
    check(true, "init() with no MidiOutput does not crash");

    // -- Fixed CC defaults, no SysEx round trip needed -------------------
    check(engine.deviceDefaultForCc(21) == cutoff, "upper fader 1 (CC21) seeds cutoff");
    check(engine.deviceDefaultForCc(28) == masterVolume, "upper fader 8 (CC28) seeds masterVolume");
    check(engine.deviceDefaultForCc(29) == morph1Volume, "lower fader 1 (CC29) seeds morph1Volume");
    check(engine.deviceDefaultForCc(36) == morphBalance, "lower fader 8 (CC36) seeds morphBalance");
    check(engine.deviceDefaultForCc(37) == filterAttackDuration, "pedal fader 1 (CC37) seeds filterAttackDuration");
    check(engine.deviceDefaultForCc(44) == sustainLevel, "pedal fader 8 (CC44) seeds sustainLevel");
    check(engine.deviceDefaultForCc(53) == masterVolume, "master fader (CC53) seeds masterVolume");
    check(engine.deviceDefaultForCc(71) == lfo1Amplitude, "upper encoder 1 (CC71) seeds lfo1Amplitude");
    check(engine.deviceDefaultForCc(75) == phaserRate, "pianoteq encoder 1 (CC75) seeds phaserRate");
    check(engine.deviceDefaultForCc(79) == arpRate, "lower encoder 1 (CC79) seeds arpRate");
    check(engine.deviceDefaultForCc(87) == widen, "pedal encoder 1 (CC87) seeds widen");
    check(engine.deviceDefaultForCc(100) == S1Parameter::S1ParameterCount,
         "an unrelated CC (100) is untouched");

    // -- The 4 claimed pads, on the confirmed channel 1 only ----------------
    check(padFilter.isPadNote(1, 66), "PANIC pad (note 66, channel 1) is claimed");
    check(padFilter.isPadNote(1, 69), "ARP/SEQ pad (note 69, channel 1) is claimed");
    check(!padFilter.isPadNote(0, 66), "the claimed channel is restricted, not a wildcard");
    check(!padFilter.isPadNote(1, 70), "pad 70 (outside the claimed 4) is left unclaimed");
    check(!padFilter.isPadNote(1, 10), "a fader-touch note (10) is left unclaimed");

    // onPadButton() must not crash even though `engine` was never started.
    s1::ctrldev::PadReport report = driver->onPadButton(1, 66, /*isDown=*/true);
    check(!report.reported, "PANIC press is handled internally, not reported outward");

    report = driver->onPadButton(1, 70, /*isDown=*/true);
    check(!report.reported, "an unclaimed note handed to onPadButton degrades to unreported, not a crash");

    const float before = engine.getParameter(arpIsOn);
    driver->onPadButton(1, 68, /*isDown=*/true);
    const float after = engine.getParameter(arpIsOn);
    check(before == after, "arpIsOn toggle is a no-op against an unstarted Engine (guarded on mKernel), not a crash");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
