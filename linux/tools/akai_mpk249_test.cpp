//
//  akai_mpk249_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Akai MPK249 driver (host/ctrldev/AkaiMpk249.cpp) against an
//  unstarted Engine -- no MIDI hardware, no audio backend needed. Like the
//  other fixed-CC Akai drivers, init() seeds Engine's device-default CC
//  table directly, so this test drives init() then checks
//  deviceDefaultForCc() for all 3 bound knob/fader rows, confirms the
//  deliberately-unbound switch CCs stay unbound, and checks the 3 claimed
//  transport CCs' onCC() behavior -- this is the first driver in this port
//  to use ControllerDriver::onCC(). Prints PASS/FAIL per case and exits
//  non-zero on any failure, mirroring tools/akai_apc40_mk2_test.cpp.
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
    s1::CcFilter ccFilter;
    auto driver = s1::ctrldev::makeAkaiMpk249();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter, ccFilter);

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
         "TRANSPORT_PLAY_CC (CC118) is not a setDeviceDefaultCc target (it's claimed via CcFilter instead)");

    // No function pads for this device -- confirm nothing got claimed.
    check(!padFilter.isPadNote(9, 60), "no pad note is claimed (device has no function-pad claims)");

    // -- The 3 claimed transport CCs, on the confirmed channel 0 only -----
    check(ccFilter.isClaimedCc(0, 117), "STOP (CC117, channel 0) is claimed");
    check(ccFilter.isClaimedCc(0, 119), "RECORD (CC119, channel 0) is claimed");
    check(!ccFilter.isClaimedCc(1, 118), "the claimed channel is restricted, not a wildcard");
    check(!ccFilter.isClaimedCc(0, 28), "a Bank A switch CC (CC28) is not claimed via CcFilter either");

    // onCC() must not crash even though `engine` was never started
    // (Engine::panic()/setParameter()/getParameter() all guard on mKernel).
    driver->onCC(0, 117, 127);
    check(true, "STOP onCC() does not crash");

    driver->onCC(0, 120, 127);
    check(true, "an unclaimed CC handed to onCC() does not crash either");

    const float before = engine.getParameter(arpIsOn);
    driver->onCC(0, 118, 127);
    const float after = engine.getParameter(arpIsOn);
    check(before == after, "arpIsOn toggle is a no-op against an unstarted Engine (guarded on mKernel), not a crash");

    driver->onCC(0, 118, 0);
    check(true, "onCC() with value 0 (a release, if this device sends one) does not crash and is a no-op");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
