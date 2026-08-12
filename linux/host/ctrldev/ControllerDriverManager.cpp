//
//  ControllerDriverManager.cpp
//  AudioKitSynthOne - Linux / Windows port
//

#include "ControllerDriverManager.h"

#include <cctype>

#include "AkaiMpkMiniMk3.h"

namespace s1::ctrldev {

namespace {

struct Entry {
    const char *name;
    std::unique_ptr<ControllerDriver> (*make)();
};

// Compiled-in driver registry -- a small static table, not a plugin system,
// mirroring AudioBackend.cpp's factory-table style. Add a new driver here.
const Entry kDrivers[] = {
    {"akai-mpk-mini-mk3", makeAkaiMpkMiniMk3},
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
                                   std::string &status) {
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
        loaded->driver->init(engine, outPtr, allowConfigure, padFilter);
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
    for (auto &loaded : mLoaded) {
        if (loaded->inputSourceId == msg.sourceId) {
            loaded->driver->onProgramChange(msg.program);
            return;
        }
    }
    for (auto &loaded : mLoaded) loaded->driver->onProgramChange(msg.program);
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

} // namespace s1::ctrldev
