//
//  NovationLaunchkeyMiniMk3.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Novation Launchkey Mini mk3. Like the Akai MIDI Mix/APC
//  drivers, this device has no SysEx worth speaking -- but unlike them, its
//  8 knobs only send their DAW-mode CCs after a "session mode" handshake,
//  confirmed as a plain Note On (channel 15, note 12, velocity 127) rather
//  than SysEx -- `MidiOutput::noteOn()` covers this, no sendSysEx() needed.
//  Sent unconditionally at init() when an output is available, the same
//  non-destructive reasoning as every other mode-entry handshake in this
//  port (see AkaiMpk249.cpp, ArturiaKeyLab61Mk2.cpp).
//
//  The 8 knobs (CC 21-28) are confirmed *absolute* by how Zynthian's own
//  driver consumes them (`ccval / 127.0` fed straight into a mixer level/
//  balance setter) -- contrast with the Launchkey Mini mk4 37, whose
//  equivalent knobs are confirmed *relative* and therefore NOT bound (see
//  NovationLaunchkeyMiniMk4.cpp's file header for why that distinction
//  matters to this framework).
//
//  The keybed and 16 pads (2 rows of 8) play as plain notes -- no function
//  pads for this device. Its transport buttons (Play/Record, and Track
//  Left/Right, Up/Down) are all CC-based with no `onCC()` hook available to
//  react to them (same reasoning as the Akai MPK249's transport CCs -- see
//  AkaiMpk249.cpp's file header), so they're left unbound rather than
//  half-implemented.
//
//  Protocol facts below are transcribed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_launchkey_mini_mk3.py on the vangelis
//  branch of zynthian/zynthian-ui) -- a working reference implementation,
//  not a guess, but not independently validated against physical hardware
//  in this project's development environment. Which target parameter each
//  knob maps to is this driver's own design choice, not a transcribed fact.
//

#include "NovationLaunchkeyMiniMk3.h"

#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

constexpr int kKnobCount = 8;

// -- Confirmed fixed CC assignment for the 8 knobs, active once "session
// mode" is entered (see init()) --
constexpr int kKnobCc[kKnobCount] = {21, 22, 23, 24, 25, 26, 27, 28};

/// Reuses the Akai MPK Mini mk3 driver's Mode 0 "sound" target set (see
/// AkaiMpkMiniMk3.cpp) -- consistent vocabulary across this port's drivers.
constexpr S1Parameter kKnobTarget[kKnobCount] = {
    cutoff, resonance, attackDuration, releaseDuration,
    lfo1Rate, reverbMix, delayMix, masterVolume};

class NovationLaunchkeyMiniMk3 : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substring (Zynthian's
        // dev_ids = ["Launchkey Mini MK3 IN 2"]). The WinMM szPname string
        // is NOT confirmed in this environment -- check with --list-midi on
        // real hardware and extend this list if it differs.
        return {"Launchkey Mini MK3", "Launchkey Mini Mk3", "Launchkey Mini mk3"};
    }

    const char *driverName() const override { return "novation-launchkey-mini-mk3"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter) override {
        (void)allowConfigure; // nothing to configure -- fixed CCs, no writable state
        (void)padFilter;      // no function pads for this device -- see the file header

        if (midiOut != nullptr && midiOut->isConnected()) {
            midiOut->noteOn(15, 12, 127); // enter session mode
        }

        for (int i = 0; i < kKnobCount; ++i) {
            engine.setDeviceDefaultCc(kKnobCc[i], kKnobTarget[i]);
        }
    }
};

} // namespace

std::unique_ptr<ControllerDriver> makeNovationLaunchkeyMiniMk3() {
    return std::make_unique<NovationLaunchkeyMiniMk3>();
}

} // namespace s1::ctrldev
