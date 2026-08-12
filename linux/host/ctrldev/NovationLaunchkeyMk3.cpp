//
//  NovationLaunchkeyMk3.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Novation Launchkey MK3 88. Same "session mode" Note On
//  handshake as the Launchkey Mini mk3 (see NovationLaunchkeyMiniMk3.cpp's
//  file header for why it's a plain noteOn(), not SysEx) -- this larger
//  sibling additionally has 8 physical faders and a master fader, all
//  confirmed *absolute* the same way the knobs are (Zynthian's own driver
//  feeds their raw CC values straight into `ccval / 127.0` mixer-level
//  setters).
//
//  The keybed and 16 pads play as plain notes -- no function pads for this
//  device. Its 8 per-channel CC_CHAIN_BUTTONS (select/mute/solo, depending
//  on mode) are a per-mixer-channel Zynthian concept with no Synth One
//  equivalent (see the developer guide's "Why this exists" section), and
//  its transport (Play/Record/Loop) and navigation buttons are all CC-based
//  with no `onCC()` hook available to react to them (same reasoning as the
//  Akai MPK249's transport CCs -- see AkaiMpk249.cpp's file header), so none
//  of them are bound.
//
//  Protocol facts below are transcribed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_launchkey_mk3_88.py on the vangelis
//  branch of zynthian/zynthian-ui) -- a working reference implementation,
//  not a guess, but not independently validated against physical hardware
//  in this project's development environment. Which target parameter each
//  knob/fader maps to is this driver's own design choice, not a transcribed
//  fact.
//

#include "NovationLaunchkeyMk3.h"

#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

constexpr int kStripCount = 8;

// -- Confirmed fixed CC assignments, active once "session mode" is entered
// (see init()) --
constexpr int kKnobCc[kStripCount]   = {21, 22, 23, 24, 25, 26, 27, 28};
constexpr int kFaderCc[kStripCount]  = {53, 54, 55, 56, 57, 58, 59, 60};
constexpr int kMasterFaderCc = 61;

/// Knobs: reuses the Akai MPK Mini mk3 driver's Mode 1 "oscillators/voice"
/// target set (see AkaiMpkMiniMk3.cpp).
constexpr S1Parameter kKnobTarget[kStripCount] = {
    morph1Volume, morph2Volume, morph2Detuning, subVolume,
    fmAmount, noiseVolume, glide, morphBalance};

/// Faders: reuses the MPK driver's Mode 0 "sound" set, minus master volume
/// (the master fader below is the more natural physical fit for that),
/// with arpRate filling the 8th slot -- the same substitution AkaiMidiMix.cpp
/// makes for its own fader row.
constexpr S1Parameter kFaderTarget[kStripCount] = {
    cutoff, resonance, attackDuration, releaseDuration,
    lfo1Rate, reverbMix, delayMix, arpRate};

class NovationLaunchkeyMk3_88 : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substring (Zynthian's
        // dev_ids = ["Launchkey MK3 88 IN 2"]). The WinMM szPname string is
        // NOT confirmed in this environment -- check with --list-midi on
        // real hardware and extend this list if it differs.
        return {"Launchkey MK3 88", "Launchkey Mk3 88", "Launchkey mk3 88"};
    }

    const char *driverName() const override { return "novation-launchkey-mk3-88"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter) override {
        (void)allowConfigure; // nothing to configure -- fixed CCs, no writable state
        (void)padFilter;      // no function pads for this device -- see the file header

        if (midiOut != nullptr && midiOut->isConnected()) {
            midiOut->noteOn(15, 12, 127); // enter session mode
        }

        for (int i = 0; i < kStripCount; ++i) {
            engine.setDeviceDefaultCc(kKnobCc[i], kKnobTarget[i]);
            engine.setDeviceDefaultCc(kFaderCc[i], kFaderTarget[i]);
        }
        engine.setDeviceDefaultCc(kMasterFaderCc, masterVolume);
    }
};

} // namespace

std::unique_ptr<ControllerDriver> makeNovationLaunchkeyMk3_88() {
    return std::make_unique<NovationLaunchkeyMk3_88>();
}

} // namespace s1::ctrldev
