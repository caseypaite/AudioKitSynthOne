//
//  AkaiMpk249.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Akai MPK249. Unlike the MPK Mini mk3, this device has no
//  SysEx query this driver uses -- its 8 knobs and 8 faders send fixed CCs,
//  but which CCs depends on the device's own onboard preset, and Zynthian's
//  own driver (the source of every protocol fact below) documents that its
//  entire CC layout assumes the device is set to its onboard **factory
//  preset #25, "MPK Generic"** -- see the comment block at the top of
//  zyngine/ctrldev/zynthian_ctrldev_akai_mpk249.py on the vangelis branch of
//  zynthian/zynthian-ui. This driver has no way to select or confirm that
//  preset itself (no SysEx handshake for it is documented), so init() seeds
//  Engine's device-default CC map unconditionally, the same way the fixed-CC
//  Akai drivers do -- if the device isn't on Preset 25, the CCs below simply
//  won't match what it actually sends, and the knobs stay effectively
//  unbound (harmless -- MIDI Learn still works). This caveat is prominent in
//  the user guide.
//
//  The MPK249 has a front-panel BANK A/B/C switch that changes which CC
//  numbers the same physical knobs and switches transmit -- Zynthian's
//  source gives distinct, fully confirmed CC lists for Bank A's knobs
//  (KNOB_PAN_CCS) and Bank B's (CTRL_BANK_B_KNOB_CCS), so both are bound
//  below; whichever bank is actually selected on the device is the only one
//  that will ever transmit, so binding both ahead of time is harmless, not a
//  guess about which bank is active.
//
//  Deliberately NOT bound, despite being confirmed CCs in the same source:
//  - The 24 BANK A/B/C switch CCs (CTRL_BANK_A/B/C_SWITCH_CCS): Zynthian's
//    own driver treats these as per-mixer-channel solo/mute/record-arm
//    buttons (7 channel strips + 1 master, a Zynthian chain-mixer concept
//    with no equivalent in Synth One -- see the developer guide's "Why this
//    exists" section). There's no honest single-parameter target for "solo
//    channel 4," so these are left alone entirely rather than invented.
//  - 3 of the 6 TRANSPORT_*_CC constants (Rewind/Fast-Forward/Loop, CCs
//    114-116): Zynthian's own use for these (clear/clone/toggle the active
//    loop sequence) has no Synth One equivalent, unlike Play/Stop/Record
//    below, which do.
//
//  Play/Stop/Record (CCs 118/117/119) ARE claimed via CcFilter and handled
//  in onCC() -- panic / arp on-off / arp<->sequencer mode, the same
//  3-function transport mapping this port's Note-based transport buttons
//  use elsewhere (e.g. the Akai APC drivers). This is the first driver in
//  this port to make use of `ControllerDriver::onCC()` -- see CcFilter.h and
//  ControllerDriver.h for how a claimed CC reaches a driver without ever
//  running driver code on the audio render thread. Confirmed via Zynthian's
//  own `midi_event()`: these 3 CCs are only interpreted on MIDI channel 0
//  (`if ch == CTRL_MIDI_CH:`, `CTRL_MIDI_CH = 0`), so this driver claims them
//  on that specific channel rather than the wildcard default most of this
//  port's drivers use for an unconfirmed channel.
//
//  Bank C has no separate confirmed knob CC list in the source (only its
//  switch CCs are documented -- see above), so no Bank C knob target set
//  exists here either.
//
//  The keybed and all pads play notes normally -- no function-pad claims for
//  this device. Zynthian's own driver repurposes its pads for zynpad/pattern-
//  editor triggering, a concept with no equivalent in Synth One, the same
//  reasoning that leaves the switch CCs above unbound.
//
//  Which target parameter each fixed CC maps to, and which action Play/Stop/
//  Record perform, is this driver's own design choice, not a transcribed
//  fact -- see the parameter tables and onCC() below.
//

#include "AkaiMpk249.h"

#include <cstdint>
#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

constexpr int kKnobCount = 8;

// -- Confirmed fixed CC assignments (device's Preset 25, "MPK Generic" --
// see the file header) --
constexpr int kBankAKnobCc[kKnobCount] = {3, 9, 14, 15, 16, 17, 20, 19};
constexpr int kFaderCc[kKnobCount]     = {18, 21, 22, 23, 24, 25, 26, 27};
constexpr int kBankBKnobCc[kKnobCount] = {52, 53, 54, 55, 57, 58, 59, 60};

/// Bank A knobs: the same "sound" macro set the Akai MPK Mini mk3 driver
/// uses for its Mode 0 (see AkaiMpkMiniMk3.cpp) -- consistent vocabulary
/// across this port's Akai drivers.
constexpr S1Parameter kBankAKnobTarget[kKnobCount] = {
    cutoff, resonance, attackDuration, releaseDuration,
    lfo1Rate, reverbMix, delayMix, masterVolume};

/// Faders: reuses the MPK Mini mk3 driver's Mode 1 "oscillators/voice" set.
constexpr S1Parameter kFaderTarget[kKnobCount] = {
    morph1Volume, morph2Volume, morph2Detuning, subVolume,
    fmAmount, noiseVolume, glide, morphBalance};

/// Bank B knobs: reuses the MPK Mini mk3 driver's Mode 2 "envelope depth"
/// set.
constexpr S1Parameter kBankBKnobTarget[kKnobCount] = {
    filterAttackDuration, filterDecayDuration, filterSustainLevel, filterReleaseDuration,
    filterADSRMix, adsrPitchTracking, decayDuration, sustainLevel};

// -- Confirmed fixed transport CCs, channel 0 only (see the file header) --
constexpr int kTransportChannel = 0;
constexpr int kCcStop   = 117;
constexpr int kCcPlay   = 118;
constexpr int kCcRecord = 119;

class AkaiMpk249 : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substring (Zynthian's
        // dev_ids = ["MPK249 IN 1".."MPK249 IN 4"], one per the device's 4
        // MIDI ports; AlsaMidi.cpp composes MidiSource::name as
        // "<client> / <port>", so matching the client-name substring alone
        // catches all 4). The WinMM szPname string is NOT confirmed in this
        // environment -- check with --list-midi on real hardware and extend
        // this list if it differs.
        return {"MPK249"};
    }

    const char *driverName() const override { return "akai-mpk249"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter, CcFilter &ccFilter) override {
        mEngine = &engine;
        (void)midiOut;       // no SysEx TX needed -- see the file header
        (void)allowConfigure; // nothing to configure -- fixed CCs, no writable state
        (void)padFilter;      // no function pads for this device -- see the file header

        for (int i = 0; i < kKnobCount; ++i) {
            engine.setDeviceDefaultCc(kBankAKnobCc[i], kBankAKnobTarget[i]);
            engine.setDeviceDefaultCc(kFaderCc[i], kFaderTarget[i]);
            engine.setDeviceDefaultCc(kBankBKnobCc[i], kBankBKnobTarget[i]);
        }

        ccFilter.claimChannel(kTransportChannel);
        ccFilter.claimCc(kCcStop);
        ccFilter.claimCc(kCcPlay);
        ccFilter.claimCc(kCcRecord);
    }

    /// The 3 claimed transport CCs, handled entirely internally via Engine&
    /// -- mirrors the Akai APC drivers' Note-based transport buttons, just
    /// reached via onCC() instead of onPadButton(). Acts only when the CC
    /// value is non-zero, matching Zynthian's own `ccval > 0` press check
    /// for these same 3 CCs -- a release, if this device sends one at all,
    /// is not confirmed to send 0, so triggering on "any non-zero value"
    /// rather than a specific one is the safer read.
    void onCC(int channel, int cc, int value) override {
        (void)channel; // claimed on the confirmed transport channel -- see init()
        if (value <= 0 || mEngine == nullptr) return;
        if (cc == kCcStop) {
            mEngine->panic();
        } else if (cc == kCcPlay) {
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

std::unique_ptr<ControllerDriver> makeAkaiMpk249() {
    return std::make_unique<AkaiMpk249>();
}

} // namespace s1::ctrldev
