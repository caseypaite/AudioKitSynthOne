//
//  AkaiApcKey25.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Akai APC Key 25 (original/gen 1). Like the MIDI Mix, this
//  device is class-compliant and has no SysEx interface, no onboard program
//  storage, and no PROG SELECT-style mode switching -- the 8 knobs above the
//  keybed always send the same 8 fixed CCs, so init() seeds Engine's
//  device-default CC map directly, no query/reply round trip (see the
//  developer guide's "Adding a driver" step 4).
//
//  The keybed plays notes normally, as does the 8x5 clip-launch pad grid
//  below it (notes 0-39) -- Synth One has no clip/scene concept for those 40
//  pads to control, so, per the developer guide's "Why this exists" section,
//  they get no driver code at all rather than an invented repurposing. Only
//  3 of the device's dedicated (non-grid) transport buttons are claimed as
//  function pads: STOP ALL CLIPS, PLAY and RECORD -- the closest things to
//  "transport" both this device and Synth One have, mirroring the Akai MPK
//  Mini mk3 driver's top-row pads (panic / arp on-off / arp<->sequencer
//  mode). Everything else dedicated-but-unclaimed on this device (the 5 soft
//  keys under the grid, the 8 track-select buttons above the knobs, SHIFT)
//  is left alone for the same "no equivalent in Synth One" reason as the
//  grid -- Zynthian's own driver uses them for chain/mixer/preset-browser/
//  admin-screen navigation, none of which this synth has.
//
//  Protocol facts below (fixed knob CCs, transport button note numbers, and
//  the confirmed ALSA name strings used to tell this device apart from the
//  separately-driven APC Key 25 mk2 -- see AkaiApcKey25Mk2.cpp) are
//  transcribed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_akai_apc_key25.py on the vangelis
//  branch of zynthian/zynthian-ui), which subclasses and reuses essentially
//  all of zynthian_ctrldev_akai_apc_key25_mk2.py's note/CC constants for the
//  gen-1 hardware -- a working reference implementation, not a guess, but
//  not independently validated against physical hardware in this project's
//  development environment. Which target parameter each fixed CC/note maps
//  to is this driver's own design choice, not a transcribed fact -- see the
//  parameter table and the 3 claimed buttons below.
//

#include "AkaiApcKey25.h"

#include <cstdint>
#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

constexpr int kKnobCount = 8;

// -- Confirmed fixed CC assignments (device's only mode; KNOB_1..KNOB_8 in
// Zynthian's source, 0x30-0x37) --
constexpr int kKnobCc[kKnobCount] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37};

// -- Confirmed fixed transport-button note numbers (Zynthian's
// BTN_STOP_ALL_CLIPS/BTN_PLAY/BTN_RECORD) --
constexpr int kNoteStopAllClips = 0x51; // 81
constexpr int kNotePlay         = 0x5B; // 91
constexpr int kNoteRecord       = 0x5D; // 93

/// The 8 knobs' target parameters: the same "sound" macro set the Akai MPK
/// Mini mk3 driver uses for its Mode 0 (see AkaiMpkMiniMk3.cpp) -- a good fit
/// here too, and keeps a knob's meaning consistent for anyone using both
/// drivers. This device has only one knob row and no mode switching, so
/// there's nothing to choose between the way the MPK driver's 4 modes do.
constexpr S1Parameter kKnobTarget[kKnobCount] = {
    cutoff, resonance, attackDuration, releaseDuration,
    lfo1Rate, reverbMix, delayMix, masterVolume};

class AkaiApcKey25 : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // Deliberately the exact confirmed strings, not a bare "APC Key 25"
        // substring -- the mk2's confirmed name ("APC Key 25 mk2 MIDI 2" /
        // "APC Key 25 mk2 IN 2", see AkaiApcKey25Mk2.cpp) contains "APC Key
        // 25" too, and ControllerDriverManager::load() claims the first
        // registered driver whose hint matches, so a generic hint here would
        // risk this driver claiming mk2 hardware. The WinMM szPname string
        // is NOT confirmed in this environment -- check with --list-midi on
        // real hardware and extend this list if it differs, keeping the
        // same non-collision requirement in mind.
        return {"APC Key 25 MIDI 1", "APC Key 25 IN 1"};
    }

    const char *driverName() const override { return "akai-apc-key25"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter) override {
        mEngine = &engine;
        (void)midiOut;       // no SysEx TX needed -- this device has none
        (void)allowConfigure; // nothing to configure -- fixed CCs, no writable state

        for (int i = 0; i < kKnobCount; ++i) {
            engine.setDeviceDefaultCc(kKnobCc[i], kKnobTarget[i]);
        }

        // Channel is unconfirmed for the input side (Zynthian's own gen-1
        // subclass explicitly ignores events on channel 1, "direct keybed to
        // chains", but never asserts what channel these transport buttons
        // arrive on), so this claims on any channel -- the same
        // degrade-safely default AkaiMpkMiniMk3 and AkaiMidiMix use for
        // their own pads.
        padFilter.claimNote(kNoteStopAllClips);
        padFilter.claimNote(kNotePlay);
        padFilter.claimNote(kNoteRecord);
    }

    /// The 3 claimed transport buttons, handled entirely internally via
    /// Engine& -- none of them needs GUI/UiState access, unlike the MPK
    /// driver's bottom-row pads. Acts only on the down edge, matching the
    /// MPK driver's top-row convention.
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

std::unique_ptr<ControllerDriver> makeAkaiApcKey25() {
    return std::make_unique<AkaiApcKey25>();
}

} // namespace s1::ctrldev
