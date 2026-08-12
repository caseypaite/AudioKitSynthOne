//
//  NovationLaunchkeyMk4.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Novation Launchkey MK4 37. Same "session mode" Note On
//  handshake as the mk3 Launchkey drivers (see NovationLaunchkeyMiniMk3.cpp's
//  file header for why it's a plain noteOn(), not SysEx), and the same
//  8-knob CC row (21-28) at the same addresses -- confirmed *absolute* here
//  too (Zynthian's own driver feeds them straight into
//  `zynmixer.set_level()`/a `ZYNPOT_ABS`-named CUIA, both unambiguously
//  absolute-position consumers). Contrast with the Launchkey Mini mk4 37,
//  whose knobs are on different CCs and confirmed *relative* instead (see
//  NovationLaunchkeyMiniMk4.cpp's file header).
//
//  The keybed and pads play as plain notes -- no function pads for this
//  device. Its ZynSwitch buttons (bank/track navigation, metronome) and
//  Track Left/Right/Up/Down navigation are CC-based with no equivalent
//  Synth One action worth inventing, so they're left unbound. Play (CC
//  0x73) and Record (CC 0x75) ARE claimed via CcFilter and handled in
//  onCC() -- arp on-off / arp<->sequencer mode -- but unlike the mk3-
//  generation Launchkeys, Zynthian's own dispatch never gates these two on
//  a specific channel, so this driver claims them on the wildcard default
//  rather than a confirmed one.
//
//  Protocol facts below are transcribed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_launchkey_mk4_37.py on the vangelis
//  branch of zynthian/zynthian-ui) -- a working reference implementation,
//  not a guess, but not independently validated against physical hardware
//  in this project's development environment. Which target parameter each
//  knob maps to, and which action Play/Record perform, is this driver's own
//  design choice, not a transcribed fact.
//

#include "NovationLaunchkeyMk4.h"

#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

constexpr int kKnobCount = 8;

// -- Confirmed fixed CC assignment for the 8 knobs, active once "DAW mode"
// is entered (see init()) --
constexpr int kKnobCc[kKnobCount] = {21, 22, 23, 24, 25, 26, 27, 28};

/// Reuses the Akai MPK Mini mk3 driver's Mode 2 "envelope depth" target set
/// (see AkaiMpkMiniMk3.cpp) -- consistent vocabulary across this port's
/// drivers, and distinct from the other Launchkey drivers' choices for
/// variety across the family.
constexpr S1Parameter kKnobTarget[kKnobCount] = {
    filterAttackDuration, filterDecayDuration, filterSustainLevel, filterReleaseDuration,
    filterADSRMix, adsrPitchTracking, decayDuration, sustainLevel};

// -- Confirmed fixed transport CCs (channel unconfirmed -- see the file
// header) --
constexpr int kCcPlay   = 0x73;
constexpr int kCcRecord = 0x75;

class NovationLaunchkeyMk4_37 : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substrings (Zynthian's
        // dev_ids = ["Launchkey MK4 37 DAW In", "Launchkey MK4 37 IN 2"]).
        // The WinMM szPname string is NOT confirmed in this environment --
        // check with --list-midi on real hardware and extend this list if
        // it differs. Deliberately excludes a bare "Launchkey MK4" that
        // could also appear in other MK4-family device names.
        return {"Launchkey MK4 37", "Launchkey Mk4 37", "Launchkey mk4 37"};
    }

    const char *driverName() const override { return "novation-launchkey-mk4-37"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter, CcFilter &ccFilter) override {
        mEngine = &engine;
        (void)allowConfigure; // nothing to configure -- fixed CCs, no writable state
        (void)padFilter;      // no function pads for this device -- see the file header

        if (midiOut != nullptr && midiOut->isConnected()) {
            midiOut->noteOn(15, 12, 127); // enter DAW mode
        }

        for (int i = 0; i < kKnobCount; ++i) {
            engine.setDeviceDefaultCc(kKnobCc[i], kKnobTarget[i]);
        }

        ccFilter.claimCc(kCcPlay);
        ccFilter.claimCc(kCcRecord);
    }

    /// The 2 claimed transport CCs, handled entirely internally via Engine&.
    /// Acts only when the CC value is non-zero, matching Zynthian's own
    /// button-press convention for these same CCs.
    void onCC(int channel, int cc, int value) override {
        (void)channel; // claimed with a wildcard channel -- see the file header
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

std::unique_ptr<ControllerDriver> makeNovationLaunchkeyMk4_37() {
    return std::make_unique<NovationLaunchkeyMk4_37>();
}

} // namespace s1::ctrldev
