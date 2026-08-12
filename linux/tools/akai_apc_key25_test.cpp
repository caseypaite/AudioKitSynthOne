//
//  akai_apc_key25_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Akai APC Key 25 (gen 1) driver (host/ctrldev/AkaiApcKey25.cpp)
//  against an unstarted Engine -- no MIDI hardware, no audio backend needed.
//  Like the MIDI Mix, this device has no SysEx to synthesize: init() seeds
//  Engine's device-default CC table directly from a fixed table, so this
//  test drives init() then checks deviceDefaultForCc() and the 3 claimed
//  transport buttons' onPadButton() behavior. Prints PASS/FAIL per case and
//  exits non-zero on any failure, mirroring tools/akai_midimix_test.cpp.
//

#include "Engine.h"
#include "PadFilter.h"
#include "ctrldev/AkaiApcKey25.h"

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
    auto driver = s1::ctrldev::makeAkaiApcKey25();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter);

    // -- Fixed CC defaults, no SysEx round trip needed -------------------
    check(engine.deviceDefaultForCc(0x30) == cutoff, "knob 1 (CC 0x30) seeds cutoff");
    check(engine.deviceDefaultForCc(0x31) == resonance, "knob 2 (CC 0x31) seeds resonance");
    check(engine.deviceDefaultForCc(0x37) == masterVolume, "knob 8 (CC 0x37) seeds masterVolume");
    check(engine.deviceDefaultForCc(1) == S1Parameter::S1ParameterCount,
         "an unrelated CC (mod wheel, CC1) is untouched");

    // -- The 3 claimed transport buttons ----------------------------------
    check(padFilter.isPadNote(0, 0x51), "STOP ALL CLIPS (note 0x51) is claimed");
    check(padFilter.isPadNote(0, 0x5B), "PLAY (note 0x5B) is claimed");
    check(padFilter.isPadNote(0, 0x5D), "RECORD (note 0x5D) is claimed");
    check(!padFilter.isPadNote(0, 0), "a clip-launch grid pad (note 0) is left unclaimed");
    check(!padFilter.isPadNote(0, 0x52), "a soft key (note 0x52, CLIP STOP) is left unclaimed");
    check(!padFilter.isPadNote(0, 0x40), "a track-select button (note 0x40) is left unclaimed");

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
