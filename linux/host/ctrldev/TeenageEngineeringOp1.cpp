//
//  TeenageEngineeringOp1.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Teenage Engineering OP-1 (original/OG), in its documented
//  MIDI Mode (Shift+COM -> CTRL, per Zynthian's own file header). This
//  driver is intentionally a recognition-only stub -- device identification
//  with no functional bindings -- and that's the confirmed, correct scope,
//  not a research gap:
//
//  - The 4 encoders (CC 1-4) are confirmed *relative*, the same
//    two's-complement-style delta Zynthian's own driver decodes
//    (`ccval if ccval < 64 else ccval - 128`). `Engine::handleMidi()`'s CC
//    path always treats an incoming value as an absolute 0-127 position
//    (see Engine.cpp), so binding a relative CC would make a small turn
//    snap the target parameter towards its minimum instead of adjusting it
//    smoothly -- the same reasoning that leaves the Novation Launchkey Mini
//    mk4 37's knobs unbound (see NovationLaunchkeyMiniMk4.cpp's file
//    header).
//  - Every other control on this device -- the 4 encoder push-buttons, HELP,
//    METRONOME, the 4 MODE buttons, T1-T4, both arrow pairs, SCISSOR, all 8
//    SS buttons, SEQ, REC/PLAY/STOP, SHIFT, MICRO, COM -- is also a plain
//    Control Change; Zynthian's own `midi_event()` has no note-on/off branch
//    at all for this device. `ControllerDriver` has no `onCC()`/
//    `onControlChange()` hook (see AkaiMpk249.cpp's file header, which
//    first documents this limitation), so none of them can be turned into a
//    function pad the way this port's Note-based transport buttons are
//    (Akai APC, Arturia KeyLab 61, Novation Launchpad Pro mk2, WORLDE MINI).
//
//  The OP-1's own keys/pads (used to play its internal engines, or send
//  MIDI notes in this mode) play as plain notes -- no special handling
//  needed, same as any other keybed in this port.
//
//  Device recognition is confirmed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_teenageengineering_op1.py on the
//  vangelis branch of zynthian/zynthian-ui).
//

#include "TeenageEngineeringOp1.h"

#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

class TeenageEngineeringOp1 : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substring (Zynthian's
        // dev_ids = ["OP-1 IN 1"]). The WinMM szPname string is NOT
        // confirmed in this environment -- check with --list-midi on real
        // hardware and extend this list if it differs.
        return {"OP-1"};
    }

    const char *driverName() const override { return "teenage-engineering-op1"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter) override {
        (void)engine;
        (void)midiOut;
        (void)allowConfigure;
        (void)padFilter; // nothing to claim or bind -- see the file header
    }
};

} // namespace

std::unique_ptr<ControllerDriver> makeTeenageEngineeringOp1() {
    return std::make_unique<TeenageEngineeringOp1>();
}

} // namespace s1::ctrldev
