//
//  WinMidi.cpp
//  AudioKitSynthOne - Windows port
//
//  MIDI input over WinMM, the counterpart of AlsaMidi.cpp.
//
//  WinMM runs the callback on its own thread, so unlike the ALSA backend there
//  is no reader thread here: the callback pushes straight onto the queue the
//  audio thread drains. That callback is documented as being unable to call
//  back into WinMM and must not block, which a lock-free ring satisfies exactly.
//

#include "MidiInput.h"

#include <windows.h>
#include <mmsystem.h>

#include <string>
#include <vector>

namespace s1 {
namespace {

/// midiInGetDevCaps gives a fixed-size name field; render it as a std::string
/// whichever of the ANSI/wide builds we are in.
std::string deviceName(const MIDIINCAPS &caps) {
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
    if (midiInGetErrorTextA(rc, buf, sizeof(buf)) == MMSYSERR_NOERROR) {
        return std::string(buf);
    }
    return "MMRESULT " + std::to_string(static_cast<unsigned>(rc));
}

void CALLBACK midiCallback(HMIDIIN, UINT message, DWORD_PTR instance,
                           DWORD_PTR param1, DWORD_PTR) {
    if (message != MIM_DATA) return; // MIM_LONGDATA is sysex; the kernel ignores it
    auto *self = reinterpret_cast<MidiInput *>(instance);
    if (self != nullptr) self->enqueuePacked(static_cast<uint32_t>(param1));
}

} // namespace

MidiInput::~MidiInput() {
    stop();
    for (void *handle : mHandles) {
        auto h = static_cast<HMIDIIN>(handle);
        midiInStop(h);
        midiInClose(h);
    }
    mHandles.clear();
}

void MidiInput::enqueuePacked(uint32_t packed) {
    // dwParam1 packs the short message low byte first: status, data1, data2.
    const uint8_t status = static_cast<uint8_t>(packed & 0xFF);

    // Channel voice messages only, matching what the ALSA backend forwards.
    // System messages (0xF0 and up) carry no channel and the kernel ignores them.
    if (status < 0x80 || status >= 0xF0) return;

    MidiMessage m;
    m.data[0] = status;
    m.data[1] = static_cast<uint8_t>((packed >> 8) & 0x7F);
    m.data[2] = static_cast<uint8_t>((packed >> 16) & 0x7F);

    // Program change and channel pressure are two bytes on the wire; the rest
    // are three. Passing the right length keeps handleMidi's `length == 3`
    // tests honest.
    const uint8_t kind = static_cast<uint8_t>(status & 0xF0);
    m.length = (kind == 0xC0 || kind == 0xD0) ? 2 : 3;

    if (mQueue != nullptr) mQueue->push(m);
}

bool MidiInput::open(const std::string &clientName, std::string &error) {
    // WinMM has no client/port registration -- an application does not publish
    // an input other software can connect to, it only opens existing devices.
    // Nothing to do until connect() names one.
    (void)clientName;
    (void)error;
    mPortName = "WinMM";
    return true;
}

bool MidiInput::connect(const std::string &spec, std::string &error) {
    if (spec.empty()) return true;

    const std::vector<MidiSource> sources = listSources();
    if (sources.empty()) {
        error = "no MIDI input devices found";
        return false;
    }

    std::vector<unsigned> wanted;
    if (spec == "all") {
        for (const auto &source : sources) wanted.push_back(static_cast<unsigned>(source.client));
    } else {
        // Accept a bare device index, and "N:0" too, so a command line copied
        // from the Linux build keeps working.
        std::string index = spec;
        const size_t colon = index.find(':');
        if (colon != std::string::npos) index = index.substr(0, colon);

        try {
            const int n = std::stoi(index);
            if (n < 0 || n >= static_cast<int>(sources.size())) {
                error = "no MIDI device " + index + " (see --list-midi)";
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
        HMIDIIN handle = nullptr;
        const MMRESULT rc = midiInOpen(&handle, device,
                                       reinterpret_cast<DWORD_PTR>(&midiCallback),
                                       reinterpret_cast<DWORD_PTR>(this),
                                       CALLBACK_FUNCTION);
        if (rc != MMSYSERR_NOERROR) {
            // With "all", a device another application already owns is normal;
            // skip it rather than failing the whole connection.
            if (spec == "all") continue;
            error = "cannot open MIDI device " + std::to_string(device) + ": " + errorText(rc);
            return false;
        }
        mHandles.push_back(handle);
        for (const auto &source : sources) {
            if (static_cast<unsigned>(source.client) == device) opened.push_back(source.name);
        }
    }

    if (mHandles.empty()) {
        error = "no connectable MIDI sources found";
        return false;
    }

    mPortName = opened.size() == 1 ? opened.front()
                                   : std::to_string(mHandles.size()) + " devices";
    return true;
}

std::vector<MidiSource> MidiInput::listSources() {
    std::vector<MidiSource> sources;
    const UINT count = midiInGetNumDevs();
    for (UINT i = 0; i < count; ++i) {
        MIDIINCAPS caps{};
        if (midiInGetDevCaps(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;

        MidiSource source;
        source.id = std::to_string(i);
        source.name = deviceName(caps);
        source.client = static_cast<int>(i);
        source.port = 0;
        sources.push_back(source);
    }
    return sources;
}

void MidiInput::start(MidiQueue *queue) {
    if (mHandles.empty() || mRunning.load()) return;

    // Publish the queue before the callbacks can fire.
    mQueue = queue;
    mRunning.store(true);
    for (void *handle : mHandles) {
        midiInStart(static_cast<HMIDIIN>(handle));
    }
}

void MidiInput::stop() {
    if (!mRunning.exchange(false)) return;
    for (void *handle : mHandles) {
        midiInStop(static_cast<HMIDIIN>(handle));
    }
    // midiInStop is documented to complete pending callbacks before returning,
    // so the queue pointer is unused past this point.
    mQueue = nullptr;
}

} // namespace s1
