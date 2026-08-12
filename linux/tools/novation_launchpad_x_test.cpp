//
//  novation_launchpad_x_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Novation Launchpad X driver
//  (host/ctrldev/NovationLaunchpadX.cpp) against an unstarted Engine -- no
//  MIDI hardware needed. Confirms init() is a safe no-op and nothing gets
//  claimed or bound (see the driver's file header for why).
//

#include "Engine.h"
#include "PadFilter.h"
#include "ctrldev/NovationLaunchpadX.h"

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
    s1::CcFilter ccFilter;
    auto driver = s1::ctrldev::makeNovationLaunchpadX();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter, ccFilter);
    check(true, "init() with no MidiOutput does not crash");

    const int notesToCheck[] = {11, 18, 88, 91};
    for (int note : notesToCheck) {
        check(!padFilter.isPadNote(0, note), "no note is claimed -- this driver binds nothing");
    }
    check(engine.deviceDefaultForCc(1) == S1Parameter::S1ParameterCount, "no CC is bound either");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
