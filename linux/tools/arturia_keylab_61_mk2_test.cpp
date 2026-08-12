//
//  arturia_keylab_61_mk2_test.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Exercises the Arturia KeyLab mkII 61 driver
//  (host/ctrldev/ArturiaKeyLab61Mk2.cpp) against an unstarted Engine -- no
//  MIDI hardware, no audio backend needed. Unlike the Akai drivers, init()
//  sends a DAW-mode-entry SysEx handshake when a MidiOutput is connected;
//  this test drives both the midiOut-absent and midiOut-connected-but-
//  disconnected paths (both must degrade to "no SysEx sent", not a crash --
//  MidiOutput::sendSysEx() has no hardware to reach in either case here), then
//  checks the 4 claimed global/transport buttons' onPadButton() behavior.
//  Prints PASS/FAIL per case and exits non-zero on any failure, mirroring
//  tools/akai_apc_key25_test.cpp.
//

#include "Engine.h"
#include "PadFilter.h"
#include "MidiOutput.h"
#include "ctrldev/ArturiaKeyLab61Mk2.h"

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
    auto driver = s1::ctrldev::makeArturiaKeyLab61Mk2();

    // nullptr MidiOutput: init() must not crash trying to send the
    // DAW-mode-entry handshake.
    driver->init(engine, /*midiOut=*/nullptr, /*allowConfigure=*/false, padFilter, ccFilter);
    check(true, "init() with no MidiOutput does not crash (DAW-mode handshake skipped)");

    // A MidiOutput that was never opened/connected: isConnected() is false
    // by construction, so init() must skip sendSysEx() the same way -- no
    // ALSA/JACK backend needed for this check.
    s1::MidiOutput unconnectedOut;
    s1::PadFilter padFilter2;
    s1::CcFilter ccFilter2;
    auto driver2 = s1::ctrldev::makeArturiaKeyLab61Mk2();
    driver2->init(engine, &unconnectedOut, /*allowConfigure=*/false, padFilter2, ccFilter2);
    check(!unconnectedOut.isConnected(),
         "init() with an unconnected MidiOutput does not attempt to connect it");

    // -- The 4 claimed global/transport buttons -----------------------------
    check(padFilter.isPadNote(0, 93), "STOP (note 93) is claimed");
    check(padFilter.isPadNote(0, 94), "PLAY/PAUSE (note 94) is claimed");
    check(padFilter.isPadNote(0, 95), "RECORD (note 95) is claimed");
    check(padFilter.isPadNote(0, 89), "METRONOME (note 89) is claimed");
    check(!padFilter.isPadNote(0, 36), "a clip pad (note 36) is left unclaimed");
    check(!padFilter.isPadNote(0, 24), "a SELECT button (note 24, SELECT_1) is left unclaimed");

    // onPadButton() must not crash even though `engine` was never started
    // (Engine::panic()/allNotesOff()/setParameter()/getParameter() all guard
    // on mKernel).
    s1::ctrldev::PadReport report = driver->onPadButton(0, 93, /*isDown=*/true);
    check(!report.reported, "STOP press is handled internally, not reported outward");

    report = driver->onPadButton(0, 89, /*isDown=*/true);
    check(!report.reported, "METRONOME press is handled internally, not reported outward");

    report = driver->onPadButton(0, 36, /*isDown=*/true);
    check(!report.reported, "an unclaimed note handed to onPadButton degrades to unreported, not a crash");

    const float before = engine.getParameter(arpIsOn);
    driver->onPadButton(0, 94, /*isDown=*/true);
    const float after = engine.getParameter(arpIsOn);
    check(before == after, "arpIsOn toggle is a no-op against an unstarted Engine (guarded on mKernel), not a crash");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL PASS" : "FAILED",
               gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
