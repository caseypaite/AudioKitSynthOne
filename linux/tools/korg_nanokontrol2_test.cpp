//
//  korg_nanokontrol2_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Korg nanoKONTROL2 driver (host/ctrldev/KorgNanoKontrol2.cpp)
//  against an unstarted Engine -- no MIDI hardware, no audio backend needed.
//  This device has no SysEx at all: init() seeds Engine's device-default CC
//  table directly from a fixed table, so this test drives init() then checks
//  deviceDefaultForCc() for both the fader and knob rows, and confirms no
//  pad note is ever claimed (every button on this device is CC-based, with
//  no onCC() hook to act on -- see the driver's file header). Prints
//  PASS/FAIL per case and exits non-zero on any failure, mirroring
//  tools/akai_midimix_test.cpp.
//

#include "Engine.h"
#include "PadFilter.h"
#include "ctrldev/KorgNanoKontrol2.h"

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
    auto driver = s1::ctrldev::makeKorgNanoKontrol2();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter);

    // -- Fixed CC defaults, no SysEx round trip needed -------------------
    check(engine.deviceDefaultForCc(0) == cutoff, "fader 1 (CC0) seeds cutoff");
    check(engine.deviceDefaultForCc(7) == masterVolume, "fader 8 (CC7) seeds masterVolume");
    check(engine.deviceDefaultForCc(16) == morph1Volume, "knob 1 (CC16) seeds morph1Volume");
    check(engine.deviceDefaultForCc(23) == morphBalance, "knob 8 (CC23) seeds morphBalance");
    check(engine.deviceDefaultForCc(1) == S1Parameter::S1ParameterCount,
         "fader 2 (CC1, also the universal mod wheel CC) is deliberately left unbound");
    check(engine.deviceDefaultForCc(100) == S1Parameter::S1ParameterCount,
         "an unrelated CC (100) is untouched");

    // -- No note-based function pads on this device (no keybed, no pads) --
    check(!padFilter.isPadNote(0, 41), "no note is claimed -- every control here is CC-based");
    check(!padFilter.isPadNote(0, 60), "no note is claimed -- every control here is CC-based");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
