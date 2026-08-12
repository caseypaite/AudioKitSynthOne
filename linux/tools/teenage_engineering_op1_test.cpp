//
//  teenage_engineering_op1_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Teenage Engineering OP-1 driver
//  (host/ctrldev/TeenageEngineeringOp1.cpp) against an unstarted Engine --
//  no MIDI hardware needed. Confirms the 4 encoder CCs stay unbound (they're
//  confirmed relative), and checks the 2 claimed transport CCs' onCC()
//  behavior.
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
    s1::CcFilter ccFilter;
    auto driver = s1::ctrldev::makeTeenageEngineeringOp1();

    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter, ccFilter);
    check(true, "init() with no MidiOutput does not crash");

    // CC 1-4 are this device's confirmed (relative) encoder range --
    // deliberately unbound.
    for (int cc = 1; cc <= 4; ++cc) {
        check(engine.deviceDefaultForCc(cc) == S1Parameter::S1ParameterCount,
             "encoder CC is deliberately left unbound (relative-encoder output)");
    }
    check(!padFilter.isPadNote(0, 60), "no note is claimed -- every control here is CC-based");

    // -- The 2 claimed transport CCs (channel unconfirmed -- wildcard) ----
    check(ccFilter.isClaimedCc(0, 39), "PLAY (CC39) is claimed on any channel");
    check(ccFilter.isClaimedCc(3, 38), "RECORD (CC38) is claimed on any channel");
    check(!ccFilter.isClaimedCc(0, 6), "METRONOME (CC6) is not claimed via CcFilter");

    const float before = engine.getParameter(arpIsOn);
    driver->onCC(0, 39, 127);
    const float after = engine.getParameter(arpIsOn);
    check(before == after, "arpIsOn toggle is a no-op against an unstarted Engine (guarded on mKernel), not a crash");
    driver->onCC(0, 38, 0);
    check(true, "onCC() with value 0 (a release) does not crash and is a no-op");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
