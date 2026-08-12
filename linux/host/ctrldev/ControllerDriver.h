//
//  ControllerDriver.h
//  AudioKitSynthOne - Linux / Windows port
//
//  Per-device MIDI controller driver, in the spirit of Zynthian's
//  zyngine/ctrldev drivers -- but scoped down to what Synth One actually has:
//  no chains, mixer strips, or pad-launcher grid, just synth parameters, MIDI
//  Learn, and ordinary note input. A driver's job here is narrow: recognise a
//  connected device by name, optionally talk SysEx to it, and seed
//  Engine's device-scoped CC defaults (Engine::setDeviceDefaultCc) so the
//  device is useful out of the box -- the user's own MIDI Learn always wins
//  over whatever a driver seeds.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "PadFilter.h"
#include "CcFilter.h"

namespace s1 {

class Engine;
class MidiOutput;

namespace ctrldev {

/// Which physical row a claimed function pad belongs to, reported by a
/// driver's onPadButton() -- never a raw note number, so device detail
/// (which note, which SysEx table index) stays inside the driver.
enum class PadRow { Bottom, Top };

/// What onPadButton() hands back when a pad press means something outside
/// this driver's own reach -- currently: bottom-row panel selection, a
/// GUI-only concern this driver (and host/ctrldev/ generally) has no
/// business knowing about. `reported` false (the default) means "handled
/// internally, nothing more to do" -- e.g. a top-row transport pad, which a
/// driver acts on directly through the Engine& given to init().
struct PadReport {
    bool   reported = false;
    PadRow row = PadRow::Bottom;
    int    index = 0; // 0-3
};

class ControllerDriver {
public:
    virtual ~ControllerDriver() = default;

    /// Case-insensitive substrings matched against MidiSource::name to claim
    /// a connected input port. A device is claimed if its name contains any
    /// one of these.
    virtual std::vector<std::string> deviceNameHints() const = 0;

    /// Short identifier used by --controller-driver NAME and
    /// --list-controller-drivers.
    virtual const char *driverName() const = 0;

    /// Called once at startup after ControllerDriverManager has matched a
    /// connected device and located (or failed to locate) a same-device MIDI
    /// output. `midiOut` is nullptr when no output could be paired -- init()
    /// must degrade gracefully (skip anything needing SysEx TX, still work
    /// as a plain input). `allowConfigure` reflects --controller-driver-configure
    /// (default off): permission to write configuration to the device, not
    /// just read from it -- a driver whose write support is unverified
    /// against real hardware should gate any such write behind this rather
    /// than firing it unconditionally at every startup. `padFilter` is
    /// where a driver claims (channel, note) pairs it wants to repurpose as
    /// function pads (see PadFilter.h) -- claiming is what actually stops
    /// them from playing notes, by diverting them before the MIDI reader
    /// thread ever pushes them onto the note/CC queue. `ccFilter` is where
    /// a driver claims (channel, CC) pairs it wants delivered to onCC() (see
    /// CcFilter.h) -- unlike padFilter, claiming a CC does not suppress it;
    /// it still reaches Engine::handleMidi() exactly as before.
    virtual void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
                      PadFilter &padFilter, CcFilter &ccFilter) = 0;

    /// A complete SysEx message addressed to this device's input arrived.
    /// Default: ignore.
    virtual void onSysEx(const uint8_t *data, size_t length) { (void)data; (void)length; }

    /// A Program Change message addressed to this device's input arrived --
    /// the hook for a hardware-native "mode"/bank switch (e.g. the Akai MPK
    /// Mini mk3's PROG SELECT button choosing a different onboard program).
    /// A driver that supports modes typically responds by clearing and
    /// re-seeding Engine's device-default CC map for the new mode's targets
    /// (see Engine::clearDeviceDefaults/setDeviceDefaultCc). Default: ignore.
    virtual void onProgramChange(int program) { (void)program; }

    /// A note-on/off arrived on (channel, note) that this driver had
    /// claimed as a function pad via `padFilter` in init() -- already
    /// diverted before it could reach Engine::handleMidi. A driver resolves
    /// the raw note back to a semantic pad itself (only it knows its own
    /// note table) and handles what it can internally (e.g. a top-row
    /// transport pad, via its own Engine&), returning an unreported
    /// PadReport. For anything it can't act on itself (e.g. a bottom-row
    /// panel-select pad -- this driver has no GUI/UiState access), it
    /// returns a reported PadReport for ControllerDriverManager to forward
    /// to whatever observer the host registered. Default: ignore.
    virtual PadReport onPadButton(int channel, int note, bool isDown) {
        (void)channel; (void)note; (void)isDown;
        return {};
    }

    /// A CC event arrived on (channel, cc) that this driver had claimed via
    /// `ccFilter` in init() -- delivered in addition to the ordinary
    /// MidiQueue path, not instead of it (see CcFilter.h for why no
    /// suppression is needed here, unlike onPadButton()). The hook for
    /// devices whose transport/mode buttons are Control Change rather than
    /// Note -- most controllers with CC-based buttons have no other way for
    /// a driver to react to a press at all. `value` is the raw 0-127 CC
    /// value; a momentary button typically sends a non-zero value on press
    /// and 0 (or nothing) on release, but that convention is device-specific
    /// -- check it, don't assume it. Default: ignore.
    virtual void onCC(int channel, int cc, int value) {
        (void)channel; (void)cc; (void)value;
    }
};

} // namespace ctrldev
} // namespace s1
