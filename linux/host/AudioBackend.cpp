//
//  AudioBackend.cpp
//  AudioKitSynthOne - Linux port
//

#include "AudioBackend.h"

namespace s1 {

#ifdef S1_HAVE_JACK
std::unique_ptr<AudioBackend> makeJackBackend();
#endif
#ifdef S1_HAVE_PORTAUDIO
std::unique_ptr<AudioBackend> makePortAudioBackend(const std::string &hostApi,
                                                   bool wasapiExclusive);
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

std::unique_ptr<AudioBackend> makeBackend(const std::string &name, const std::string &hostApi,
                                          bool wasapiExclusive, std::string &error) {
#ifdef S1_HAVE_JACK
    if (name == "jack") return makeJackBackend();
#endif
#ifdef S1_HAVE_PORTAUDIO
    if (name == "portaudio") return makePortAudioBackend(hostApi, wasapiExclusive);
#endif

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
