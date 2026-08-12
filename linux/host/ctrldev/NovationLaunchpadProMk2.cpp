//
//  NovationLaunchpadProMk2.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Novation Launchpad Pro mk2. Like the other Launchpad
//  drivers in this port, the 8x8 clip-launch grid plays as plain notes --
//  Synth One has no clip/scene concept for it to control (see the developer
//  guide's "Why this exists" section). Unlike the rest of the Launchpad
//  family (see NovationLaunchpadX.cpp for why they're left alone entirely),
//  this device has 3 confirmed, dedicated (non-grid) Record/Stop/Play
//  buttons that Zynthian's own driver uses for real transport actions, so
//  this driver claims them as function pads -- STOP/PLAY/RECORD mapped to
//  panic/arp on-off/arp<->sequencer mode, mirroring every other Akai/Novation
//  transport-button claim elsewhere in this port.
//
//  Getting those 3 buttons to behave the way Zynthian's own driver relies on
//  requires the same "DAW session mode" SysEx handshake the other Launchpad
//  drivers skip -- here it's sent, since (unlike those) there's a confirmed
//  payoff (the 3 buttons) that justifies losing the grid's alternate
//  "Note/Drum" chromatic layout while in this mode.
//
//  Protocol facts below (the handshake, and the 3 buttons' note numbers) are
//  transcribed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_launchpad_pro_mk2.py on the vangelis
//  branch of zynthian/zynthian-ui) -- a working reference implementation,
//  not a guess, but not independently validated against physical hardware
//  in this project's development environment. Which action each claimed
//  button performs is this driver's own design choice, not a transcribed
//  fact.
//

#include "NovationLaunchpadProMk2.h"

#include <cstdint>
#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

// -- Confirmed DAW-mode-entry SysEx messages (sent once at startup),
// envelope F0 00 20 29 02 10 <data> F7 -- wake, enter Ableton/DAW mode,
// select session layout --
constexpr uint8_t kWake[]           = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x10, 0x09, 0x01, 0xF7};
constexpr uint8_t kEnterDawMode[]   = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x10, 0x21, 0x00, 0xF7};
constexpr uint8_t kSelectSession[]  = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x10, 0x22, 0x00, 0xF7};

// -- Confirmed fixed note numbers for the 3 claimed transport buttons
// (channel unconfirmed for the input side; Zynthian's own dispatch for
// these routes through a mode-scoped handler that doesn't filter by
// channel) --
constexpr int kNoteRecord = 65; // 0x41
constexpr int kNoteStop   = 66; // 0x42
constexpr int kNotePlay   = 67; // 0x43

class NovationLaunchpadProMk2 : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substring (Zynthian's
        // dev_ids = ["Launchpad Pro IN 1"]). The WinMM szPname string is NOT
        // confirmed in this environment -- check with --list-midi on real
        // hardware and extend this list if it differs. Deliberately
        // excludes "mk3" so this driver never claims the separately-driven
        // Launchpad Pro mk3 (see NovationLaunchpadProMk3.cpp).
        return {"Launchpad Pro IN", "Launchpad Pro:"};
    }

    const char *driverName() const override { return "novation-launchpad-pro-mk2"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter, CcFilter &ccFilter) override {
        mEngine = &engine;
        (void)allowConfigure; // entering DAW mode doesn't write persistent
                              // device state -- same reasoning as the
                              // Arturia KeyLab 61 mk2 driver
        (void)ccFilter;       // this device's claimed transport buttons are
                              // Note-based (see padFilter below), not CC

        if (midiOut != nullptr && midiOut->isConnected()) {
            midiOut->sendSysEx(kWake, sizeof(kWake));
            midiOut->sendSysEx(kEnterDawMode, sizeof(kEnterDawMode));
            midiOut->sendSysEx(kSelectSession, sizeof(kSelectSession));
        }

        padFilter.claimNote(kNoteRecord);
        padFilter.claimNote(kNoteStop);
        padFilter.claimNote(kNotePlay);
    }

    /// The 3 claimed buttons, handled entirely internally via Engine& --
    /// none of them needs GUI/UiState access. Acts only on the down edge,
    /// matching this port's other transport-button drivers.
    PadReport onPadButton(int channel, int note, bool isDown) override {
        (void)channel; // claimed with a wildcard channel -- see init()
        if (isDown && mEngine != nullptr) {
            if (note == kNoteStop) {
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

std::unique_ptr<ControllerDriver> makeNovationLaunchpadProMk2() {
    return std::make_unique<NovationLaunchpadProMk2>();
}

} // namespace s1::ctrldev
