//
//  akai_apc40_mk2_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Akai APC40 mk2 driver (host/ctrldev/AkaiApc40Mk2.cpp)
//  against an unstarted Engine -- no MIDI hardware, no audio backend needed.
//  Like the other Akai drivers with no SysEx, init() seeds Engine's
//  device-default CC table directly from a fixed table, so this test drives
//  init() then checks deviceDefaultForCc() -- including that the two
//  deliberately-relative/ambiguous controls (Tempo Knob, Cue Level) are
//  left unbound -- and the 3 claimed transport buttons' onPadButton()
//  behavior. Prints PASS/FAIL per case and exits non-zero on any failure,
//  mirroring tools/akai_apc_key25_test.cpp.
//

#include "Engine.h"
#include "PadFilter.h"
#include "ctrldev/AkaiApc40Mk2.h"

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
    auto driver = s1::ctrldev::makeAkaiApc40Mk2();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter);

    // -- Fixed CC defaults, no SysEx round trip needed -------------------
    check(engine.deviceDefaultForCc(0x10) == filterAttackDuration,
         "device knob 1 (CC 0x10) seeds filterAttackDuration");
    check(engine.deviceDefaultForCc(0x17) == sustainLevel,
         "device knob 8 (CC 0x17) seeds sustainLevel");
    check(engine.deviceDefaultForCc(0x30) == morph1Volume,
         "track knob 1 (CC 0x30) seeds morph1Volume");
    check(engine.deviceDefaultForCc(0x37) == morphBalance,
         "track knob 8 (CC 0x37) seeds morphBalance");
    check(engine.deviceDefaultForCc(0x0E) == masterVolume, "master fader (CC 0x0E) seeds masterVolume");
    check(engine.deviceDefaultForCc(0x0F) == widen, "crossfader (CC 0x0F) seeds widen");
    check(engine.deviceDefaultForCc(0x07) == reverbMix, "track fader (CC 0x07) seeds reverbMix");

    // Deliberately unbound: relative/ambiguous per the official protocol doc.
    check(engine.deviceDefaultForCc(0x0D) == S1Parameter::S1ParameterCount,
         "tempo knob (CC 0x0D, relative) is deliberately left unbound");
    check(engine.deviceDefaultForCc(0x2F) == S1Parameter::S1ParameterCount,
         "cue level (CC 0x2F, ambiguous absolute/relative) is deliberately left unbound");

    // -- The 3 claimed transport buttons ----------------------------------
    check(padFilter.isPadNote(0, 0x51), "STOP ALL CLIPS (note 0x51) is claimed");
    check(padFilter.isPadNote(0, 0x5B), "PLAY (note 0x5B) is claimed");
    check(padFilter.isPadNote(0, 0x5D), "RECORD (note 0x5D) is claimed");
    check(!padFilter.isPadNote(0, 0), "a clip-launch grid pad (note 0) is left unclaimed");
    check(!padFilter.isPadNote(0, 0x30), "a per-track button (note 0x30, RECORD ARM) is left unclaimed");

    // onPadButton() must not crash even though `engine` was never started
    // (Engine::panic()/setParameter()/getParameter() all guard on mKernel).
    s1::ctrldev::PadReport report = driver->onPadButton(0, 0x51, /*isDown=*/true);
    check(!report.reported, "STOP ALL CLIPS press is handled internally, not reported outward");

    report = driver->onPadButton(0, 0, /*isDown=*/true);
    check(!report.reported, "an unclaimed note handed to onPadButton degrades to unreported, not a crash");

    const float before = engine.getParameter(arpIsOn);
    driver->onPadButton(0, 0x5B, /*isDown=*/true);
    const float after = engine.getParameter(arpIsOn);
    check(before == after, "arpIsOn toggle is a no-op against an unstarted Engine (guarded on mKernel), not a crash");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
