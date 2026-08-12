//
//  NovationLaunchkeyMiniMk4.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Novation Launchkey Mini mk4 (37-key). Sends the same
//  "session mode" Note On handshake as the rest of this port's Launchkey
//  drivers (channel 15, note 12, velocity 127 -- see
//  NovationLaunchkeyMiniMk3.cpp's file header) so the pads/buttons route
//  correctly, but deliberately does NOT bind this device's 8 knobs (CC
//  85-92) to anything.
//
//  Why, when every other Launchkey driver in this port binds its knob row:
//  Zynthian's own driver explicitly puts these encoders into what it calls
//  "Transport mode (relative mode)" via a startup CC
//  (`dev_send_ccontrol_change(idev_out, 6, 30, 5)`), and its own consumption
//  of the resulting values confirms they're relative deltas, not absolute
//  positions -- `delta = -1 if ccval < 64 else 1 if ccval > 64 else 0`. This
//  driver never sends that mode-select CC (there's no confirmed absolute
//  mode to select instead, and guessing at one risks silently binding a
//  parameter to whatever the device's true default happens to be), but that
//  doesn't establish the knobs are absolute by default either -- there's
//  simply no confirmed evidence in the source this driver was built from
//  that these particular knobs ever send an absolute position in any mode.
//  `Engine::handleMidi()`'s CC path (see Engine.cpp) always interprets an
//  incoming CC value as an absolute 0-127 position; feeding it a relative
//  delta would make a small nudge snap the bound parameter towards its
//  minimum, not adjust it smoothly. Better left unbound -- MIDI Learn is
//  unaffected -- than shipped with that failure mode. Contrast with the
//  (non-Mini) Launchkey MK4 37, whose knobs are confirmed absolute and are
//  bound (see NovationLaunchkeyMk4.cpp).
//
//  The keybed and pads play as plain notes -- no function pads for this
//  device either. Its solo/mute pads (96-119, per Zynthian's own per-chain
//  mixer-strip use of them) are a per-mixer-channel concept with no Synth
//  One equivalent (see the developer guide's "Why this exists" section),
//  and its transport (Play/Record), bank-switch and navigation buttons are
//  all CC-based with no `onCC()` hook available to react to them (same
//  reasoning as the Akai MPK249's transport CCs -- see AkaiMpk249.cpp's file
//  header).
//
//  Protocol facts below are transcribed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_launchkey_mini_mk4_37.py on the
//  vangelis branch of zynthian/zynthian-ui) -- a working reference
//  implementation, not a guess, but not independently validated against
//  physical hardware in this project's development environment.
//

#include "NovationLaunchkeyMiniMk4.h"

#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

class NovationLaunchkeyMiniMk4_37 : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substring (Zynthian's
        // dev_ids = ["Launchkey Mini MK4 37 IN 2"]). The WinMM szPname
        // string is NOT confirmed in this environment -- check with
        // --list-midi on real hardware and extend this list if it differs.
        return {"Launchkey Mini MK4 37", "Launchkey Mini Mk4 37", "Launchkey Mini mk4 37"};
    }

    const char *driverName() const override { return "novation-launchkey-mini-mk4-37"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter) override {
        (void)engine;
        (void)allowConfigure; // nothing to configure
        (void)padFilter;      // no function pads for this device -- see the file header

        if (midiOut != nullptr && midiOut->isConnected()) {
            midiOut->noteOn(15, 12, 127); // enter session mode
        }
        // Deliberately no knob CC bindings -- see the file header.
    }
};

} // namespace

std::unique_ptr<ControllerDriver> makeNovationLaunchkeyMiniMk4_37() {
    return std::make_unique<NovationLaunchkeyMiniMk4_37>();
}

} // namespace s1::ctrldev
