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

namespace s1 {

class Engine;
class MidiOutput;

namespace ctrldev {

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
    /// than firing it unconditionally at every startup.
    virtual void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure) = 0;

    /// A complete SysEx message addressed to this device's input arrived.
    /// Default: ignore.
    virtual void onSysEx(const uint8_t *data, size_t length) { (void)data; (void)length; }
};

} // namespace ctrldev
} // namespace s1
