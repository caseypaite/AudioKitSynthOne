//
//  BehringerMotor.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Behringer MOTÖR61/MOTÖR49 motorized-fader keyboard. This
//  is the richest driver in this port's fixed-CC family: 25 confirmed
//  absolute fader/encoder CCs (originally laid out by Zynthian for
//  setBfree drawbars and Pianoteq parameters -- a Hammond-organ emulator
//  and a piano engine, both with no equivalent in Synth One) plus 32
//  confirmed pad notes used there for musical-scale/key masking (also no
//  Synth One equivalent -- see the developer guide's "Why this exists"
//  section). Every CC/note number below is transcribed as a protocol fact;
//  which Synth One parameter or action each one maps to is this driver's
//  own design choice, not a transcribed one.
//
//  All faders/encoders/pads are confirmed to arrive on a single MIDI
//  channel (channel 2, 0-indexed 1 -- `evchan == 1` gates every branch of
//  Zynthian's own `midi_event()`), with no SysEx or mode-entry handshake
//  needed at all -- so, like the Akai MIDI Mix and Korg nanoKONTROL2,
//  init() seeds Engine's device-default CC map directly from a
//  compile-time table.
//
//  Faders/encoders bound (25 of Zynthian's confirmed CCs):
//  - Upper-bank faders (CC 21-28, 8 drawbar faders): reuses the Akai MPK
//    Mini mk3 driver's Mode 0 "sound" target set.
//  - Lower-bank faders (CC 29-36, 8): reuses the MPK driver's Mode 1
//    "oscillators/voice" set.
//  - Pedal-bank faders (CC 37-44, 8): reuses the MPK driver's Mode 2
//    "envelope depth" set.
//  - Master fader (CC 53): masterVolume.
//  - Upper-bank encoders 1-4 (CC 71-74) + the "Pianoteq" encoders 5-8
//    (CC 75-78), 8 total: reuses the MPK driver's Mode 3 "modulation/FX"
//    set.
//  - Lower-bank encoder 1 (CC 79): arpRate.
//  - Pedal-bank encoder 1 (CC 87): widen.
//
//  Not bound: the "fader touch" notes (0-32, motorized-fader touch-sense,
//  channel 1) -- these exist purely so Zynthian can suppress motor feedback
//  while a fader is being physically held, a concern only relevant to a
//  driver that pushes fader *positions back* to the device, which this
//  framework's `ControllerDriver` interface has no concept of at all (see
//  the developer guide -- no driver in this port does LED/motor feedback).
//  Left unclaimed, they pass through as ordinary (very low, essentially
//  inaudible) notes, same as any other unclaimed control.
//
//  4 of this device's 32 mode-select pads (notes 66-97, the first 4 of
//  "Bank A") are claimed as function pads -- panic / all notes off / arp
//  on-off / arp<->sequencer mode, the same 4-function transport mapping the
//  Akai MPK Mini mk3 driver's top row uses. The other 28 are deliberately
//  left unclaimed: 4 already covers every established "transport" function
//  this port's drivers use, and inventing 28 more would be exactly the kind
//  of speculative design the developer guide's "Why this exists" section
//  cautions against.
//
//  Protocol facts below are transcribed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_behringer_motor.py on the vangelis
//  branch of zynthian/zynthian-ui) -- a working reference implementation,
//  not a guess, but not independently validated against physical hardware
//  in this project's development environment.
//

#include "BehringerMotor.h"

#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

constexpr int kBankSize = 8;

// -- Confirmed fixed CC assignments, all on MIDI channel 2 (see the file
// header) --
constexpr int kUpperFaderCc[kBankSize] = {21, 22, 23, 24, 25, 26, 27, 28};
constexpr int kLowerFaderCc[kBankSize] = {29, 30, 31, 32, 33, 34, 35, 36};
constexpr int kPedalFaderCc[kBankSize] = {37, 38, 39, 40, 41, 42, 43, 44};
constexpr int kMasterFaderCc = 53;
constexpr int kUpperEncoderCc[4] = {71, 72, 73, 74};
constexpr int kPianoteqEncoderCc[4] = {75, 76, 77, 78};
constexpr int kLowerEncoderCc = 79;
constexpr int kPedalEncoderCc = 87;

// -- Confirmed fixed pad notes (bank A, first 4 of 32) --
constexpr int kPadChannel      = 1;
constexpr int kNotePanic       = 66;
constexpr int kNoteAllNotesOff = 67;
constexpr int kNoteArpOn       = 68;
constexpr int kNoteArpSeq      = 69;

/// Reuses the Akai MPK Mini mk3 driver's Mode 0 "sound" target set.
constexpr S1Parameter kUpperFaderTarget[kBankSize] = {
    cutoff, resonance, attackDuration, releaseDuration,
    lfo1Rate, reverbMix, delayMix, masterVolume};

/// Reuses the MPK driver's Mode 1 "oscillators/voice" target set.
constexpr S1Parameter kLowerFaderTarget[kBankSize] = {
    morph1Volume, morph2Volume, morph2Detuning, subVolume,
    fmAmount, noiseVolume, glide, morphBalance};

/// Reuses the MPK driver's Mode 2 "envelope depth" target set.
constexpr S1Parameter kPedalFaderTarget[kBankSize] = {
    filterAttackDuration, filterDecayDuration, filterSustainLevel, filterReleaseDuration,
    filterADSRMix, adsrPitchTracking, decayDuration, sustainLevel};

/// Reuses the MPK driver's Mode 3 "modulation/FX" target set, split across
/// the two encoder banks that make up 8 controls.
constexpr S1Parameter kUpperEncoderTarget[4] = {lfo1Amplitude, lfo2Rate, lfo2Amplitude, phaserMix};
constexpr S1Parameter kPianoteqEncoderTarget[4] = {phaserRate, autoPanAmount, autoPanFrequency, bitCrushSampleRate};

class BehringerMotor : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substrings (Zynthian's
        // dev_ids = ["MOTÖR61 Keyboard IN 1", "MOTÖR49 Keyboard IN 1"]).
        // The WinMM szPname string is NOT confirmed in this environment --
        // check with --list-midi on real hardware and extend this list if
        // it differs.
        return {"MOTÖR61 Keyboard", "MOTÖR49 Keyboard", "MOTOR61 Keyboard", "MOTOR49 Keyboard"};
    }

    const char *driverName() const override { return "behringer-motor"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter, CcFilter &ccFilter) override {
        mEngine = &engine;
        (void)midiOut;       // no SysEx TX needed -- this device has none
        (void)allowConfigure; // nothing to configure -- fixed CCs, no writable state
        (void)ccFilter;       // this device's claimed pads are Note-based
                              // (see padFilter below), not CC

        for (int i = 0; i < kBankSize; ++i) {
            engine.setDeviceDefaultCc(kUpperFaderCc[i], kUpperFaderTarget[i]);
            engine.setDeviceDefaultCc(kLowerFaderCc[i], kLowerFaderTarget[i]);
            engine.setDeviceDefaultCc(kPedalFaderCc[i], kPedalFaderTarget[i]);
        }
        engine.setDeviceDefaultCc(kMasterFaderCc, masterVolume);
        for (int i = 0; i < 4; ++i) {
            engine.setDeviceDefaultCc(kUpperEncoderCc[i], kUpperEncoderTarget[i]);
            engine.setDeviceDefaultCc(kPianoteqEncoderCc[i], kPianoteqEncoderTarget[i]);
        }
        engine.setDeviceDefaultCc(kLowerEncoderCc, arpRate);
        engine.setDeviceDefaultCc(kPedalEncoderCc, widen);

        // Channel is explicitly confirmed here (Zynthian's own dispatch
        // gates everything behind `evchan == 1`), unlike most drivers in
        // this port, which default to a wildcard channel because it's
        // unconfirmed.
        padFilter.claimChannel(kPadChannel);
        padFilter.claimNote(kNotePanic);
        padFilter.claimNote(kNoteAllNotesOff);
        padFilter.claimNote(kNoteArpOn);
        padFilter.claimNote(kNoteArpSeq);
    }

    /// The 4 claimed pads, handled entirely internally via Engine& --
    /// mirrors the Akai MPK Mini mk3 driver's top-row pads. Acts only on
    /// the down edge, matching that convention.
    PadReport onPadButton(int channel, int note, bool isDown) override {
        (void)channel; // claimed on the confirmed pad channel -- see init()
        if (isDown && mEngine != nullptr) {
            if (note == kNotePanic) {
                mEngine->panic();
            } else if (note == kNoteAllNotesOff) {
                mEngine->allNotesOff();
            } else if (note == kNoteArpOn) {
                mEngine->setParameter(
                    arpIsOn, mEngine->getParameter(arpIsOn) != 0.0f ? 0.0f : 1.0f);
            } else if (note == kNoteArpSeq) {
                mEngine->setParameter(
                    arpIsSequencer,
                    mEngine->getParameter(arpIsSequencer) != 0.0f ? 0.0f : 1.0f);
            }
        }
        return {};
    }

private:
    Engine *mEngine = nullptr;
};

} // namespace

std::unique_ptr<ControllerDriver> makeBehringerMotor() {
    return std::make_unique<BehringerMotor>();
}

} // namespace s1::ctrldev
