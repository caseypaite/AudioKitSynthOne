//
//  NovationLaunchpadMini.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Novation Launchpad Mini (original/mk1). This device is a
//  pure 8x8 clip-launch button grid -- no knobs, no faders, no dedicated
//  transport buttons. Zynthian's own driver (the source of every fact this
//  file cites) uses the grid entirely for its zynpad/chain-launcher concept,
//  which has no equivalent in Synth One (see the developer guide's "Why
//  this exists" section), and the only non-grid controls are 8 CC buttons
//  (104-111, silkscreened Up/Down/Left/Right/Session/User1/User2/Mixer on
//  real hardware) used there for chain selection -- again no equivalent, and
//  CC-based besides, so there's no `onPadButton()`-style hook available for
//  them even if there were a use (see the developer guide's "Adding a
//  driver" step 6 and the MPK249/Launchkey drivers' file headers for the
//  same CC-vs-note distinction).
//
//  This driver is therefore intentionally minimal: it recognises the
//  device by name and does nothing else. The keybed doesn't exist on this
//  device at all (it's a pad controller, not a keyboard) -- the 8x8 grid
//  plays as plain notes, exactly like any unclaimed pad on every other
//  driver in this port.
//
//  Protocol facts (there being none worth binding) are confirmed by reading
//  Zynthian's shipped driver (zyngine/ctrldev/zynthian_ctrldev_launchpad_mini.py
//  on the vangelis branch of zynthian/zynthian-ui) in full -- this isn't an
//  absence of research, it's the actual, confirmed shape of the device's
//  useful-to-Synth-One surface.
//

#include "NovationLaunchpadMini.h"

#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

class NovationLaunchpadMini : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substring (Zynthian's
        // dev_ids = ["Launchpad Mini IN 1"]; AlsaMidi.cpp composes
        // MidiSource::name as "<client> / <port>", so matching the
        // client-name substring alone is robust to the port suffix). The
        // WinMM szPname string is NOT confirmed in this environment -- check
        // with --list-midi on real hardware and extend this list if it
        // differs. Deliberately excludes "Mini MK3"/"mk3" so this driver
        // never claims the separately-driven Launchpad Mini mk3 (see
        // NovationLaunchpadMiniMk3.cpp).
        return {"Launchpad Mini IN", "Launchpad Mini:"};
    }

    const char *driverName() const override { return "novation-launchpad-mini"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter, CcFilter &ccFilter) override {
        (void)engine;
        (void)midiOut;
        (void)allowConfigure;
        (void)padFilter; // nothing to claim -- see the file header
        (void)ccFilter;  // nothing to claim here either
    }
};

} // namespace

std::unique_ptr<ControllerDriver> makeNovationLaunchpadMini() {
    return std::make_unique<NovationLaunchpadMini>();
}

} // namespace s1::ctrldev
