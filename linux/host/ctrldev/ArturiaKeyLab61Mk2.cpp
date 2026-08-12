//
//  ArturiaKeyLab61Mk2.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Arturia KeyLab mkII 61. Unlike every Akai driver in this
//  port, this device needs a one-time SysEx handshake at startup before its
//  transport/track/global control buttons send anything meaningful at all --
//  Arturia calls this "DAW mode" (as opposed to the device's standalone/
//  Analog Lab mode). init() sends the same two fixed messages Zynthian's own
//  driver sends for this (see below) whenever an output port is available;
//  there is no query/reply to wait on, so this is unconditional the same way
//  the Akai MPK Mini mk3 driver's startup SysEx query is -- not gated behind
//  --controller-driver-configure, since entering DAW mode doesn't write or
//  overwrite anything stored on the device (unlike that driver's
//  writeProgram(), which does).
//
//  Only 4 of the device's dedicated global/transport buttons are claimed as
//  function pads -- STOP, PLAY/PAUSE, RECORD and METRONOME -- mirroring the
//  Akai MPK Mini mk3 driver's 4 top-row functions (panic / all notes off /
//  arp on-off / arp<->sequencer mode) and the Akai APC drivers' 3-button
//  transport claims elsewhere in this port. Deliberately NOT bound:
//  - The 8 SELECT_1-8 buttons and the track SOLO/MUTE/RECORD_ARM/READ/WRITE
//    buttons: per-mixer-channel controls in Zynthian's reference driver, the
//    same "no per-channel-strip concept in Synth One" reasoning that leaves
//    the Akai APC/MIDI Mix drivers' equivalent buttons unclaimed (see the
//    developer guide's "Why this exists" section).
//  - The 4x4 pad grid (notes 36-51): plays as plain notes, same as every
//    other driver's keybed/pad grid -- Synth One has no zynpad/pattern
//    concept for Zynthian's own repurposing of these to control.
//  - The 8 knobs, 8 faders and 8 toggle buttons in the device's mixing
//    strip (per Arturia's own manual, cited below): Zynthian's driver
//    itself never gives these a CC or note mapping -- there's no confirmed
//    protocol fact for them in the source this driver was built from, so
//    nothing is bound rather than guessed. They're available for MIDI Learn
//    like any unmapped control.
//  - SAVE, PUNCH IN/OUT, UNDO, NEXT/PREVIOUS, BANK, the preset +/- buttons,
//    and the pitch/mod wheels: no equivalent Synth One concept for any of
//    them either.
//
//  Protocol facts below (the DAW-mode-entry SysEx messages and the 4 claimed
//  buttons' note numbers) are transcribed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_arturia_keylab_61_mk2.py on the
//  vangelis branch of zynthian/zynthian-ui), itself citing Arturia's own
//  KeyLab mk2 manual and a third-party Bitwig extension's button-ID table as
//  sources -- a working reference implementation, not a guess, but not
//  independently validated against physical hardware in this project's
//  development environment. Which action each claimed button performs is
//  this driver's own design choice, not a transcribed fact.
//

#include "ArturiaKeyLab61Mk2.h"

#include <cstdint>
#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

// -- Confirmed DAW-mode-entry SysEx messages (sent once at startup) --
constexpr uint8_t kEnterDawPresetLive[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42,
                                          0x02, 0x00, 0x40, 0x52, 0x02, 0xF7};
constexpr uint8_t kEnterDawMode[]       = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42,
                                          0x05, 0x02, 0xF7};

// -- Confirmed fixed note numbers for the 4 claimed global/transport
// buttons (channel unconfirmed -- Zynthian's own dispatch for these never
// filters by channel, unlike its pad handling, which explicitly requires
// channel 9) --
constexpr int kNoteStop        = 93;
constexpr int kNotePlayPause   = 94;
constexpr int kNoteRecord      = 95;
constexpr int kNoteMetronome   = 89;

class ArturiaKeyLab61Mk2 : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substring (Zynthian's
        // dev_ids = ["KeyLab mkII 61 IN 2"]; AlsaMidi.cpp composes
        // MidiSource::name as "<client> / <port>", so matching the
        // client-name substring alone is robust to the port suffix). The
        // WinMM szPname string is NOT confirmed in this environment -- check
        // with --list-midi on real hardware and extend this list if it
        // differs.
        return {"KeyLab mkII 61", "KeyLab mk2 61", "KeyLab MkII 61"};
    }

    const char *driverName() const override { return "arturia-keylab-61-mk2"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter, CcFilter &ccFilter) override {
        mEngine = &engine;
        (void)allowConfigure; // entering DAW mode doesn't write persistent
                              // device state -- see the file header
        (void)ccFilter;       // this device's claimed transport buttons are
                              // Note-based (see padFilter below), not CC

        if (midiOut != nullptr && midiOut->isConnected()) {
            midiOut->sendSysEx(kEnterDawPresetLive, sizeof(kEnterDawPresetLive));
            midiOut->sendSysEx(kEnterDawMode, sizeof(kEnterDawMode));
        }

        // Channel is unconfirmed for these 4 -- see the note-number comment
        // above -- so this claims on any channel, the same degrade-safely
        // default the rest of this port's drivers use.
        padFilter.claimNote(kNoteStop);
        padFilter.claimNote(kNotePlayPause);
        padFilter.claimNote(kNoteRecord);
        padFilter.claimNote(kNoteMetronome);
    }

    /// The 4 claimed buttons, handled entirely internally via Engine& --
    /// none of them needs GUI/UiState access. Acts only on the down edge,
    /// matching the Akai drivers' convention.
    PadReport onPadButton(int channel, int note, bool isDown) override {
        (void)channel; // claimed with a wildcard channel -- see init()
        if (isDown && mEngine != nullptr) {
            if (note == kNoteStop) {
                mEngine->panic();
            } else if (note == kNoteMetronome) {
                mEngine->allNotesOff();
            } else if (note == kNotePlayPause) {
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

std::unique_ptr<ControllerDriver> makeArturiaKeyLab61Mk2() {
    return std::make_unique<ArturiaKeyLab61Mk2>();
}

} // namespace s1::ctrldev
