//
//  novation_launchkey_mini_mk4_37_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Novation Launchkey Mini mk4 37 driver
//  (host/ctrldev/NovationLaunchkeyMiniMk4.cpp) against an unstarted Engine --
//  no MIDI hardware needed. Confirms init() doesn't crash with no
//  MidiOutput, and -- unlike every other Launchkey driver -- that its knob
//  CCs are deliberately left unbound (they're confirmed relative-encoder
//  output, which Engine::handleMidi's CC path can't interpret correctly;
//  see the driver's file header). Prints PASS/FAIL per case and exits
//  non-zero on any failure.
//

#include "Engine.h"
#include "PadFilter.h"
#include "ctrldev/NovationLaunchkeyMiniMk4.h"

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
    auto driver = s1::ctrldev::makeNovationLaunchkeyMiniMk4_37();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter);
    check(true, "init() with no MidiOutput does not crash (session-mode noteOn skipped)");

    // CC 85-92 are this device's confirmed knob range -- deliberately
    // unbound since they're relative, not absolute.
    for (int cc = 85; cc <= 92; ++cc) {
        check(engine.deviceDefaultForCc(cc) == S1Parameter::S1ParameterCount,
             "knob CC is deliberately left unbound (relative-encoder output)");
    }
    check(!padFilter.isPadNote(0, 96), "a pad note (96) is left unclaimed (no function pads)");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
