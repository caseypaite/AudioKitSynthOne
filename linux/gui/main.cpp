//
//  main.cpp
//  AudioKitSynthOne - Linux port, graphical front end
//
//  GLFW + OpenGL3 + Dear ImGui shell around the same Engine the headless host
//  uses. Audio runs on the backend's realtime thread; the UI thread only reads
//  and writes parameters, exactly as the iOS app's main thread does.
//

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include "MidiInput.h"
#include "MidiOutput.h"
#include "AudioBackend.h"
#include "Engine.h"
#include "Json.h"
#include "Panels.h"
#include "Widgets.h"
#include "ctrldev/ControllerDriverManager.h"

#include <algorithm>

namespace {

/// Mirrors DSP notifications into the UI state and, when `midiOut` is set,
/// forwards the same notification to MIDI out -- see StatusObserver in
/// host/main.cpp, which this matches.
class UiObserver : public S1Protocol {
public:
    explicit UiObserver(s1gui::UiState &ui) : mUi(ui) {}

    void heldNotesDidChange(HeldNotes notes) override {
        std::memcpy(mUi.heldNotes, notes.heldNotes, sizeof(mUi.heldNotes));
    }
    void arpBeatCounterDidChange(S1ArpBeatCounter counter) override {
        mUi.arpBeat = counter.beatCounter;
        mUi.heldNoteCount = counter.heldNotesCount;
    }
    void playingNotesDidChange(PlayingNotes notes) override {
        int voices = 0;
        for (int i = 0; i < notes.polyphony; ++i) {
            if (notes.playingNotes[i].noteNumber >= 0) ++voices;
        }
        mUi.voiceCount = voices;

        if (midiOut != nullptr) forwardToMidiOut(notes);
    }

    s1::MidiOutput *midiOut = nullptr;

private:
    void forwardToMidiOut(const PlayingNotes &notes) {
        for (int i = 0; i < S1_MAX_POLYPHONY; ++i) {
            const bool sounding = i < notes.polyphony && notes.playingNotes[i].noteNumber >= 0;
            const int newNote = sounding
                ? std::clamp(notes.playingNotes[i].noteNumber + notes.playingNotes[i].transpose,
                             0, 127)
                : -1;
            if (mLastNote[i] == newNote) continue;

            if (mLastNote[i] >= 0) midiOut->noteOff(0, mLastNote[i], 0);
            if (newNote >= 0) {
                const int velocity = std::clamp(notes.playingNotes[i].velocity, 1, 127);
                midiOut->noteOn(0, newNote, velocity);
            }
            mLastNote[i] = newNote;
        }
    }

    s1gui::UiState &mUi;
    int mLastNote[S1_MAX_POLYPHONY] = {-1, -1, -1, -1, -1, -1};
};

/// Chrome density. The official Raspberry Pi 7" panel is 800x480, which is
/// less than a third of the pixels the roomy layout was drawn for, so on a
/// small display every spare pixel is taken out of padding and spacing.
///
/// Frame padding is the exception: it sets how tall a button is, and a control
/// you cannot hit with a fingertip is worse than one you have to scroll to. It
/// goes *up* in compact mode, not down.
void applySpacing(bool compact) {
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowPadding = compact ? ImVec2(6, 5)  : ImVec2(12, 10);
    style.FramePadding  = compact ? ImVec2(6, 7)  : ImVec2(8, 5);
    style.ItemSpacing   = compact ? ImVec2(6, 3)  : ImVec2(8, 7);
    style.ScrollbarSize = compact ? 16.0f : 12.0f;  // a touch-draggable bar
}

void applyDarkStyle() {
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowPadding = ImVec2(12, 10);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 7);
    style.ScrollbarSize = 12.0f;

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_WindowBg]        = ImColor(s1gui::color::kBackground).Value;
    colors[ImGuiCol_ChildBg]         = ImColor(s1gui::color::kPanel).Value;
    colors[ImGuiCol_PopupBg]         = ImColor(s1gui::color::kPanel).Value;
    colors[ImGuiCol_Border]          = ImColor(s1gui::color::kPanelEdge).Value;
    colors[ImGuiCol_FrameBg]         = ImColor(s1gui::color::kKnobBody).Value;
    colors[ImGuiCol_FrameBgHovered]  = ImColor(s1gui::color::kKnobEdge).Value;
    colors[ImGuiCol_FrameBgActive]   = ImColor(s1gui::color::kAccentDim).Value;
    colors[ImGuiCol_Button]          = ImColor(s1gui::color::kKnobBody).Value;
    colors[ImGuiCol_ButtonHovered]   = ImColor(s1gui::color::kKnobEdge).Value;
    colors[ImGuiCol_ButtonActive]    = ImColor(s1gui::color::kAccentDim).Value;
    colors[ImGuiCol_Header]          = ImColor(s1gui::color::kAccentDim).Value;
    colors[ImGuiCol_HeaderHovered]   = ImColor(s1gui::color::kKnobEdge).Value;
    colors[ImGuiCol_HeaderActive]    = ImColor(s1gui::color::kAccent).Value;
    colors[ImGuiCol_Text]            = ImColor(s1gui::color::kText).Value;
    colors[ImGuiCol_TextDisabled]    = ImColor(s1gui::color::kTextDim).Value;
    colors[ImGuiCol_SliderGrab]      = ImColor(s1gui::color::kAccent).Value;
    colors[ImGuiCol_SliderGrabActive]= ImColor(s1gui::color::kAccent).Value;
    colors[ImGuiCol_Separator]       = ImColor(s1gui::color::kPanelEdge).Value;
}

/// Panel selector strip, standing in for the NavButtons on iOS.
///
/// Synth One shows two panels stacked, each independently navigable, so there
/// are two of these. `other` is the panel showing in the opposite slot: iOS
/// refuses to put the same panel on screen twice (PanelController's "make sure
/// the same view doesn't appear twice"), and here picking the one already
/// opposite swaps the two rather than duplicating it.
void panelTabs(const char *id, const char *label, s1gui::Panel &current, s1gui::Panel &other) {
    ImGui::PushID(id);

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImColor(s1gui::color::kTextDim), "%s", label);
    ImGui::SameLine(0.0f, 8.0f);

    for (int i = 0; i < static_cast<int>(s1gui::Panel::Count); ++i) {
        const s1gui::Panel panel = static_cast<s1gui::Panel>(i);
        if (i > 0) ImGui::SameLine(0.0f, 3.0f);

        const bool selected = (panel == current);
        const bool opposite = (panel == other);

        ImGui::PushStyleColor(ImGuiCol_Button,
                              selected ? ImColor(s1gui::color::kAccentDim).Value
                                       : ImColor(s1gui::color::kKnobBody).Value);
        // The panel showing opposite is dimmed: still clickable, but it swaps.
        ImGui::PushStyleColor(ImGuiCol_Text,
                              selected  ? ImColor(s1gui::color::kText).Value
                              : opposite ? ImColor(s1gui::color::kOnDim).Value
                                         : ImColor(s1gui::color::kTextDim).Value);
        ImGui::PushID(i);
        if (ImGui::Button(s1gui::PanelName(panel), s1gui::PanelTabSize())) {
            if (panel == other) {
                other = current; // swap rather than show the same panel twice
            }
            current = panel;
        }
        if (opposite && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("showing in the other slot - click to swap");
        }
        ImGui::PopID();
        ImGui::PopStyleColor(2);
    }
    ImGui::PopID();
}

} // namespace

namespace {

/// Where the chosen output is remembered between runs: the data directory that
/// *holds* the preset directory, never the preset directory itself.
///
/// Two reasons it sits outside. Banks are saved as `<bank>.json` right there, so
/// a bank innocently named "audio" would collide with this file and one would
/// silently overwrite the other. And --user-dir moves where *presets* live,
/// which is a different question from which speaker this machine uses -- the
/// device must not follow a preset collection onto another box, so this path
/// deliberately ignores that flag.
/// Returned as a string, and every filesystem::path here is converted with an
/// explicit .string(): on Windows path::value_type is wchar_t, so the implicit
/// conversion that compiles on Linux is not available and neither is handing
/// c_str() to fopen.
std::string audioConfigPath() {
    const std::filesystem::path presets(s1::Engine::defaultUserDataDir());
    const std::filesystem::path root = presets.parent_path();
    return ((root.empty() ? std::filesystem::path(".") : root) / "audio.json").string();
}

/// Device names come from the driver and are not ours to trust as JSON.
std::string jsonEscape(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

void loadAudioConfig(s1gui::UiState &ui) {
    s1::JsonValue root;
    std::string error;
    if (!s1::JsonValue::parseFile(audioConfigPath(), root, error) || !root.isObject()) return;

    if (root.contains("backend")) ui.audioBackend = root["backend"].asString(ui.audioBackend);
    if (root.contains("deviceIndex")) ui.audioDeviceIndex = root["deviceIndex"].asInt(ui.audioDeviceIndex);
    if (root.contains("sampleRate")) ui.audioSampleRate = root["sampleRate"].asInt(ui.audioSampleRate);
    if (root.contains("bufferFrames")) ui.audioBufferFrames = root["bufferFrames"].asInt(ui.audioBufferFrames);

    // A device id is only meaningful next to the name it had when it was saved:
    // ids shift as devices come and go, and silently opening whatever now sits
    // at that index is worse than falling back to automatic.
    const std::string savedName = root["deviceName"].asString();
    if (ui.audioDeviceIndex != s1::kAutoDevice && !savedName.empty()) {
        bool matched = false;
        for (const auto &device : s1::availableOutputDevices(ui.audioBackend)) {
            if (device.index == ui.audioDeviceIndex && device.name == savedName) {
                matched = true;
                break;
            }
        }
        if (!matched) ui.audioDeviceIndex = s1::kAutoDevice;
    }
}

void saveAudioConfig(const s1gui::UiState &ui) {
    std::string deviceName;
    for (const auto &device : ui.audioDevices) {
        if (device.index == ui.audioDeviceIndex) {
            deviceName = device.name;
            break;
        }
    }

    std::string json = "{\n";
    json += "  \"backend\": \"" + ui.audioBackend + "\",\n";
    json += "  \"deviceIndex\": " + std::to_string(ui.audioDeviceIndex) + ",\n";
    json += "  \"deviceName\": \"" + jsonEscape(deviceName) + "\",\n";
    json += "  \"sampleRate\": " + std::to_string(ui.audioSampleRate) + ",\n";
    json += "  \"bufferFrames\": " + std::to_string(ui.audioBufferFrames) + "\n";
    json += "}\n";

    const std::string path = audioConfigPath();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (FILE *f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(json.data(), 1, json.size(), f);
        std::fclose(f);
    }
}

} // namespace

int main(int argc, char **argv) {
    std::string backendName;
    std::string hostApi;
    std::string resourceDir = s1::Engine::defaultResourceDir();
    std::string userDir;
    std::string midiSpec = "all";
    std::string midiOutSpec;
    std::string tuningsPath = s1::Engine::defaultTuningsPath();
    std::string deviceSpec;
    bool backendGiven = false;
    int  requestedRate = 0;
    int  requestedFrames = 0;
    bool listDevices = false;
    int windowWidth = 1440, windowHeight = 900;
    bool fullscreen = false;
    bool hideCursor = false;
    bool geometryGiven = false;
    int  layoutOverride = -1;   // -1 = read it off the display, 0 = desktop, 1 = Pi
    std::string topPanelName, bottomPanelName;
    bool wasapiExclusive = false;
    std::string controllerDriverSpec = "auto";
    bool listControllerDrivers = false;
    bool controllerDriverConfigure = false;

    auto panelByName = [](const std::string &name, s1gui::Panel &out) {
        for (int i = 0; i < static_cast<int>(s1gui::Panel::Count); ++i) {
            if (name == s1gui::PanelName(static_cast<s1gui::Panel>(i))) {
                out = static_cast<s1gui::Panel>(i);
                return true;
            }
        }
        return false;
    };

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
        if (arg == "--backend") { backendName = next(); backendGiven = true; }
        else if (arg == "--host-api") hostApi = next();
        else if (arg == "--wasapi-exclusive") wasapiExclusive = true;
        else if (arg == "--device") deviceSpec = next();
        else if (arg == "--rate") requestedRate = std::atoi(next().c_str());
        else if (arg == "--buffer") requestedFrames = std::atoi(next().c_str());
        else if (arg == "--list-devices") listDevices = true;
        else if (arg == "--controller-driver") controllerDriverSpec = next();
        else if (arg == "--controller-driver-configure") controllerDriverConfigure = true;
        else if (arg == "--list-controller-drivers") listControllerDrivers = true;
        else if (arg == "--resources") resourceDir = next();
        else if (arg == "--user-dir") userDir = next();
        else if (arg == "--midi") midiSpec = next();
        else if (arg == "--midi-out") midiOutSpec = next();
        else if (arg == "--tunings") tuningsPath = next();
        else if (arg == "--top") topPanelName = next();
        else if (arg == "--bottom") bottomPanelName = next();
        else if (arg == "--compact") layoutOverride = 1;
        else if (arg == "--no-compact") layoutOverride = 0;
        else if (arg == "--geometry") {
            const std::string g = next();
            const size_t x = g.find('x');
            if (x != std::string::npos) {
                windowWidth = std::atoi(g.substr(0, x).c_str());
                windowHeight = std::atoi(g.substr(x + 1).c_str());
                geometryGiven = true;
            }
        } else if (arg == "--fullscreen") fullscreen = true;
        else if (arg == "--hide-cursor") hideCursor = true;
        else if (arg == "-h" || arg == "--help") {
            std::printf("usage: synthone-gui [--backend jack|portaudio] [--host-api NAME]\n"
                        "       [--resources DIR] [--wasapi-exclusive]\n"
                        "                    [--device INDEX|NAME] [--list-devices]\n"
                        "                    [--rate HZ] [--buffer FRAMES]\n"
                        "                    [--midi CLIENT:PORT|all] [--midi-out CLIENT:PORT|all]\n"
                        "                    [--controller-driver auto|off|NAME]\n"
                        "                    [--controller-driver-configure] [--list-controller-drivers]\n"
                        "                    [--geometry WxH]\n"
                        "                    [--top PANEL] [--bottom PANEL]\n"
                        "                    [--fullscreen] [--hide-cursor]\n"
                        "                    [--compact | --no-compact]\n"
                        "  PANEL: MAIN ENV PAD FX SEQ TUNE\n"
                        "  compact is chosen automatically on a display under\n"
                        "  1000x620, such as the Raspberry Pi 7\" panel (800x480);\n"
                        "  the header's PI VIEW button switches it while running\n");
            return 0;
        }
    }

    // -- audio + engine ----------------------------------------------------

    const auto backends = s1::availableBackends();
    if (backends.empty()) {
        std::fprintf(stderr, "error: no audio backend compiled in\n");
        return 1;
    }
    if (backendName.empty()) backendName = backends.front();

    if (listControllerDrivers) {
        for (const auto &name : s1::ctrldev::ControllerDriverManager::availableDriverNames()) {
            std::printf("%s\n", name.c_str());
        }
        return 0;
    }

    if (listDevices) {
        for (const auto &device : s1::availableOutputDevices(backendName)) {
            std::printf("  %3d  %-44s %-10s %6.0f Hz  %dch%s\n", device.index, device.name.c_str(),
                        device.hostApi.c_str(), device.defaultSampleRate, device.maxChannels,
                        device.isDefault ? "  [default]" : "");
        }
        return 0;
    }

    s1::Engine engine;
    if (!userDir.empty()) engine.setUserDataDir(userDir);

    // Audio preferences: the saved file is the baseline, anything given on the
    // command line wins over it, and neither is written back until the dialog
    // applies a change -- so a one-off --device does not become permanent.
    s1gui::UiState ui;
    ui.audioBackend = backendName;
    loadAudioConfig(ui);
    if (backendGiven) ui.audioBackend = backendName;
    if (requestedRate > 0) ui.audioSampleRate = requestedRate;
    if (requestedFrames > 0) ui.audioBufferFrames = requestedFrames;
    if (!deviceSpec.empty()) {
        s1gui::RefreshAudioDevices(ui);
        std::string resolveError;
        if (!s1::resolveOutputDevice(deviceSpec, ui.audioDevices, ui.audioDeviceIndex,
                                     resolveError)) {
            std::fprintf(stderr, "error: %s\n", resolveError.c_str());
            return 1;
        }
    }

    std::string error;
    auto backend = s1::makeBackend(ui.audioBackend, hostApi, ui.audioDeviceIndex, wasapiExclusive,
                                   error);
    if (!backend || !backend->open(ui.audioSampleRate, ui.audioBufferFrames, 0.0, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    if (!engine.start(backend->sampleRate(), 2, resourceDir, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    if (!engine.loadBanks(error)) {
        std::fprintf(stderr, "warning: %s\n", error.c_str());
    }
    if (!engine.loadTunings(tuningsPath, error)) {
        std::fprintf(stderr, "warning: tunings: %s\n", error.c_str());
    }

    ui.layoutOverride = layoutOverride;
    if (!topPanelName.empty()) panelByName(topPanelName, ui.topPanel);
    if (!bottomPanelName.empty()) panelByName(bottomPanelName, ui.bottomPanel);

    if (ui.topPanel == ui.bottomPanel) {
        // Never start with the same panel in both slots.
        ui.bottomPanel = static_cast<s1gui::Panel>(
            (static_cast<int>(ui.topPanel) + 1) % static_cast<int>(s1gui::Panel::Count));
    }
    const auto banks = engine.bankNames();
    if (!banks.empty()) {
        ui.currentBank = banks.front();
        const auto presets = engine.presetsInBank(ui.currentBank);
        if (!presets.empty()) {
            std::string ignored;
            if (engine.applyPreset(ui.currentBank, presets.front().position, ignored)) {
                ui.currentPreset = presets.front().position;
                ui.currentPresetName = presets.front().name;
            }
        }
    }

    UiObserver observer(ui);
    engine.setObserver(&observer);

    s1::MidiQueue midiQueue;
    s1::SysExQueue sysexQueue;
    s1::ProgramChangeQueue programChangeQueue;
    s1::MidiInput midi;
    std::string midiStatus = "unavailable";
    if (midi.open("SynthOne", error)) {
        std::string ignored;
        midi.connect(midiSpec, ignored);
        midi.start(&midiQueue, &sysexQueue, &programChangeQueue);
        midiStatus = midi.portName();
    }

    s1::MidiOutput midiOut;
    if (!midiOutSpec.empty() && midiOut.open("SynthOne", error)) {
        std::string ignored;
        if (midiOut.connect(midiOutSpec, ignored)) observer.midiOut = &midiOut;
    }

    s1::ctrldev::ControllerDriverManager driverManager;
    if (controllerDriverSpec != "off") {
        std::string driverStatus;
        if (driverManager.load(controllerDriverSpec, engine, midi.connectedSources(),
                               controllerDriverConfigure, driverStatus)) {
            std::printf("controller driver: %s\n", driverStatus.c_str());
        }
    }

    auto render = [&engine, &midiQueue](float *left, float *right, uint32_t frames) {
        s1::MidiMessage m;
        while (midiQueue.pop(m)) engine.handleMidi(m.data, m.length);
        engine.render(left, right, frames);
    };

    // The srate callback runs on JACK's notification thread and must not
    // block or allocate, so it only records the new rate; the actual
    // reinitialization happens in the UI loop below, with the stream stopped
    // so it can never race the render callback. applyAudio() below builds a
    // fresh backend object on every device change, so this has to be
    // reinstalled on it too, not just the one created here.
    std::atomic<double> pendingSampleRate{0.0};
    auto installSrateCallback = [&pendingSampleRate](s1::AudioBackend &b) {
        b.setSampleRateChangeCallback([&pendingSampleRate](double newRate) {
            pendingSampleRate.store(newRate, std::memory_order_relaxed);
        });
    };
    installSrateCallback(*backend);

    if (!backend->start(render, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    auto describeAudio = [&backend]() {
        char buf[320];
        std::snprintf(buf, sizeof(buf), "%s  %s   %.0f Hz / %u frames", backend->name(),
                      backend->description().c_str(), backend->sampleRate(),
                      backend->bufferFrames());
        return std::string(buf);
    };
    ui.audioStatus = describeAudio();

    // The DSP is built for one sample rate: the wavetable increments, envelope
    // rates and LFO phases all derive from it, so changing rate means building
    // the kernel again and putting the preset and tuning back.
    auto rebuildEngine = [&](double rate) {
        std::string rebuildError;
        if (!engine.start(rate, 2, resourceDir, rebuildError)) return false;
        const int tuning = engine.currentTuningIndex();
        if (tuning >= 0) engine.applyTuning(tuning);
        if (!ui.currentBank.empty()) {
            std::string ignored;
            engine.applyPreset(ui.currentBank, ui.currentPreset, ignored);
        }
        return true;
    };

    // Reopen the stream on the device the dialog chose. Every failure path ends
    // with the synth audible on the device it had before, because a mistyped
    // device is a much smaller problem than a synth that has gone silent.
    auto applyAudio = [&]() {
        const double previousRate = engine.sampleRate();
        backend->stop();
        engine.allNotesOff();

        std::string applyError;
        auto next = s1::makeBackend(ui.audioBackend, hostApi, ui.audioDeviceIndex, wasapiExclusive,
                                    applyError);
        if (next && next->open(ui.audioSampleRate, ui.audioBufferFrames, 0.0, applyError)) {
            installSrateCallback(*next);
            const bool rateChanged = next->sampleRate() != previousRate;
            if (!rateChanged || rebuildEngine(next->sampleRate())) {
                if (next->start(render, applyError)) {
                    backend = std::move(next);
                    ui.audioStatus = describeAudio();
                    ui.notify("audio: " + ui.audioStatus);
                    saveAudioConfig(ui);
                    return;
                }
                // The new stream refused to start after the kernel had already
                // moved: put the kernel back before falling through.
                if (rateChanged) rebuildEngine(previousRate);
            } else {
                applyError = "could not rebuild the DSP at " +
                             std::to_string(static_cast<int>(next->sampleRate())) + " Hz";
                rebuildEngine(previousRate);
            }
        }

        std::string restartError;
        if (backend->start(render, restartError)) {
            ui.notify("audio: " + applyError + " -- kept the previous device");
        } else {
            ui.notify("audio: " + applyError + "; the previous device also failed: " +
                      restartError);
        }
        ui.audioStatus = describeAudio();
    };

    // -- window ------------------------------------------------------------

    glfwSetErrorCallback([](int code, const char *description) {
        std::fprintf(stderr, "glfw error %d: %s\n", code, description);
    });
    if (!glfwInit()) {
        std::fprintf(stderr, "error: cannot initialise GLFW\n");
        return 1;
    }
    // Kiosk mode: take the primary monitor at its current resolution unless a
    // --geometry was asked for. On the Pi's 7" panel that is 800x480, and with
    // no window manager running there is nothing else to yield the screen to.
    GLFWmonitor *monitor = nullptr;
    if (fullscreen) {
        monitor = glfwGetPrimaryMonitor();
        if (monitor == nullptr) {
            std::fprintf(stderr, "warning: no monitor reported; staying windowed\n");
        } else if (const GLFWvidmode *mode = glfwGetVideoMode(monitor)) {
            if (!geometryGiven) {
                windowWidth = mode->width;
                windowHeight = mode->height;
            }
            glfwWindowHint(GLFW_RED_BITS, mode->redBits);
            glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
            glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
            glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        }
    }

    // GL 3.2 core is what the ImGui backend is happiest on, but it is not
    // available everywhere: the Raspberry Pi 4's V3D driver tops out at
    // OpenGL 3.1 (Mesa 24.2 on Bookworm), and asking for 3.2 core there does
    // not degrade -- glfwCreateWindow fails outright with GLXBadFBConfig and
    // the kiosk dies two seconds after every start.
    //
    // So try the tiers in order and keep the first that gives a window. The
    // GLSL string has to move with the context: #version 150 *is* GL 3.2, and
    // leaving it behind on a 3.1 context would only move the failure into
    // shader compilation.
    struct GlTier {
        int         major;
        int         minor;
        int         profile;
        const char *glsl;
    };
    static const GlTier kGlTiers[] = {
        {3, 2, GLFW_OPENGL_CORE_PROFILE, "#version 150"},
        {3, 1, GLFW_OPENGL_ANY_PROFILE,  "#version 140"},
    };

    // Failing a tier is expected, not news, so the error callback is muted
    // while probing -- otherwise every Pi boot logs a GLXBadFBConfig that
    // looks like the fault and is not. Whatever survives is reported below.
    GLFWerrorfun previousErrorCallback = glfwSetErrorCallback(nullptr);

    GLFWwindow *window = nullptr;
    const char *glslVersion = kGlTiers[0].glsl;
    std::string lastGlError;
    for (const GlTier &tier : kGlTiers) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, tier.major);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, tier.minor);
        glfwWindowHint(GLFW_OPENGL_PROFILE, tier.profile);

        window = glfwCreateWindow(windowWidth, windowHeight, "AudioKit Synth One", monitor,
                                  nullptr);
        if (window != nullptr) {
            glslVersion = tier.glsl;
            break;
        }
        // Take the error so the next attempt starts clean, and keep the text
        // in case this was the last tier.
        const char *text = nullptr;
        glfwGetError(&text);
        lastGlError = text != nullptr ? text : "unknown error";
    }

    glfwSetErrorCallback(previousErrorCallback);

    if (window == nullptr) {
        std::fprintf(stderr, "error: cannot create a window; no usable OpenGL context (%s)\n",
                     lastGlError.c_str());
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (hideCursor) {
        // A pointer arrow on a touch panel is noise; it only reappears if the
        // kiosk is driven with a mouse, which is what GLFW_CURSOR_HIDDEN allows.
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // don't litter the cwd
    applyDarkStyle();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    // -- loop --------------------------------------------------------------

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        const double newRate = pendingSampleRate.exchange(0.0, std::memory_order_relaxed);
        if (newRate > 0.0 && newRate != engine.sampleRate()) {
            backend->stop();
            engine.setSampleRate(newRate);
            std::string resumeError;
            if (!backend->start(render, resumeError)) {
                std::fprintf(stderr, "error: failed to resume audio after sample-rate change: %s\n",
                            resumeError.c_str());
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }

        engine.drainNotifications();

        s1::SysExMessage sysexMsg;
        while (sysexQueue.pop(sysexMsg)) driverManager.dispatchSysEx(sysexMsg);

        s1::ProgramChangeMessage pcMsg;
        while (programChangeQueue.pop(pcMsg)) driverManager.dispatchProgramChange(pcMsg);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (!ui.message.empty() && ImGui::GetTime() - ui.messageTime > 3.0) ui.message.clear();

        const ImGuiViewport *viewport = ImGui::GetMainViewport();

        // Decide the density from what we are actually drawing on, so the same
        // binary suits a desktop window and the Pi's 800x480 panel, and adapts
        // if the window is resized. 1000x620 sits above 800x480 and below the
        // smallest roomy layout that still works. An explicit choice -- the
        // PI VIEW toggle, or --compact/--no-compact -- wins over the display.
        const bool wantCompact = (ui.layoutOverride >= 0)
                                     ? (ui.layoutOverride == 1)
                                     : (viewport->WorkSize.x < 1000.0f || viewport->WorkSize.y < 620.0f);
        // How much bigger than the 800x480 baseline this panel is, taken from
        // whichever axis is tightest so nothing overflows the other one. The
        // Waveshare 7" (1024x600) lands at 1.25; the official 7" panel stays at
        // 1.0. Capped because past about 1.5 the block flow starts leaving
        // whole shelves empty again, which is the problem it exists to fix.
        const float roomScale =
            std::min({viewport->WorkSize.x / 800.0f, viewport->WorkSize.y / 480.0f, 1.5f});
        s1gui::SetCompactScale(roomScale);

        if (wantCompact != ui.compact) {
            ui.compact = wantCompact;
            applySpacing(ui.compact);
            s1gui::SetCompactWidgets(ui.compact);
            // The on-screen keyboard is dead weight on a MIDI-driven box; start
            // it hidden on a small display and let KEYS bring it back.
            if (ui.compact) ui.showKeyboard = false;
        }

        // One panel or two follows the density: a short screen has room for
        // exactly one, a desktop has room for two and is better off showing
        // them than hiding one behind a tab. Set every frame rather than on the
        // threshold crossing above -- a desktop starts non-compact and never
        // crosses it, so a transition-only assignment would leave this at its
        // initial value.
        ui.singlePanel = ui.compact;


        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("SynthOne", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

        s1gui::SetLearnMode(ui.midiLearnMode);
        s1gui::DrawHeader(engine, ui);
        ImGui::Separator();

        if (ui.showPresets) {
            s1gui::DrawPresetBrowser(engine, ui);
        } else {
            // The keyboard bar is a wheel column (74px slider plus its caption)
            // beside the keys, so it packs down to ~108px before anything is
            // clipped. 150px is comfort, not a requirement.
            const float keyboardHeight = ui.showKeyboard ? (ui.compact ? 108.0f : 150.0f) : 0.0f;
            const float bodyHeight = ImGui::GetContentRegionAvail().y - keyboardHeight;

            auto drawSlot = [&](const char *childId, s1gui::Panel panel, const ImVec2 &size) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImColor(s1gui::color::kPanel).Value);
                ImGui::BeginChild(childId, size, ImGuiChildFlags_Borders);
                s1gui::DrawPanel(panel, engine, ui);
                ImGui::EndChild();
                ImGui::PopStyleColor();
            };

            if (ui.singlePanel) {
                // One panel filling the body, switched by its own tab row --
                // the way the iOS app presents a panel at a time. Nothing is
                // "opposite", so pass a scratch panel the tabs can write to.
                s1gui::Panel unused = s1gui::Panel::Count;
                panelTabs("single", "PANEL", ui.topPanel, unused);
                // Measure after the tabs rather than guessing their height, so
                // the panel ends exactly where the keyboard bar begins.
                const float remaining = ImGui::GetContentRegionAvail().y - keyboardHeight - 4.0f;
                drawSlot("toppanel", ui.topPanel, ImVec2(0, remaining));
            } else {
                // Desktop: both panels stacked, each with its own tab row. Two
                // panes of a 900px screen give ~330px each, which the block
                // flow fills by packing wider shelves than it would at 800x480.
                const float paneHeight = bodyHeight * 0.5f - 24.0f;

                panelTabs("top", "UPPER", ui.topPanel, ui.bottomPanel);
                drawSlot("toppanel", ui.topPanel, ImVec2(0, paneHeight));

                panelTabs("bottom", "LOWER", ui.bottomPanel, ui.topPanel);
                drawSlot("bottompanel", ui.bottomPanel, ImVec2(0, paneHeight));
            }

            if (ui.showKeyboard) s1gui::DrawKeyboardBar(engine, ui);
        }

        s1gui::DrawSaveDialog(engine, ui);
        s1gui::DrawAudioDialog(ui);
        ImGui::End();

        // Between frames, never inside one: reopening the stream tears down the
        // realtime thread the render callback runs on.
        if (ui.audioApplyRequested) {
            ui.audioApplyRequested = false;
            applyAudio();
        }

        // On-screen keyboard note tracking: send note on/off as the pointer
        // moves across keys, through the same queue as hardware MIDI.
        if (ui.keyboardNote != ui.keyboardNotePrev) {
            if (ui.keyboardNotePrev >= 0 && !ui.holdMode) {
                const uint8_t off[3] = {0x80, static_cast<uint8_t>(ui.keyboardNotePrev), 0};
                s1::MidiMessage m; m.length = 3; std::memcpy(m.data, off, 3);
                midiQueue.push(m);
            }
            if (ui.keyboardNote >= 0) {
                const uint8_t on[3] = {0x90, static_cast<uint8_t>(ui.keyboardNote), 100};
                s1::MidiMessage m; m.length = 3; std::memcpy(m.data, on, 3);
                midiQueue.push(m);
            }
            ui.keyboardNotePrev = ui.keyboardNote;
        }

        ImGui::Render();
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // -- teardown ----------------------------------------------------------

    midi.stop();
    engine.allNotesOff();
    backend->stop();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
