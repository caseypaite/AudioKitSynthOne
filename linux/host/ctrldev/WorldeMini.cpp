//
//  WorldeMini.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the WORLDE MINI. Per Zynthian's own driver comment
//  ("Unroute channel 10 (akai MPK mini's pads)"), this is a budget clone of
//  the Akai MPK Mini's layout -- its own Zynthian integration exists purely
//  as a "mode enforcer": 8 pads on MIDI channel 10 (notes 36-43) select a
//  musical scale/key that Zynthian then masks incoming notes against, and a
//  second bank of 8 CCs (22-29, channel 1) selects a different scale set.
//  Synth One has no scale/key-masking concept at all (only the separate,
//  unrelated microtonal Tunings system -- see linux/README.md), so neither
//  bank's *purpose* has anything for this driver to replicate. What's
//  reusable is the confirmed fact that 4 of the 8 note-based pads exist,
//  unconditionally, with no SysEx or mode-entry handshake required (this
//  device's Zynthian driver never sends one) -- so, mirroring the Akai MPK
//  Mini mk3's own top-row pads, this driver claims the first 4 as function
//  pads for the closest things Synth One has to "transport."
//
//  The CC bank (22-29) is left alone entirely -- it's CC-based, and
//  `ControllerDriver` has no `onCC()`/`onControlChange()` hook (see
//  AkaiMpk249.cpp's file header for the fuller explanation, first
//  documented there). Pads 40-43 (the other half of the note-based bank)
//  are left unclaimed too -- 4 claimed pads already covers every
//  "transport" function this driver framework has established a precedent
//  for (see AkaiMpkMiniMk3.cpp's top row), and inventing a use for 4 more
//  would be exactly the kind of speculative design the developer guide's
//  "Why this exists" section cautions against. The keybed plays notes
//  normally.
//
//  Protocol facts (the 8 pad notes, their MIDI channel, and the absence of
//  any startup handshake) are transcribed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_worlde_mini_moder.py on the vangelis
//  branch of zynthian/zynthian-ui) -- a working reference implementation,
//  not a guess, but not independently validated against physical hardware
//  in this project's development environment. Which action each claimed pad
//  performs is this driver's own design choice, not a transcribed fact.
//

#include "WorldeMini.h"

#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

// -- Confirmed fixed pad notes (bank A, MIDI channel 10 i.e. 0-indexed 9) --
constexpr int kPadChannel     = 9;
constexpr int kNotePanic      = 36;
constexpr int kNoteAllNotesOff = 37;
constexpr int kNoteArpOn      = 38;
constexpr int kNoteArpSeq     = 39;

class WorldeMini : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substring (Zynthian's
        // dev_ids = ["WORLDE MINI IN 1"]). The WinMM szPname string is NOT
        // confirmed in this environment -- check with --list-midi on real
        // hardware and extend this list if it differs.
        return {"WORLDE MINI", "WORLDE Mini", "Worlde Mini"};
    }

    const char *driverName() const override { return "worlde-mini"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter) override {
        mEngine = &engine;
        (void)midiOut;       // no SysEx TX needed -- this device has none
        (void)allowConfigure; // nothing to configure -- fixed notes, no writable state

        // Channel is explicitly confirmed here (unlike most drivers in this
        // port, which default to a wildcard channel because it's
        // unconfirmed) -- Zynthian's own dispatch checks `evchan == 9`
        // before treating anything as a pad press.
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

std::unique_ptr<ControllerDriver> makeWorldeMini() {
    return std::make_unique<WorldeMini>();
}

} // namespace s1::ctrldev
