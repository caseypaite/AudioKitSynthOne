//
//  NovationLaunchpadX.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Novation Launchpad X. Like the Launchpad Mini (mk1), this
//  device is a pure 8x8 clip-launch button grid with no knobs or faders --
//  see NovationLaunchpadMini.cpp for the general "no equivalent in Synth
//  One" reasoning that leaves grid-launcher devices this thin.
//
//  Unlike the Mini, this device DOES support a "DAW session mode" SysEx
//  handshake (confirmed from Zynthian's shipped driver -- see below), and 4
//  of its non-grid buttons send CC-based arrow presses Zynthian maps to
//  navigation. This driver deliberately sends neither the handshake nor
//  binds the arrows:
//  - The arrow CCs have no ControllerDriver hook to act on (no onCC(), same
//    reasoning as the Akai MPK249's transport CCs -- see AkaiMpk249.cpp's
//    file header), so there's nothing to gain from claiming them.
//  - Entering DAW/session mode has a real *downside* here with no offsetting
//    benefit: per Zynthian's own end()/exit path, doing so switches the grid
//    away from the device's other selectable layout ("Keys" -- a chromatic
//    note-playing mode), which is the behavior Synth One actually wants
//    (the grid playing as ordinary, musically useful notes, same as any
//    unclaimed pad elsewhere in this port). Unlike the Akai APC40 mk2 or
//    Launchpad Pro mk2 drivers, nothing here is claimed that would justify
//    that tradeoff, so this driver leaves the device in whatever layout it
//    powers on into.
//
//  Device recognition is confirmed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_launchpad_x.py on the vangelis branch
//  of zynthian/zynthian-ui).
//

#include "NovationLaunchpadX.h"

#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

class NovationLaunchpadX : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substring (Zynthian's
        // dev_ids = ["Launchpad X IN 1"]). The WinMM szPname string is NOT
        // confirmed in this environment -- check with --list-midi on real
        // hardware and extend this list if it differs.
        return {"Launchpad X"};
    }

    const char *driverName() const override { return "novation-launchpad-x"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter, CcFilter &ccFilter) override {
        (void)engine;
        (void)midiOut; // deliberately no DAW-mode handshake -- see the file header
        (void)allowConfigure;
        (void)padFilter; // nothing to claim -- see the file header
        (void)ccFilter;  // nothing to claim here either
    }
};

} // namespace

std::unique_ptr<ControllerDriver> makeNovationLaunchpadX() {
    return std::make_unique<NovationLaunchpadX>();
}

} // namespace s1::ctrldev
