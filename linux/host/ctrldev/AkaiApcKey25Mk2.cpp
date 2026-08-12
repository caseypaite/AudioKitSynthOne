//
//  AkaiApcKey25Mk2.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Akai APC Key 25 mk2. Zynthian's own mk2 driver subclasses
//  from the mk1 one below it in the same source tree in the *other*
//  direction from what this port does -- here, AkaiApcKey25.cpp (gen 1) and
//  this file are independent, side-by-side drivers, because
//  zynthian_ctrldev_akai_apc_key25.py (gen 1) actually imports its note/CC
//  constants FROM zynthian_ctrldev_akai_apc_key25_mk2.py: both hardware
//  generations share the exact same fixed knob-CC and transport-button-note
//  assignments in that source, confirmed by the gen-1 file never overriding
//  any of them. So this driver's knob/transport handling below is
//  deliberately identical in shape to AkaiApcKey25.cpp's -- see that file
//  for the fuller design rationale (no SysEx, no modes, fixed CCs, "why only
//  3 buttons are claimed"). The only real differences between the two
//  generations Zynthian's driver treats specially are LED color model (mk2
//  has full per-pad RGB; gen 1 has a simpler brightness+color scheme) and
//  channel handling gen 1 adds for direct-keybed routing -- neither is
//  relevant here, since this framework has no LED-feedback concept at all
//  (see the developer guide) and Synth One's keybed already just plays notes
//  regardless of channel.
//
//  Protocol facts below (fixed knob CCs, transport button note numbers, and
//  the confirmed ALSA name strings used to tell this device apart from the
//  separately-driven gen-1 APC Key 25 -- see AkaiApcKey25.cpp) are
//  transcribed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_akai_apc_key25_mk2.py on the vangelis
//  branch of zynthian/zynthian-ui) -- a working reference implementation,
//  not a guess, but not independently validated against physical hardware in
//  this project's development environment. Which target parameter each fixed
//  CC/note maps to is this driver's own design choice, not a transcribed
//  fact -- see the parameter table and the 3 claimed buttons below.
//

#include "AkaiApcKey25Mk2.h"

#include <cstdint>
#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

constexpr int kKnobCount = 8;

// -- Confirmed fixed CC assignments (device's only mode; KNOB_1..KNOB_8 in
// Zynthian's source, 0x30-0x37 -- identical to the gen-1 device) --
constexpr int kKnobCc[kKnobCount] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37};

// -- Confirmed fixed transport-button note numbers (Zynthian's
// BTN_STOP_ALL_CLIPS/BTN_PLAY/BTN_RECORD -- identical to the gen-1 device) --
constexpr int kNoteStopAllClips = 0x51; // 81
constexpr int kNotePlay         = 0x5B; // 91
constexpr int kNoteRecord       = 0x5D; // 93

/// Same target set as AkaiApcKey25.cpp's Mode 0-equivalent knob row -- see
/// that file for the rationale (reuses the Akai MPK Mini mk3 driver's Mode 0
/// "sound" set). Kept identical between the two device generations since
/// they're the same 8 physical knobs on the same fixed CCs.
constexpr S1Parameter kKnobTarget[kKnobCount] = {
    cutoff, resonance, attackDuration, releaseDuration,
    lfo1Rate, reverbMix, delayMix, masterVolume};

class AkaiApcKey25Mk2 : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed mk2-specific strings -- distinct from the gen-1
        // device's "APC Key 25 MIDI 1"/"APC Key 25 IN 1" (see
        // AkaiApcKey25.cpp) by the "mk2" token, so the two drivers never
        // contend for the same hardware. The WinMM szPname string is NOT
        // confirmed in this environment -- check with --list-midi on real
        // hardware and extend this list if it differs.
        return {"APC Key 25 mk2 MIDI 2", "APC Key 25 mk2 IN 2"};
    }

    const char *driverName() const override { return "akai-apc-key25-mk2"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter) override {
        mEngine = &engine;
        (void)midiOut;       // no SysEx TX needed -- this device has none
        (void)allowConfigure; // nothing to configure -- fixed CCs, no writable state

        for (int i = 0; i < kKnobCount; ++i) {
            engine.setDeviceDefaultCc(kKnobCc[i], kKnobTarget[i]);
        }

        // Channel is unconfirmed for the input side -- see AkaiApcKey25.cpp
        // for the same caveat -- so this claims on any channel, the same
        // degrade-safely default the rest of this port's drivers use.
        padFilter.claimNote(kNoteStopAllClips);
        padFilter.claimNote(kNotePlay);
        padFilter.claimNote(kNoteRecord);
    }

    /// Identical shape to AkaiApcKey25::onPadButton() -- see that file.
    PadReport onPadButton(int channel, int note, bool isDown) override {
        (void)channel; // claimed with a wildcard channel -- see init()
        if (isDown && mEngine != nullptr) {
            if (note == kNoteStopAllClips) {
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

std::unique_ptr<ControllerDriver> makeAkaiApcKey25Mk2() {
    return std::make_unique<AkaiApcKey25Mk2>();
}

} // namespace s1::ctrldev
