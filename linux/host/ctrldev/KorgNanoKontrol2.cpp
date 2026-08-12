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
//  `midi_event()` has no note-on/off branch at all for this device. Unlike
//  every Note-based transport button this port claims elsewhere (the Akai
//  APC/MIDI Mix drivers, Arturia KeyLab 61, Novation Launchpad Pro mk2),
//  `PadFilter`/`onPadButton()` only ever intercepts Notes -- there is no
//  `onCC()`/`onControlChange()` hook in `ControllerDriver` at all (see
//  AkaiMpk249.cpp's file header, which first documents this limitation), so
//  none of this device's buttons can be turned into a function pad. They're
//  left unbound rather than half-implemented.
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
             PadFilter &padFilter) override {
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
    }
};

} // namespace

std::unique_ptr<ControllerDriver> makeKorgNanoKontrol2() {
    return std::make_unique<KorgNanoKontrol2>();
}

} // namespace s1::ctrldev
