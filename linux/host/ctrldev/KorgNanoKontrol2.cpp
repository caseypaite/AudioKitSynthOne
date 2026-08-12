//
//  KorgNanoKontrol2.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Korg nanoKONTROL2. This device has no keybed and no
//  pads -- every one of its controls (8 faders, 8 knobs, 8 each of
//  SOLO/MUTE/REC buttons, transport, track/marker navigation) is a fixed,
//  class-compliant MIDI CC, confirmed absolute by how Zynthian's own driver
//  consumes the values (`ccval / 127` fed straight into level/balance
//  setters). init() seeds Engine's device-default CC map directly from a
//  compile-time table -- see the developer guide's "Adding a driver" step
//  4 -- with no SysEx at all: the only SysEx this device speaks is an
//  "enable external LED control" handshake (a Scene Data Dump
//  request/modify/write-back round trip), which exists purely to let a host
//  drive the device's own LEDs -- this framework has no LED-feedback
//  concept (see the developer guide), so there is nothing here for that
//  handshake to unlock, and it's never sent.
//
//  Every one of this device's buttons (SOLO/MUTE/REC x8, transport Play/
//  Stop/Record/Rewind/Fast-Forward/Cycle, track left/right, marker set/
//  left/right) is also a plain Control Change, not a Note -- Zynthian's own
//  `midi_event()` has no note-on/off branch at all for this device, so
//  `PadFilter`/`onPadButton()` (Note-only) can't reach any of them. Play
//  (CC 41), Stop (CC 42) and Record (CC 45) ARE claimed via CcFilter and
//  handled in onCC() -- panic / arp on-off / arp<->sequencer mode, the same
//  3-function transport mapping this port's Note-based transport buttons use
//  elsewhere (e.g. the Akai APC drivers). Channel is unconfirmed for the
//  input side (Zynthian's own `midi_event()` never extracts or checks a
//  channel for any CC on this device), so this claims on any channel, the
//  same degrade-safely default the rest of this port's drivers use. The
//  SOLO/MUTE/REC x8 and navigation buttons remain unbound -- see below for
//  why.
//
//  Protocol facts below are transcribed from Zynthian's shipped driver
//  (zyngine/ctrldev/zynthian_ctrldev_korg_nanokontrol2.py on the vangelis
//  branch of zynthian/zynthian-ui) -- a working reference implementation,
//  not a guess, but not independently validated against physical hardware
//  in this project's development environment. Which target parameter each
//  knob/fader maps to is this driver's own design choice, not a transcribed
//  fact.
//
//  One deliberate exception: fader 2 sits at CC1, which is also the
//  universal MIDI mod wheel convention. This isn't a guess (the device
//  really does send CC1 there), but binding it anyway would mean a user's
//  mod wheel -- on a *different*, simultaneously-connected keyboard --
//  would silently drive whatever this driver bound to fader 2, the same
//  "never hijack CC1/CC64" hazard the developer guide warns about, just
//  arrived at via a confirmed fact instead of a guess. Fader 2 is left
//  unbound; the other 7 faders and all 8 knobs are unaffected.
//
//  The 24 SOLO/MUTE/REC buttons and the track/marker navigation buttons stay
//  unbound even though onCC() could technically reach them now: Zynthian's
//  own driver treats them as per-mixer-channel solo/mute/record-arm state (7
//  channel strips + master), the same Zynthian chain-mixer concept with no
//  Synth One equivalent that leaves the Akai MPK249's 24 switch CCs unbound
//  too (see AkaiMpk249.cpp's file header) -- there's no honest single-
//  parameter target for "solo channel 4," so nothing is invented.
//

#include "KorgNanoKontrol2.h"

#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

constexpr int kStripCount = 8;

// -- Confirmed fixed CC assignments (device's only mode -- no factory
// preset or session-mode dependency, unlike the Akai MPK249 or the
// Novation Launchkey family) --
constexpr int kFaderCc[kStripCount] = {0, 1, 2, 3, 4, 5, 6, 7};
constexpr int kKnobCc[kStripCount]  = {16, 17, 18, 19, 20, 21, 22, 23};

/// Faders: reuses the Akai MPK Mini mk3 driver's Mode 0 "sound" target set
/// (see AkaiMpkMiniMk3.cpp) -- this device has no separate physical master
/// fader (Zynthian's own driver only repurposes fader 8 as "master" while a
/// SHIFT CC is held, a stateful distinction this framework's flat CC table
/// has no way to represent), so masterVolume is used directly here rather
/// than substituted out the way AkaiMidiMix.cpp's fader row does.
constexpr S1Parameter kFaderTarget[kStripCount] = {
    cutoff, resonance, attackDuration, releaseDuration,
    lfo1Rate, reverbMix, delayMix, masterVolume};

/// Knobs: reuses the MPK driver's Mode 1 "oscillators/voice" target set.
constexpr S1Parameter kKnobTarget[kStripCount] = {
    morph1Volume, morph2Volume, morph2Detuning, subVolume,
    fmAmount, noiseVolume, glide, morphBalance};

// -- Confirmed fixed transport CCs (channel unconfirmed -- see the file
// header) --
constexpr int kCcPlay   = 41;
constexpr int kCcStop   = 42;
constexpr int kCcRecord = 45;

class KorgNanoKontrol2 : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // The confirmed ALSA client-name substring (Zynthian's
        // dev_ids = ["nanoKONTROL2 IN 1"]). The WinMM szPname string is NOT
        // confirmed in this environment -- check with --list-midi on real
        // hardware and extend this list if it differs.
        return {"nanoKONTROL2", "nanoKontrol2", "NANOKONTROL2"};
    }

    const char *driverName() const override { return "korg-nanokontrol2"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter, CcFilter &ccFilter) override {
        mEngine = &engine;
        (void)midiOut;        // no SysEx TX needed -- this device has none
                              // worth speaking, see the file header
        (void)allowConfigure; // nothing to configure -- fixed CCs, no writable state
        (void)padFilter;      // no function pads for this device -- see the file header

        for (int i = 0; i < kStripCount; ++i) {
            // Fader 2 (CC1) is deliberately skipped -- see the file header.
            if (kFaderCc[i] != 1) {
                engine.setDeviceDefaultCc(kFaderCc[i], kFaderTarget[i]);
            }
            engine.setDeviceDefaultCc(kKnobCc[i], kKnobTarget[i]);
        }

        ccFilter.claimCc(kCcPlay);
        ccFilter.claimCc(kCcStop);
        ccFilter.claimCc(kCcRecord);
    }

    /// The 3 claimed transport CCs, handled entirely internally via Engine&
    /// -- mirrors the Akai APC drivers' Note-based transport buttons, just
    /// reached via onCC() instead of onPadButton(). Acts only when the CC
    /// value is non-zero; Zynthian's own driver never reads these as input
    /// beyond that (they're mostly used there for LED feedback, an output
    /// concern this framework has no equivalent for).
    void onCC(int channel, int cc, int value) override {
        (void)channel; // claimed with a wildcard channel -- see the file header
        if (value <= 0 || mEngine == nullptr) return;
        if (cc == kCcStop) {
            mEngine->panic();
        } else if (cc == kCcPlay) {
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

std::unique_ptr<ControllerDriver> makeKorgNanoKontrol2() {
    return std::make_unique<KorgNanoKontrol2>();
}

} // namespace s1::ctrldev
