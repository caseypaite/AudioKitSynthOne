//
//  teenage_engineering_op1_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Teenage Engineering OP-1 driver
//  (host/ctrldev/TeenageEngineeringOp1.cpp) against an unstarted Engine --
//  no MIDI hardware needed. Confirms init() is a safe no-op and nothing
//  gets claimed or bound (see the driver's file header for why -- its
//  encoders are confirmed relative, and every other control is CC-based
//  with no onCC() hook).
//

#include "Engine.h"
#include "PadFilter.h"
#include "ctrldev/TeenageEngineeringOp1.h"

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
    auto driver = s1::ctrldev::makeTeenageEngineeringOp1();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter);
    check(true, "init() with no MidiOutput does not crash");

    // CC 1-4 are this device's confirmed (relative) encoder range --
    // deliberately unbound.
    for (int cc = 1; cc <= 4; ++cc) {
        check(engine.deviceDefaultForCc(cc) == S1Parameter::S1ParameterCount,
             "encoder CC is deliberately left unbound (relative-encoder output)");
    }
    check(!padFilter.isPadNote(0, 60), "no note is claimed -- every control here is CC-based");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
