//
//  ControllerDriverManager.cpp
//  AudioKitSynthOne - Linux / Windows port
//

#include "ControllerDriverManager.h"

#include <cctype>

#include "AkaiMpkMiniMk3.h"
#include "AkaiMidiMix.h"
#include "AkaiApcKey25.h"
#include "AkaiApcKey25Mk2.h"
#include "AkaiApc40Mk2.h"
#include "AkaiMpk249.h"
#include "ArturiaKeyLab61Mk2.h"
#include "NovationLaunchkeyMiniMk3.h"
#include "NovationLaunchkeyMiniMk4.h"
#include "NovationLaunchkeyMk3.h"
#include "NovationLaunchkeyMk4.h"
#include "NovationLaunchpadMini.h"
#include "NovationLaunchpadMiniMk3.h"
#include "NovationLaunchpadProMk2.h"
#include "NovationLaunchpadProMk3.h"
#include "NovationLaunchpadX.h"
#include "KorgNanoKontrol2.h"
#include "BehringerMotor.h"
#include "WorldeMini.h"
#include "TeenageEngineeringOp1.h"

namespace s1::ctrldev {

namespace {

struct Entry {
    const char *name;
    std::unique_ptr<ControllerDriver> (*make)();
};

// Compiled-in driver registry -- a small static table, not a plugin system,
// mirroring AudioBackend.cpp's factory-table style. Add a new driver here.
// Order matters for "auto" matching: AkaiApcKey25's hint list is deliberately
// non-overlapping with AkaiApcKey25Mk2's (see AkaiApcKey25.cpp), so either
// order is actually safe between those two, but a driver whose hints could
// ever be a substring of another's should be listed after the more specific
// one regardless. Same reasoning applies to every other mk1/mk2/mk3/mk4
// pair in the Novation entries below -- each one's deviceNameHints() is
// deliberately non-overlapping with its siblings' (see the individual
// driver files).
const Entry kDrivers[] = {
    {"akai-mpk-mini-mk3", makeAkaiMpkMiniMk3},
    {"akai-midimix", makeAkaiMidiMix},
    {"akai-apc-key25", makeAkaiApcKey25},
    {"akai-apc-key25-mk2", makeAkaiApcKey25Mk2},
    {"akai-apc40-mk2", makeAkaiApc40Mk2},
    {"akai-mpk249", makeAkaiMpk249},
    {"arturia-keylab-61-mk2", makeArturiaKeyLab61Mk2},
    {"novation-launchkey-mini-mk3", makeNovationLaunchkeyMiniMk3},
    {"novation-launchkey-mini-mk4-37", makeNovationLaunchkeyMiniMk4_37},
    {"novation-launchkey-mk3-88", makeNovationLaunchkeyMk3_88},
    {"novation-launchkey-mk4-37", makeNovationLaunchkeyMk4_37},
    {"novation-launchpad-mini", makeNovationLaunchpadMini},
    {"novation-launchpad-mini-mk3", makeNovationLaunchpadMiniMk3},
    {"novation-launchpad-pro-mk2", makeNovationLaunchpadProMk2},
    {"novation-launchpad-pro-mk3", makeNovationLaunchpadProMk3},
    {"novation-launchpad-x", makeNovationLaunchpadX},
    {"korg-nanokontrol2", makeKorgNanoKontrol2},
    {"behringer-motor", makeBehringerMotor},
    {"worlde-mini", makeWorldeMini},
    {"teenage-engineering-op1", makeTeenageEngineeringOp1},
};

std::string toLower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

std::vector<std::string> ControllerDriverManager::availableDriverNames() {
    std::vector<std::string> names;
    for (const auto &entry : kDrivers) names.push_back(entry.name);
    return names;
}

bool ControllerDriverManager::load(const std::string &wantedName, Engine &engine,
                                   const std::vector<MidiSource> &connectedInputs,
                                   bool allowConfigure, PadFilter &padFilter,
                                   CcFilter &ccFilter, std::string &status) {
    if (wantedName == "off") {
        status = "off";
        return false;
    }

    const std::vector<MidiSource> outputs = MidiOutput::listDestinations();

    for (const auto &entry : kDrivers) {
        if (wantedName != "auto" && wantedName != entry.name) continue;

        std::unique_ptr<ControllerDriver> driver = entry.make();
        std::vector<std::string> hints = driver->deviceNameHints();
        for (auto &hint : hints) hint = toLower(hint);

        const MidiSource *matchedInput = nullptr;
        for (const auto &source : connectedInputs) {
            const std::string name = toLower(source.name);
            for (const auto &hint : hints) {
                if (name.find(hint) != std::string::npos) {
                    matchedInput = &source;
                    break;
                }
            }
            if (matchedInput != nullptr) break;
        }
        if (matchedInput == nullptr) continue; // this driver's device isn't connected

        auto loaded = std::make_unique<Loaded>();
        loaded->inputSourceId = matchedInput->client;

        // Find the same physical device's output port: prefer the same
        // client id (how one USB MIDI interface's paired in/out ALSA ports
        // are identified), falling back to an identical name (the only
        // signal available on WinMM, where input/output device indices are
        // independent enumerations).
        const MidiSource *matchedOutput = nullptr;
        for (const auto &out : outputs) {
            if (out.client == matchedInput->client) {
                matchedOutput = &out;
                break;
            }
        }
        if (matchedOutput == nullptr) {
            for (const auto &out : outputs) {
                if (out.name == matchedInput->name) {
                    matchedOutput = &out;
                    break;
                }
            }
        }

        MidiOutput *outPtr = nullptr;
        if (matchedOutput != nullptr) {
            std::string openError;
            if (loaded->out.open("SynthOne", openError) &&
                loaded->out.connect(matchedOutput->id, openError) && loaded->out.isConnected()) {
                outPtr = &loaded->out;
            }
        }

        status = std::string(driver->driverName()) + " <- " + matchedInput->name;
        if (outPtr == nullptr) status += " (SysEx TX unavailable)";

        loaded->driver = std::move(driver);
        loaded->driver->init(engine, outPtr, allowConfigure, padFilter, ccFilter);
        mLoaded.push_back(std::move(loaded));
        return true;
    }

    status = (wantedName == "auto") ? "none matched"
                                    : ("'" + wantedName + "' requested but not connected");
    return false;
}

void ControllerDriverManager::dispatchSysEx(const SysExMessage &msg) {
    for (auto &loaded : mLoaded) {
        if (loaded->inputSourceId == msg.sourceId) {
            loaded->driver->onSysEx(msg.data, msg.length);
            return;
        }
    }
    // Unmatched sourceId (e.g. not captured, or a startup race) -- offer to
    // every loaded driver rather than dropping it silently.
    for (auto &loaded : mLoaded) loaded->driver->onSysEx(msg.data, msg.length);
}

void ControllerDriverManager::dispatchProgramChange(const ProgramChangeMessage &msg) {
    auto handle = [&](Loaded &loaded) {
        loaded.driver->onProgramChange(msg.program);
        const std::string status = loaded.driver->modeStatusText();
        if (!status.empty() && mModeChangeObserver) mModeChangeObserver(status);
    };
    for (auto &loaded : mLoaded) {
        if (loaded->inputSourceId == msg.sourceId) {
            handle(*loaded);
            return;
        }
    }
    for (auto &loaded : mLoaded) handle(*loaded);
}

void ControllerDriverManager::dispatchPadButton(const PadButtonMessage &msg) {
    auto handle = [&](Loaded &loaded) {
        const PadReport report = loaded.driver->onPadButton(msg.channel, msg.note, msg.isNoteOn);
        if (report.reported && mPadButtonObserver) {
            mPadButtonObserver(report.row, report.index, msg.isNoteOn);
        }
    };
    for (auto &loaded : mLoaded) {
        if (loaded->inputSourceId == msg.sourceId) {
            handle(*loaded);
            return;
        }
    }
    for (auto &loaded : mLoaded) handle(*loaded);
}

void ControllerDriverManager::dispatchCc(const CcMessage &msg) {
    for (auto &loaded : mLoaded) {
        if (loaded->inputSourceId == msg.sourceId) {
            loaded->driver->onCC(msg.channel, msg.cc, msg.value);
            return;
        }
    }
    for (auto &loaded : mLoaded) loaded->driver->onCC(msg.channel, msg.cc, msg.value);
}

} // namespace s1::ctrldev
