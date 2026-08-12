//
//  WinMidiOut.cpp
//  AudioKitSynthOne - Windows port
//
//  MIDI output over WinMM, the write-side counterpart of WinMidi.cpp's input.
//

#include "MidiOutput.h"

#include <windows.h>
#include <mmsystem.h>

#include <string>
#include <vector>

namespace s1 {
namespace {

/// midiOutGetDevCaps gives a fixed-size name field; render it as a
/// std::string whichever of the ANSI/wide builds we are in.
std::string deviceName(const MIDIOUTCAPS &caps) {
#ifdef UNICODE
    const int need = WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1,
                                         nullptr, 0, nullptr, nullptr);
    if (need <= 1) return std::string();
    std::string out(static_cast<size_t>(need - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, out.data(), need, nullptr, nullptr);
    return out;
#else
    return std::string(caps.szPname);
#endif
}

std::string errorText(MMRESULT rc) {
    char buf[MAXERRORLENGTH] = {0};
    if (midiOutGetErrorTextA(rc, buf, sizeof(buf)) == MMSYSERR_NOERROR) {
        return std::string(buf);
    }
    return "MMRESULT " + std::to_string(static_cast<unsigned>(rc));
}

} // namespace

MidiOutput::~MidiOutput() {
    for (void *handle : mHandles) {
        midiOutClose(static_cast<HMIDIOUT>(handle));
    }
    mHandles.clear();
}

bool MidiOutput::open(const std::string &clientName, std::string &error) {
    // WinMM has no client/port registration -- an application does not
    // publish an output other software can connect to, it only opens
    // existing devices. Nothing to do until connect() names one.
    (void)clientName;
    (void)error;
    mPortName = "WinMM";
    return true;
}

bool MidiOutput::connect(const std::string &spec, std::string &error) {
    if (spec.empty()) return true;

    const std::vector<MidiSource> dests = listDestinations();
    if (dests.empty()) {
        error = "no MIDI output devices found";
        return false;
    }

    std::vector<unsigned> wanted;
    if (spec == "all") {
        for (const auto &dest : dests) wanted.push_back(static_cast<unsigned>(dest.client));
    } else {
        // Accept a bare device index, and "N:0" too, so a command line copied
        // from the Linux build keeps working.
        std::string index = spec;
        const size_t colon = index.find(':');
        if (colon != std::string::npos) index = index.substr(0, colon);

        try {
            const int n = std::stoi(index);
            if (n < 0 || n >= static_cast<int>(dests.size())) {
                error = "no MIDI output device " + index + " (see --list-midi)";
                return false;
            }
            wanted.push_back(static_cast<unsigned>(n));
        } catch (...) {
            error = "malformed MIDI device '" + spec + "' (want a device index, or 'all')";
            return false;
        }
    }

    std::vector<std::string> opened;
    for (unsigned device : wanted) {
        HMIDIOUT handle = nullptr;
        const MMRESULT rc = midiOutOpen(&handle, device, 0, 0, CALLBACK_NULL);
        if (rc != MMSYSERR_NOERROR) {
            // With "all", a device another application already owns is
            // normal; skip it rather than failing the whole connection.
            if (spec == "all") continue;
            error = "cannot open MIDI output device " + std::to_string(device) + ": " +
                    errorText(rc);
            return false;
        }
        mHandles.push_back(handle);
        for (const auto &dest : dests) {
            if (static_cast<unsigned>(dest.client) == device) opened.push_back(dest.name);
        }
    }

    if (mHandles.empty()) {
        error = "no connectable MIDI destinations found";
        return false;
    }

    mPortName = opened.size() == 1 ? opened.front()
                                   : std::to_string(mHandles.size()) + " devices";
    mConnected = true;
    return true;
}

std::vector<MidiSource> MidiOutput::listDestinations() {
    std::vector<MidiSource> dests;
    const UINT count = midiOutGetNumDevs();
    for (UINT i = 0; i < count; ++i) {
        MIDIOUTCAPS caps{};
        if (midiOutGetDevCaps(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;

        MidiSource dest;
        dest.id = std::to_string(i);
        dest.name = deviceName(caps);
        dest.client = static_cast<int>(i);
        dest.port = 0;
        dests.push_back(dest);
    }
    return dests;
}

namespace {

void sendShortMsg(std::vector<void *> &handles, int status, int note, int velocity) {
    const DWORD packed = static_cast<DWORD>(status & 0xFF) |
                         (static_cast<DWORD>(note & 0x7F) << 8) |
                         (static_cast<DWORD>(velocity & 0x7F) << 16);
    for (void *handle : handles) midiOutShortMsg(static_cast<HMIDIOUT>(handle), packed);
}

} // namespace

void MidiOutput::noteOn(int channel, int note, int velocity) {
    sendShortMsg(mHandles, 0x90 | (channel & 0x0F), note, velocity);
}

void MidiOutput::noteOff(int channel, int note, int velocity) {
    sendShortMsg(mHandles, 0x80 | (channel & 0x0F), note, velocity);
}

void MidiOutput::sendSysEx(const uint8_t *data, size_t length) {
    if (data == nullptr || length == 0) return;

    for (void *rawHandle : mHandles) {
        auto handle = static_cast<HMIDIOUT>(rawHandle);

        MIDIHDR hdr{};
        // Stable storage for the duration of the prepare/send/unprepare
        // cycle below -- WinMM keeps a pointer into this, so it must outlive
        // midiOutLongMsg, not just this function's call to it.
        std::vector<uint8_t> buffer(data, data + length);
        hdr.lpData = reinterpret_cast<LPSTR>(buffer.data());
        hdr.dwBufferLength = hdr.dwBytesRecorded = static_cast<DWORD>(length);

        if (midiOutPrepareHeader(handle, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR) continue;
        if (midiOutLongMsg(handle, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR) {
            midiOutUnprepareHeader(handle, &hdr, sizeof(hdr));
            continue;
        }

        // This output was opened with CALLBACK_NULL, so there is no MOM_DONE
        // notification to wait on -- polling midiOutUnprepareHeader until it
        // stops returning MIDIERR_STILLPLAYING is the standard callback-free
        // idiom for a synchronous sysex send. Bounded so a wedged device
        // can't hang the control thread (this must never be called from the
        // render callback).
        for (int waitedMs = 0; waitedMs < 2000; waitedMs += 5) {
            if (midiOutUnprepareHeader(handle, &hdr, sizeof(hdr)) != MIDIERR_STILLPLAYING) break;
            Sleep(5);
        }
    }
}

} // namespace s1
