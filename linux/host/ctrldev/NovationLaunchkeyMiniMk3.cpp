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
//  pads for this device. Play (CC 0x73) and Record (CC 0x75) ARE claimed via
//  CcFilter and handled in onCC() -- arp on-off / arp<->sequencer mode --
//  confirmed by Zynthian's own dispatch to arrive only on channel 15 (0xF),
//  the same "if chan == 0xf:" gate the knob CCs are read under, so this
//  driver claims them on that specific channel. Track Left/Right/Up/Down are
//  also CC-based but have no equivalent Synth One action worth inventing, so
//  they're left unclaimed.
//
//  Protocol facts below are transcribed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_launchkey_mini_mk3.py on the vangelis
//  branch of zynthian/zynthian-ui) -- a working reference implementation,
//  not a guess, but not independently validated against physical hardware
//  in this project's development environment. Which target parameter each
//  knob maps to, and which action Play/Record perform, is this driver's own
//  design choice, not a transcribed fact.
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

// -- Confirmed fixed transport CCs, "session mode" channel 15 only --
constexpr int kSessionChannel = 15;
constexpr int kCcPlay   = 0x73;
constexpr int kCcRecord = 0x75;

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
             PadFilter &padFilter, CcFilter &ccFilter) override {
        mEngine = &engine;
        (void)allowConfigure; // nothing to configure -- fixed CCs, no writable state
        (void)padFilter;      // no function pads for this device -- see the file header

        if (midiOut != nullptr && midiOut->isConnected()) {
            midiOut->noteOn(15, 12, 127); // enter session mode
        }

        for (int i = 0; i < kKnobCount; ++i) {
            engine.setDeviceDefaultCc(kKnobCc[i], kKnobTarget[i]);
        }

        ccFilter.claimChannel(kSessionChannel);
        ccFilter.claimCc(kCcPlay);
        ccFilter.claimCc(kCcRecord);
    }

    /// The 2 claimed transport CCs, handled entirely internally via Engine&.
    /// Acts only when the CC value is non-zero, matching Zynthian's own
    /// button-press convention for these same CCs.
    void onCC(int channel, int cc, int value) override {
        (void)channel; // claimed on the confirmed session channel -- see init()
        if (value <= 0 || mEngine == nullptr) return;
        if (cc == kCcPlay) {
            mEngine->setParameter(
                arpIsOn, mEngine->getParameter(arpIsOn) != 0.0f ? 0.0f : 1.0f);
        } else if (cc == kCcRecord) {
            mEngine->setParameter(
                arpIsSequencer,
                mEngine->getParameter(arpIsSequencer) != 0.0f ? 0.0f : 1.0f);
        }
    }

private:
    Engine *mEngine = nullptr;
};

} // namespace

std::unique_ptr<ControllerDriver> makeNovationLaunchkeyMiniMk3() {
    return std::make_unique<NovationLaunchkeyMiniMk3>();
}

} // namespace s1::ctrldev
