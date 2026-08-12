//
//  NovationLaunchpadMiniMk3.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Novation Launchpad Mini mk3. Identical shape and
//  reasoning to NovationLaunchpadX.cpp -- an 8x8 grid with a DAW-mode SysEx
//  handshake and 4 CC-based arrow buttons this driver deliberately doesn't
//  send/bind, for the same reasons (no onCC() hook, and entering DAW mode
//  would trade away the grid's default chromatic note-playing layout for no
//  offsetting benefit). See that file's header for the full rationale.
//
//  Device recognition is confirmed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_launchpad_mini_mk3.py on the vangelis
//  branch of zynthian/zynthian-ui).
//

#include "NovationLaunchpadMiniMk3.h"

#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

class NovationLaunchpadMiniMk3 : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substring (Zynthian's
        // dev_ids = ["Launchpad Mini MK3 IN 1"]). The WinMM szPname string
        // is NOT confirmed in this environment -- check with --list-midi on
        // real hardware and extend this list if it differs.
        return {"Launchpad Mini MK3", "Launchpad Mini Mk3", "Launchpad Mini mk3"};
    }

    const char *driverName() const override { return "novation-launchpad-mini-mk3"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter) override {
        (void)engine;
        (void)midiOut; // deliberately no DAW-mode handshake -- see the file header
        (void)allowConfigure;
        (void)padFilter; // nothing to claim -- see the file header
    }
};

} // namespace

std::unique_ptr<ControllerDriver> makeNovationLaunchpadMiniMk3() {
    return std::make_unique<NovationLaunchpadMiniMk3>();
}

} // namespace s1::ctrldev
