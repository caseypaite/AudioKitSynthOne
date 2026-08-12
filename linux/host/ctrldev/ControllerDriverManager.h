//
//  ControllerDriverManager.h
//  AudioKitSynthOne - Linux / Windows port
//
//  Matches connected MIDI input ports against the compiled-in controller
//  drivers by name (Zynthian-style dev_ids matching, scaled down: a small
//  static table, not a plugin system), loads whichever one(s) claim a
//  connected device, and routes reassembled SysEx messages to them.
//
//  No hotplug: matching happens once at startup against whatever MidiInput
//  already connected, mirroring this codebase's existing MIDI port handling
//  (ports are enumerated and subscribed once, never rescanned).
//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ControllerDriver.h"
#include "MidiInput.h"  // MidiSource
#include "MidiOutput.h"
#include "MidiSysEx.h"

namespace s1 { class Engine; }

namespace s1::ctrldev {

class ControllerDriverManager {
public:
    /// Names of every driver compiled into this binary, for
    /// --list-controller-drivers.
    static std::vector<std::string> availableDriverNames();

    /// wantedName: "auto" (match by connected device name), "off" (do
    /// nothing, returns false), or an exact driver name (force-load if that
    /// driver's device is connected; fails if it isn't -- no forcing a
    /// driver onto hardware it doesn't recognise).
    /// `connectedInputs` should be MidiInput::connectedSources(). Opens a
    /// private MidiOutput per loaded driver for TX, independent of any
    /// --midi-out the caller may also have configured, so the two don't
    /// fight over one MidiOutput's connection state.
    /// Fills `status` with a human-readable summary either way (what
    /// loaded, or why nothing did) for the caller to print. `allowConfigure`
    /// is forwarded to the driver's init() -- see ControllerDriver::init().
    bool load(const std::string &wantedName, Engine &engine,
             const std::vector<MidiSource> &connectedInputs, bool allowConfigure,
             std::string &status);

    /// Routes a fully-reassembled SysEx message (from SysExQueue) to
    /// whichever loaded driver's input it matches, by SysExMessage::sourceId
    /// against the client id (ALSA) / device index (WinMM) recorded at
    /// load(). If sourceId doesn't match any loaded driver -- e.g. it wasn't
    /// captured, or there's a startup race -- offers the message to every
    /// loaded driver rather than dropping it silently.
    void dispatchSysEx(const SysExMessage &msg);

    bool loaded() const { return !mLoaded.empty(); }

private:
    struct Loaded {
        std::unique_ptr<ControllerDriver> driver;
        MidiOutput                        out; // private, dedicated to this driver's TX
        int                                inputSourceId = -1;
    };
    // unique_ptr<Loaded>, not Loaded, so a vector reallocation as more
    // drivers load never moves/copies a live MidiOutput (which owns a raw
    // ALSA seq handle / WinMM handles with no defined copy semantics).
    std::vector<std::unique_ptr<Loaded>> mLoaded;
};

} // namespace s1::ctrldev
