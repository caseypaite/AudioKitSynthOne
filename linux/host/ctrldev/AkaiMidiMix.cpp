//
//  AkaiMidiMix.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Akai MIDI Mix. Unlike the MPK Mini mk3, this device has no
//  SysEx interface at all -- it's a class-compliant control surface that
//  always sends a fixed set of CCs and note numbers in "MIDI mode" (its only
//  mode; there's no onboard program storage or PROG SELECT-style switching).
//  So this driver's init() seeds Engine's device-default CC map directly from
//  a compile-time table, with no query/reply round trip -- see the developer
//  guide's "Adding a driver" step 4.
//
//  Layout: 8 channel strips, each with 3 stacked knobs and a fader, plus a
//  master fader and 3 global buttons (SOLO, BANK LEFT, BANK RIGHT) and,
//  per strip, MUTE/SOLO/REC buttons. Only the 3 global buttons are claimed
//  as function pads here (panic / arp on-off / arp<->sequencer mode, mirroring
//  the closest four "transport" functions the Akai MPK Mini mk3 driver uses
//  for its own top-row pads -- this device only has 3 unclaimed candidates,
//  not 4). The 24 per-strip MUTE/SOLO/REC buttons are left unclaimed, the
//  same "no driver code needed" default every other pad/key gets (see the
//  developer guide's "Why this exists" section) -- Synth One has no per-track
//  mixer concept for them to control, and inventing one would be exactly the
//  kind of speculative design the driver framework avoids.
//
//  CC numbers, note numbers and the SOLO/BANK LEFT/BANK RIGHT/MUTE/SOLO/REC
//  note assignments below are transcribed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_akai_midimix.py on the vangelis branch
//  of zynthian/zynthian-ui) -- a working reference implementation (Zynthian
//  ships full mixer-strip integration built on these same constants), not a
//  guess. The knob-row-1/knob-row-2 CCs are present in that source only as a
//  commented-out table (Zynthian's own mixer integration uses just the third
//  knob row), but the numbers themselves come from the same confirmed source
//  as everything else here. Not independently validated against physical
//  hardware in this project's development environment. Which target
//  parameter each fixed CC/note maps to is this driver's own design choice,
//  not a transcribed fact -- see the parameter tables below.
//

#include "AkaiMidiMix.h"

#include <cstdint>
#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

constexpr int kStripCount = 8;

// -- Confirmed fixed CC assignments (device's only mode) --
// Row 1 and row 2 knobs are commented out in Zynthian's own driver (it maps
// only row 3), but the CC numbers are already part of that source.
constexpr int kKnobRow1Cc[kStripCount] = {16, 20, 24, 28, 46, 50, 54, 58};
constexpr int kKnobRow2Cc[kStripCount] = {17, 21, 25, 29, 47, 51, 55, 59};
constexpr int kKnobRow3Cc[kStripCount] = {18, 22, 26, 30, 48, 52, 56, 60};
constexpr int kFaderCc[kStripCount]    = {19, 23, 27, 31, 49, 53, 57, 61};
constexpr int kMasterFaderCc = 62;

// -- Confirmed fixed note assignments (device's only mode) --
// Per-strip MUTE (1,4,7,10,13,16,19,22)/SOLO (2,5,8,11,14,17,20,23)/
// REC (3,6,9,12,15,18,21,24) notes are deliberately not claimed here -- see
// the file header -- so no table for them is needed below.
constexpr int kBankLeftNote  = 25;
constexpr int kBankRightNote = 26;
constexpr int kSoloGlobalNote = 27;

/// Row 1: oscillators/voice -- reuses the same target set as the Akai MPK
/// Mini mk3 driver's Mode 1 (see AkaiMpkMiniMk3.cpp), since it's an equally
/// good fit here and keeps a knob's meaning consistent for anyone using both
/// drivers.
constexpr S1Parameter kKnobRow1Target[kStripCount] = {
    morph1Volume, morph2Volume, morph2Detuning, subVolume,
    fmAmount, noiseVolume, glide, morphBalance};

/// Row 2: envelope depth -- reuses the MPK driver's Mode 2 target set.
constexpr S1Parameter kKnobRow2Target[kStripCount] = {
    filterAttackDuration, filterDecayDuration, filterSustainLevel, filterReleaseDuration,
    filterADSRMix, adsrPitchTracking, decayDuration, sustainLevel};

/// Row 3: modulation/FX depth -- reuses the MPK driver's Mode 3 target set.
constexpr S1Parameter kKnobRow3Target[kStripCount] = {
    lfo1Amplitude, lfo2Rate, lfo2Amplitude, phaserMix,
    phaserRate, autoPanAmount, autoPanFrequency, bitCrushSampleRate};

/// The 8 channel faders: core sound-shaping macros, the same set the MPK
/// driver's Mode 0 uses for its knobs minus master volume (the master fader
/// below is the more natural physical fit for that), with arpRate filling
/// the 8th slot instead.
constexpr S1Parameter kFaderTarget[kStripCount] = {
    cutoff, resonance, attackDuration, releaseDuration,
    lfo1Rate, reverbMix, delayMix, arpRate};

class AkaiMidiMix : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // "MIDI Mix" is the confirmed ALSA client-name substring (Zynthian's
        // dev_ids = ["MIDI Mix IN 1"]; AlsaMidi.cpp composes MidiSource::name
        // as "<client> / <port>", so matching the client-name substring alone
        // is robust to the port suffix). The WinMM szPname string is NOT
        // confirmed in this environment -- check with --list-midi on real
        // hardware and extend this list if it differs.
        return {"MIDI Mix", "MIDIMIX"};
    }

    const char *driverName() const override { return "akai-midimix"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter, CcFilter &ccFilter) override {
        mEngine = &engine;
        (void)midiOut;       // no SysEx TX needed -- this device has none
        (void)allowConfigure; // nothing to configure -- fixed CCs, no writable state
        (void)ccFilter;       // this device's global buttons are Note-based
                              // (claimed via padFilter below), not CC

        for (int i = 0; i < kStripCount; ++i) {
            engine.setDeviceDefaultCc(kKnobRow1Cc[i], kKnobRow1Target[i]);
            engine.setDeviceDefaultCc(kKnobRow2Cc[i], kKnobRow2Target[i]);
            engine.setDeviceDefaultCc(kKnobRow3Cc[i], kKnobRow3Target[i]);
            engine.setDeviceDefaultCc(kFaderCc[i], kFaderTarget[i]);
        }
        engine.setDeviceDefaultCc(kMasterFaderCc, masterVolume);

        // Only the 3 global buttons are claimed -- see the file header for
        // why the 24 per-strip MUTE/SOLO/REC buttons are left as plain
        // notes. Channel is unconfirmed for the input side (Zynthian's
        // driver never checks it on receipt, only hardcodes channel 0 when
        // it sends LED feedback out), so this claims on any channel, same
        // degrade-safely default AkaiMpkMiniMk3 uses for its own pads.
        padFilter.claimNote(kSoloGlobalNote);
        padFilter.claimNote(kBankLeftNote);
        padFilter.claimNote(kBankRightNote);
    }

    /// The 3 claimed global buttons, handled entirely internally via Engine&
    /// -- there's no GUI/UiState concept any of them needs, unlike the MPK
    /// driver's bottom-row pads. Acts only on the down edge, matching the
    /// MPK driver's top-row convention.
    PadReport onPadButton(int channel, int note, bool isDown) override {
        (void)channel; // claimed with a wildcard channel -- see init()
        if (isDown && mEngine != nullptr) {
            if (note == kSoloGlobalNote) {
                mEngine->panic();
            } else if (note == kBankLeftNote) {
                mEngine->setParameter(
                    arpIsOn, mEngine->getParameter(arpIsOn) != 0.0f ? 0.0f : 1.0f);
            } else if (note == kBankRightNote) {
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

std::unique_ptr<ControllerDriver> makeAkaiMidiMix() {
    return std::make_unique<AkaiMidiMix>();
}

} // namespace s1::ctrldev
