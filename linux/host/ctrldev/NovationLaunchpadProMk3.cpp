//
//  NovationLaunchpadProMk3.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Novation Launchpad Pro mk3. Same shape and reasoning as
//  NovationLaunchpadX.cpp and NovationLaunchpadMiniMk3.cpp -- an 8x8 grid
//  with a DAW-mode SysEx handshake and 4 CC-based arrow buttons this driver
//  deliberately doesn't send/bind. Unlike the Launchpad Pro mk2 (see
//  NovationLaunchpadProMk2.cpp), this device's Zynthian source has no
//  confirmed note-based Play/Stop/Record-style buttons to claim as function
//  pads -- only the same CC-based arrows every other thin Launchpad driver
//  in this port already can't act on (no onCC() hook -- see the developer
//  guide) -- so there's nothing here that would justify the DAW-mode
//  handshake's tradeoff (losing the grid's default chromatic note-playing
//  layout). See NovationLaunchpadX.cpp's header for the fuller rationale.
//
//  Device recognition is confirmed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_launchpad_pro_mk3.py on the vangelis
//  branch of zynthian/zynthian-ui).
//

#include "NovationLaunchpadProMk3.h"

#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

class NovationLaunchpadProMk3 : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substring (Zynthian's
        // dev_ids = ["Launchpad Pro MK3 IN 3"]). The WinMM szPname string is
        // NOT confirmed in this environment -- check with --list-midi on
        // real hardware and extend this list if it differs. Deliberately
        // excludes "mk2" so this driver never claims the separately-driven
        // Launchpad Pro mk2 (see NovationLaunchpadProMk2.cpp).
        return {"Launchpad Pro MK3", "Launchpad Pro Mk3", "Launchpad Pro mk3"};
    }

    const char *driverName() const override { return "novation-launchpad-pro-mk3"; }

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

std::unique_ptr<ControllerDriver> makeNovationLaunchpadProMk3() {
    return std::make_unique<NovationLaunchpadProMk3>();
}

} // namespace s1::ctrldev
