//
//  akai_mpk249_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Akai MPK249 driver (host/ctrldev/AkaiMpk249.cpp) against an
//  unstarted Engine -- no MIDI hardware, no audio backend needed. Like the
//  other fixed-CC Akai drivers, init() seeds Engine's device-default CC
//  table directly, so this test drives init() then checks
//  deviceDefaultForCc() for all 3 bound knob/fader rows, and confirms the
//  deliberately-unbound switch/transport CCs stay unbound. Prints PASS/FAIL
//  per case and exits non-zero on any failure, mirroring
//  tools/akai_apc40_mk2_test.cpp.
//

#include "Engine.h"
#include "PadFilter.h"
#include "ctrldev/AkaiMpk249.h"

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
    auto driver = s1::ctrldev::makeAkaiMpk249();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter);

    // -- Bank A knobs -------------------------------------------------------
    check(engine.deviceDefaultForCc(3) == cutoff, "Bank A knob 1 (CC3) seeds cutoff");
    check(engine.deviceDefaultForCc(19) == masterVolume, "Bank A knob 8 (CC19) seeds masterVolume");

    // -- Faders ---------------------------------------------------------
    check(engine.deviceDefaultForCc(18) == morph1Volume, "fader 1 (CC18) seeds morph1Volume");
    check(engine.deviceDefaultForCc(27) == morphBalance, "fader 8 (CC27) seeds morphBalance");

    // -- Bank B knobs -------------------------------------------------------
    check(engine.deviceDefaultForCc(52) == filterAttackDuration, "Bank B knob 1 (CC52) seeds filterAttackDuration");
    check(engine.deviceDefaultForCc(60) == sustainLevel, "Bank B knob 8 (CC60) seeds sustainLevel");

    // -- Deliberately unbound: mixer-strip switches and one-shot transport --
    check(engine.deviceDefaultForCc(28) == S1Parameter::S1ParameterCount,
         "a Bank A switch CC (CC28, solo 1) is deliberately left unbound");
    check(engine.deviceDefaultForCc(75) == S1Parameter::S1ParameterCount,
         "a Bank B switch CC (CC75, mute 1) is deliberately left unbound");
    check(engine.deviceDefaultForCc(106) == S1Parameter::S1ParameterCount,
         "a Bank C switch CC (CC106, record arm 1) is deliberately left unbound");
    check(engine.deviceDefaultForCc(118) == S1Parameter::S1ParameterCount,
         "TRANSPORT_PLAY_CC (CC118) is deliberately left unbound");

    // No function pads for this device -- confirm nothing got claimed.
    check(!padFilter.isPadNote(9, 60), "no pad note is claimed (device has no function-pad claims)");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
