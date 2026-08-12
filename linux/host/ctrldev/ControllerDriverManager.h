//
//  ControllerDriverManager.h
//  AudioKitSynthOne - Linux / Windows port
//
//  Matches connected MIDI input ports against the compiled-in controller
//  drivers by name (Zynthian-style dev_ids matching, scaled down: a small
//  static table, not a plugin system), loads whichever one(s) claim a
//  connected device, and routes reassembled SysEx, Program Change and
//  function-pad-button messages to them.
//
//  No hotplug: matching happens once at startup against whatever MidiInput
//  already connected, mirroring this codebase's existing MIDI port handling
//  (ports are enumerated and subscribed once, never rescanned).
//

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ControllerDriver.h"
#include "MidiInput.h"  // MidiSource
#include "MidiOutput.h"
#include "MidiSysEx.h"
#include "PadFilter.h"
#include "CcFilter.h"

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
    /// loaded, or why nothing did) for the caller to print. `allowConfigure`,
    /// `padFilter` and `ccFilter` are forwarded to the driver's init() --
    /// see ControllerDriver::init(). Both filters are owned by the caller
    /// and must outlive both this manager and the MidiInput they were also
    /// given to (see PadFilter.h/CcFilter.h) -- they're passed empty here
    /// and populated asynchronously once the driver's own discovery
    /// completes, the same timing Engine::mDeviceDefaultCc already relies
    /// on.
    bool load(const std::string &wantedName, Engine &engine,
             const std::vector<MidiSource> &connectedInputs, bool allowConfigure,
             PadFilter &padFilter, CcFilter &ccFilter, std::string &status);

    /// Routes a fully-reassembled SysEx message (from SysExQueue) to
    /// whichever loaded driver's input it matches, by SysExMessage::sourceId
    /// against the client id (ALSA) / device index (WinMM) recorded at
    /// load(). If sourceId doesn't match any loaded driver -- e.g. it wasn't
    /// captured, or there's a startup race -- offers the message to every
    /// loaded driver rather than dropping it silently.
    void dispatchSysEx(const SysExMessage &msg);

    /// Routes a Program Change message (from ProgramChangeQueue) the same
    /// way dispatchSysEx routes a SysEx message: by sourceId first, offered
    /// to every loaded driver if unmatched.
    void dispatchProgramChange(const ProgramChangeMessage &msg);

    /// Routes a diverted pad-button message (from PadButtonQueue) the same
    /// way, by sourceId. Calls the matching driver's onPadButton(); if that
    /// returns a reported PadReport (a bottom-row/GUI concern the driver
    /// can't act on itself), forwards it to the registered observer, if any.
    void dispatchPadButton(const PadButtonMessage &msg);

    /// Routes a claimed CC message (from CcQueue) the same way the other
    /// dispatch*() methods do, by sourceId, offered to every loaded driver
    /// if unmatched. Calls the matching driver's onCC() -- see CcFilter.h
    /// for why, unlike dispatchPadButton(), there is no suppression/
    /// observer-forwarding step here: onCC() either acts directly via its
    /// own Engine&, or does nothing.
    void dispatchCc(const CcMessage &msg);

    /// Registered by a GUI host to react to a bottom-row pad's panel-select
    /// report -- never invoked for a top-row pad, since a driver handles
    /// those internally via its own Engine& and never sets
    /// PadReport::reported for them (a convention followed by
    /// AkaiMpkMiniMk3, not enforced here). Unset (the default) means
    /// nothing is listening -- the headless CLI host never registers one,
    /// since it has no panels. One slot, not a list: exactly one GUI.
    using PadButtonObserver = std::function<void(PadRow row, int index, bool isDown)>;
    void setPadButtonObserver(PadButtonObserver observer) { mPadButtonObserver = std::move(observer); }

    /// Registered by a host to react to a driver's mode changing (see
    /// ControllerDriver::modeStatusText()) -- called with that driver's own
    /// modeStatusText() right after dispatchProgramChange() invokes
    /// onProgramChange(), only when the text is non-empty. Both hosts in
    /// this port register one: the GUI turns it into a status-line toast,
    /// the headless CLI prints it. One slot, not a list, matching
    /// PadButtonObserver above.
    using ModeChangeObserver = std::function<void(const std::string &statusText)>;
    void setModeChangeObserver(ModeChangeObserver observer) { mModeChangeObserver = std::move(observer); }

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
    PadButtonObserver                    mPadButtonObserver;
    ModeChangeObserver                   mModeChangeObserver;
};

} // namespace s1::ctrldev
