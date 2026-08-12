//
//  TeenageEngineeringOp1.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Teenage Engineering OP-1 (original/OG), in its documented
//  MIDI Mode (Shift+COM -> CTRL, per Zynthian's own file header). Binds
//  almost nothing -- and that's the confirmed, correct scope, not a
//  research gap:
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
//    SS buttons, SEQ, SHIFT, MICRO, COM -- is also a plain Control Change,
//    like REC/PLAY below, but has no equivalent Synth One action worth
//    inventing, so it's left unbound.
//
//  REC (CC 38) and PLAY (CC 39) ARE claimed via CcFilter and handled in
//  onCC() -- arp<->sequencer mode / arp on-off -- the same 2 functions the
//  Novation Launchkey family claims for its own Play/Record CCs (see
//  NovationLaunchkeyMk4.cpp). Channel is unconfirmed for the input side
//  (Zynthian's own `midi_event()` extracts a channel but never checks it
//  anywhere in the function), so this claims on any channel, the same
//  degrade-safely default the rest of this port's drivers use. There is no
//  confirmed dedicated STOP CC on this device to give a third function to.
//
//  The OP-1's own keys/pads (used to play its internal engines, or send
//  MIDI notes in this mode) play as plain notes -- no special handling
//  needed, same as any other keybed in this port.
//
//  Device recognition and protocol facts are confirmed from Zynthian's
//  shipped driver (zyngine/ctrldev/zynthian_ctrldev_teenageengineering_op1.py
//  on the vangelis branch of zynthian/zynthian-ui). Which action REC/PLAY
//  perform is this driver's own design choice, not a transcribed fact.
//

#include "TeenageEngineeringOp1.h"

#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

// -- Confirmed fixed transport CCs (channel unconfirmed -- see the file
// header) --
constexpr int kCcRecord = 38;
constexpr int kCcPlay   = 39;

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
             PadFilter &padFilter, CcFilter &ccFilter) override {
        mEngine = &engine;
        (void)midiOut;
        (void)allowConfigure;
        (void)padFilter; // nothing to claim or bind here -- see the file header

        ccFilter.claimCc(kCcRecord);
        ccFilter.claimCc(kCcPlay);
    }

    /// The 2 claimed transport CCs, handled entirely internally via Engine&.
    /// Acts only when the CC value is non-zero, matching the Novation
    /// Launchkey drivers' own convention for their Play/Record CCs.
    void onCC(int channel, int cc, int value) override {
        (void)channel; // claimed with a wildcard channel -- see the file header
        if (value <= 0 || mEngine == nullptr) return;
        if (cc == kCcPlay) {
            mEngine->setParameter(
                arpIsOn, mEngine->getParameter(arpIsOn) != 0.0f ? 0.0f : 1.0f);
        } else if (cc == kCcRecord) {
            mEngine->setParameter(
                arpIsSequencer,
                mEngine->getParameter(arpIsSequencer) != 0.0f ? 0.0f : 1.0f);
        }
    }

private:
    Engine *mEngine = nullptr;
};

} // namespace

std::unique_ptr<ControllerDriver> makeTeenageEngineeringOp1() {
    return std::make_unique<TeenageEngineeringOp1>();
}

} // namespace s1::ctrldev
