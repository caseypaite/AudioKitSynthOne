//
//  AkaiApc40Mk2.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Akai APC40 mk2. Like the other APC/MIDI Mix drivers in
//  this port, this device needs no SysEx: its knobs, faders and buttons
//  report fixed Note/CC numbers in the device's default power-on state
//  ("Generic Mode", per the protocol doc below), so init() seeds Engine's
//  device-default CC map directly, no query/reply round trip.
//
//  Protocol facts below (CC/note numbers, which controls vary by MIDI
//  channel and which don't) are transcribed from Akai's own official
//  published protocol document -- "Akai APC40 Mk2 Communications Protocol",
//  version 1.2, cited directly in Zynthian's own driver header
//  (zyngine/ctrldev/zynthian_ctrldev_akai_apc_40_mk2.py on the vangelis
//  branch of zynthian/zynthian-ui):
//  https://cdn.inmusicbrands.com/akai/attachments/apc40II/APC40Mk2_Communications_Protocol_v1.2.pdf
//  This is a primary source, not a guess, and not merely a third-party
//  transcription -- but still not independently validated against physical
//  hardware in this project's development environment.
//
//  IMPORTANT, confirmed directly from that document: Engine's device-default
//  CC table (Engine::mDeviceDefaultCc, see Engine::handleMidi's `data[0] &
//  0xF0` status mask) is channel-blind -- a bound CC fires the same way no
//  matter which of the 16 MIDI channels it arrives on. Most of this device's
//  controls are unaffected (each has its own fixed, single-channel CC/note
//  number), but two are NOT: TRACK FADER (all 8 physical faders send the
//  *same* CC, 0x07, on channels 0-7 to indicate which one moved) and the 5
//  per-track buttons (RECORD ARM/SOLO/ACTIVATOR/TRACK SELECTION/TRACK STOP,
//  notes 0x30-0x34, same channel-per-track scheme). Binding TRACK FADER to a
//  target still works -- moving *any* of the 8 faders drives that one bound
//  parameter, since this framework can't tell them apart -- so it's given
//  exactly one target below rather than 8 (see kFaderTrackTarget). The 5
//  per-track buttons are left unclaimed entirely, for the same reason the 40
//  clip-launch pads are: Synth One has no per-track concept for a specific
//  track's button to mean anything, and claiming one channel-blind note
//  shared across 8 "tracks" would just be a single confusing button, not a
//  useful one.
//
//  Two controls are deliberately NOT bound to anything: TEMPO KNOB (Control
//  ID 0x0D) reports as a *relative* controller (a delta since the last
//  report, not a position), which Engine::handleMidi's CC path always
//  interprets as an absolute 0-127 position -- binding it would make the
//  knob snap the target parameter towards its minimum on every nudge, not
//  smoothly adjust it. CUE LEVEL (Control ID 0x2F) is listed in the protocol
//  doc under *both* the absolute (Type CC1) and relative (Type CC2) message
//  tables with no resolving note -- given that ambiguity, it's left unbound
//  rather than guessed at.
//
//  Grid pads (0x00-0x27, 40 clip-launch cells) and the per-track buttons
//  mentioned above play/act as plain, unclaimed notes -- Synth One has no
//  clip/track concept for them to control (see the developer guide's "Why
//  this exists" section). Only 3 of the device's dedicated global transport
//  buttons are claimed as function pads: STOP ALL CLIPS, PLAY and RECORD --
//  the same 3, at the same note numbers, the Akai APC Key 25 driver claims
//  (see AkaiApcKey25.cpp), mirroring the Akai MPK Mini mk3 driver's top-row
//  pads (panic / arp on-off / arp<->sequencer mode).
//
//  Which target parameter each fixed CC/note maps to is this driver's own
//  design choice, not a transcribed fact -- see the parameter tables below.
//

#include "AkaiApc40Mk2.h"

#include <cstdint>
#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

constexpr int kKnobCount = 8;

// -- Confirmed fixed CC assignments, all single-channel and independently
// bindable (each of the 8 knobs in a row has its own Control ID) --
constexpr int kDeviceKnobCc[kKnobCount] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
constexpr int kTrackKnobCc[kKnobCount]  = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37};
constexpr int kMasterFaderCc = 0x0E;
constexpr int kCrossfaderCc  = 0x0F;

// -- Confirmed fixed CC assignment shared across all 8 physical track
// faders (channel-blind here -- see the file header) --
constexpr int kTrackFaderCc = 0x07;

// -- Confirmed fixed transport-button note numbers (single-channel; same
// values as the Akai APC Key 25's BTN_STOP_ALL_CLIPS/BTN_PLAY/BTN_RECORD) --
constexpr int kNoteStopAllClips = 0x51; // 81
constexpr int kNotePlay         = 0x5B; // 91
constexpr int kNoteRecord       = 0x5D; // 93

/// Device Control knob row: reuses the Akai MPK Mini mk3 driver's Mode 2
/// "envelope depth" target set (see AkaiMpkMiniMk3.cpp) -- consistent
/// vocabulary across this port's Akai drivers.
constexpr S1Parameter kDeviceKnobTarget[kKnobCount] = {
    filterAttackDuration, filterDecayDuration, filterSustainLevel, filterReleaseDuration,
    filterADSRMix, adsrPitchTracking, decayDuration, sustainLevel};

/// Track Control knob row: reuses the MPK driver's Mode 1 "oscillators/
/// voice" target set.
constexpr S1Parameter kTrackKnobTarget[kKnobCount] = {
    morph1Volume, morph2Volume, morph2Detuning, subVolume,
    fmAmount, noiseVolume, glide, morphBalance};

class AkaiApc40Mk2 : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substring (Zynthian's
        // dev_ids = ["APC40 mkII IN 1"]; AlsaMidi.cpp composes
        // MidiSource::name as "<client> / <port>", so matching the
        // client-name substring alone is robust to the port suffix). The
        // WinMM szPname string is NOT confirmed in this environment -- check
        // with --list-midi on real hardware and extend this list if it
        // differs.
        return {"APC40 mkII", "APC40 MkII", "APC40 mk2"};
    }

    const char *driverName() const override { return "akai-apc40-mk2"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter, CcFilter &ccFilter) override {
        mEngine = &engine;
        (void)midiOut;       // no SysEx TX needed -- the default power-on
                             // mode needs none of the controls this driver
                             // uses (see the file header)
        (void)allowConfigure; // nothing to configure -- fixed CCs, no writable state
        (void)ccFilter;       // no CC-based function triggers for this device

        for (int i = 0; i < kKnobCount; ++i) {
            engine.setDeviceDefaultCc(kDeviceKnobCc[i], kDeviceKnobTarget[i]);
            engine.setDeviceDefaultCc(kTrackKnobCc[i], kTrackKnobTarget[i]);
        }
        engine.setDeviceDefaultCc(kMasterFaderCc, masterVolume);
        engine.setDeviceDefaultCc(kCrossfaderCc, widen);
        // Only one target despite 8 physical faders -- see the file header
        // for why TRACK FADER can't be bound per-fader through this
        // channel-blind table.
        engine.setDeviceDefaultCc(kTrackFaderCc, reverbMix);

        // Channel is confirmed irrelevant for these 3 (outside the
        // 0x30-0x49 per-track range the protocol doc calls out), so this
        // claims on any channel, the same degrade-safely default the rest
        // of this port's drivers use.
        padFilter.claimNote(kNoteStopAllClips);
        padFilter.claimNote(kNotePlay);
        padFilter.claimNote(kNoteRecord);
    }

    /// Identical shape to AkaiApcKey25::onPadButton() -- see that file.
    PadReport onPadButton(int channel, int note, bool isDown) override {
        (void)channel; // claimed with a wildcard channel -- see init()
        if (isDown && mEngine != nullptr) {
            if (note == kNoteStopAllClips) {
                mEngine->panic();
            } else if (note == kNotePlay) {
                mEngine->setParameter(
                    arpIsOn, mEngine->getParameter(arpIsOn) != 0.0f ? 0.0f : 1.0f);
            } else if (note == kNoteRecord) {
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

std::unique_ptr<ControllerDriver> makeAkaiApc40Mk2() {
    return std::make_unique<AkaiApc40Mk2>();
}

} // namespace s1::ctrldev
