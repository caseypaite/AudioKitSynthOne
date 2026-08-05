//
//  AudioBackend.cpp
//  AudioKitSynthOne - Linux port
//

#include "AudioBackend.h"

#include <cctype>
#include <cstdlib>

namespace s1 {

#ifdef S1_HAVE_JACK
std::unique_ptr<AudioBackend> makeJackBackend();
std::vector<OutputDevice> jackOutputDevices();
#endif
#ifdef S1_HAVE_PORTAUDIO
std::unique_ptr<AudioBackend> makePortAudioBackend(const std::string &hostApi, int deviceIndex);
std::vector<OutputDevice> portAudioOutputDevices();
#endif

std::vector<std::string> availableBackends() {
    std::vector<std::string> names;
#ifdef S1_HAVE_JACK
    names.push_back("jack");
#endif
#ifdef S1_HAVE_PORTAUDIO
    names.push_back("portaudio");
#endif
    return names;
}

std::vector<OutputDevice> availableOutputDevices(const std::string &backend) {
#ifdef S1_HAVE_JACK
    if (backend == "jack") return jackOutputDevices();
#endif
#ifdef S1_HAVE_PORTAUDIO
    if (backend == "portaudio") return portAudioOutputDevices();
#endif
    (void)backend;
    return {};
}

bool resolveOutputDevice(const std::string &spec, const std::vector<OutputDevice> &devices,
                         int &out, std::string &error) {
    if (spec.empty() || spec == "auto") {
        out = kAutoDevice;
        return true;
    }

    const bool numeric = spec.find_first_not_of("0123456789") == std::string::npos;
    if (numeric) {
        const int index = std::atoi(spec.c_str());
        for (const auto &device : devices) {
            if (device.index == index) {
                out = index;
                return true;
            }
        }
        error = "no output device with index " + spec + "; try --list-devices";
        return false;
    }

    auto lower = [](std::string s) {
        for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string needle = lower(spec);

    // Substring, not equality: driver names are long and awkward to type
    // ("HD-Audio Generic: ALC257 Analog (hw:1,0)"), so "ALC257" should be
    // enough. Ambiguity is reported rather than resolved by picking the first.
    const OutputDevice *match = nullptr;
    for (const auto &device : devices) {
        if (lower(device.name).find(needle) == std::string::npos) continue;
        if (match != nullptr) {
            error = "'" + spec + "' matches more than one device; use an index from --list-devices";
            return false;
        }
        match = &device;
    }
    if (match == nullptr) {
        error = "no output device matching '" + spec + "'; try --list-devices";
        return false;
    }
    out = match->index;
    return true;
}

std::unique_ptr<AudioBackend> makeBackend(const std::string &name, const std::string &hostApi,
                                          int deviceIndex, std::string &error) {
#ifdef S1_HAVE_JACK
    if (name == "jack") return makeJackBackend();
#endif
#ifdef S1_HAVE_PORTAUDIO
    if (name == "portaudio") return makePortAudioBackend(hostApi, deviceIndex);
#endif
    (void)deviceIndex;

    error = "unknown or unavailable backend '" + name + "'; this build has:";
    const auto available = availableBackends();
    if (available.empty()) {
        error += " none";
    } else {
        for (const auto &b : available) error += " " + b;
    }
    return nullptr;
}

} // namespace s1
