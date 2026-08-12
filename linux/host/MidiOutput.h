//
//  MidiOutput.h
//  AudioKitSynthOne - Linux / Windows port
//
//  MIDI output: the actually-sounding notes (post-arp, post-sequencer) sent to
//  an external device or another application, driven off the same
//  S1Protocol::playingNotesDidChange notification the GUI uses for keyboard
//  highlighting (see MidiNoteForwarder in host/main.cpp). That notification
//  already runs on the control thread -- drained from the render thread's
//  message ring once per poll, not delivered from the render thread itself --
//  so sending here is a plain synchronous call, no queue or worker thread
//  needed the way MidiInput's realtime handoff does.
//
//  One class, two implementations: ALSA sequencer on Linux (AlsaMidiOut.cpp)
//  and WinMM on Windows (WinMidiOut.cpp).
//

#pragma once

#include "MidiInput.h" // reuses MidiSource

#include <string>
#include <vector>

namespace s1 {

class MidiOutput {
public:
    ~MidiOutput();

    /// Create the output. `clientName` is the name this synth advertises to
    /// other applications; WinMM has no such concept and ignores it.
    bool open(const std::string &clientName, std::string &error);

    /// Connect to the destination named by `spec` ("CLIENT:PORT" on Linux, a
    /// device index on Windows), or every available destination when `spec`
    /// is "all". Empty `spec` connects to nothing, leaving the port available
    /// for manual connection (Linux only -- WinMM has no such notion).
    bool connect(const std::string &spec, std::string &error);

    /// True once connect() has bound at least one destination. False after a
    /// no-op connect() (empty spec) or before connect() is called at all --
    /// useful for a caller (e.g. a controller driver) that needs to know
    /// whether it actually has somewhere to send before trying.
    bool isConnected() const { return mConnected; }

    void noteOn(int channel, int note, int velocity);
    void noteOff(int channel, int note, int velocity);

    /// Send a complete SysEx message. `data` must start with 0xF0 and end
    /// with 0xF7. Synchronous -- returns once the platform confirms the
    /// bytes are sent (Windows) or handed to the kernel (Linux). Control-
    /// thread only; never call from the render callback.
    void sendSysEx(const uint8_t *data, size_t length);

    /// Enumerate destinations that accept MIDI input on the system.
    static std::vector<MidiSource> listDestinations();

    std::string portName() const { return mPortName; }

private:
    std::string mPortName;
    bool        mConnected = false;

#ifdef _WIN32
    // HMIDIOUT, kept as void* so this header stays free of <windows.h>.
    std::vector<void *> mHandles;
#else
    void *mSeq = nullptr; // snd_seq_t*
    int   mPort = -1;
#endif
};

} // namespace s1
