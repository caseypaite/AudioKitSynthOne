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
#include <string>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include "AlsaMidi.h"
#include "AudioBackend.h"
#include "Engine.h"
#include "Panels.h"
#include "Widgets.h"

namespace {

/// Mirrors DSP notifications into the UI state.
class UiObserver : public S1Protocol {
public:
    explicit UiObserver(s1gui::UiState &ui) : mUi(ui) {}

    void heldNotesDidChange(HeldNotes notes) override {
        std::memcpy(mUi.heldNotes, notes.heldNotes, sizeof(mUi.heldNotes));
    }
    void arpBeatCounterDidChange(S1ArpBeatCounter counter) override {
        mUi.arpBeat = counter.beatCounter;
    }
    void playingNotesDidChange(PlayingNotes notes) override {
        int voices = 0;
        for (int i = 0; i < notes.polyphony; ++i) {
            if (notes.playingNotes[i].noteNumber >= 0) ++voices;
        }
        mUi.voiceCount = voices;
    }

private:
    s1gui::UiState &mUi;
};

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
void panelTabs(const char *id, s1gui::Panel &current) {
    ImGui::PushID(id);
    for (int i = 0; i < static_cast<int>(s1gui::Panel::Count); ++i) {
        const s1gui::Panel panel = static_cast<s1gui::Panel>(i);
        if (i > 0) ImGui::SameLine(0.0f, 3.0f);
        const bool selected = (panel == current);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              selected ? ImColor(s1gui::color::kAccentDim).Value
                                       : ImColor(s1gui::color::kKnobBody).Value);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              selected ? ImColor(s1gui::color::kText).Value
                                       : ImColor(s1gui::color::kTextDim).Value);
        ImGui::PushID(i);
        if (ImGui::Button(s1gui::PanelName(panel), ImVec2(74, 0))) current = panel;
        ImGui::PopID();
        ImGui::PopStyleColor(2);
    }
    ImGui::PopID();
}

} // namespace

int main(int argc, char **argv) {
    std::string backendName;
    std::string resourceDir = S1_DEFAULT_RESOURCE_DIR;
    std::string midiSpec = "all";
    std::string tuningsPath = S1_TUNINGS_JSON;
    int windowWidth = 1440, windowHeight = 900;
    std::string topPanelName, bottomPanelName;

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
        if (arg == "--backend") backendName = next();
        else if (arg == "--resources") resourceDir = next();
        else if (arg == "--midi") midiSpec = next();
        else if (arg == "--tunings") tuningsPath = next();
        else if (arg == "--top") topPanelName = next();
        else if (arg == "--bottom") bottomPanelName = next();
        else if (arg == "--geometry") {
            const std::string g = next();
            const size_t x = g.find('x');
            if (x != std::string::npos) {
                windowWidth = std::atoi(g.substr(0, x).c_str());
                windowHeight = std::atoi(g.substr(x + 1).c_str());
            }
        } else if (arg == "-h" || arg == "--help") {
            std::printf("usage: synthone-gui [--backend jack|portaudio] [--resources DIR]\n"
                        "                    [--midi CLIENT:PORT|all] [--geometry WxH]\n"
                        "                    [--top PANEL] [--bottom PANEL]\n"
                        "  PANEL: MAIN ENV PAD FX SEQ TUNE\n");
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

    std::string error;
    auto backend = s1::makeBackend(backendName, error);
    if (!backend || !backend->open(0, 0, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    s1::Engine engine;
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

    s1gui::UiState ui;
    if (!topPanelName.empty()) panelByName(topPanelName, ui.topPanel);
    if (!bottomPanelName.empty()) panelByName(bottomPanelName, ui.bottomPanel);
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
    s1::AlsaMidiInput midi;
    std::string midiStatus = "unavailable";
    if (midi.open("SynthOne", error)) {
        std::string ignored;
        midi.connect(midiSpec, ignored);
        midi.start(&midiQueue);
        midiStatus = midi.portName();
    }

    auto render = [&engine, &midiQueue](float *left, float *right, uint32_t frames) {
        s1::MidiMessage m;
        while (midiQueue.pop(m)) engine.handleMidi(m.data, m.length);
        engine.render(left, right, frames);
    };
    if (!backend->start(render, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    // -- window ------------------------------------------------------------

    glfwSetErrorCallback([](int code, const char *description) {
        std::fprintf(stderr, "glfw error %d: %s\n", code, description);
    });
    if (!glfwInit()) {
        std::fprintf(stderr, "error: cannot initialise GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *window =
        glfwCreateWindow(windowWidth, windowHeight, "AudioKit Synth One", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "error: cannot create a window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // don't litter the cwd
    applyDarkStyle();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    // -- loop --------------------------------------------------------------

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        engine.drainNotifications();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (!ui.message.empty() && ImGui::GetTime() - ui.messageTime > 3.0) ui.message.clear();

        const ImGuiViewport *viewport = ImGui::GetMainViewport();
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
            const float keyboardHeight = ui.showKeyboard ? 150.0f : 0.0f;
            const float bodyHeight = ImGui::GetContentRegionAvail().y - keyboardHeight;
            const float panelHeight = bodyHeight * 0.5f - 24.0f;

            panelTabs("top", ui.topPanel);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImColor(s1gui::color::kPanel).Value);
            ImGui::BeginChild("toppanel", ImVec2(0, panelHeight), ImGuiChildFlags_Borders);
            s1gui::DrawPanel(ui.topPanel, engine, ui);
            ImGui::EndChild();
            ImGui::PopStyleColor();

            panelTabs("bottom", ui.bottomPanel);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImColor(s1gui::color::kPanel).Value);
            ImGui::BeginChild("bottompanel", ImVec2(0, panelHeight), ImGuiChildFlags_Borders);
            s1gui::DrawPanel(ui.bottomPanel, engine, ui);
            ImGui::EndChild();
            ImGui::PopStyleColor();

            if (ui.showKeyboard) s1gui::DrawKeyboardBar(engine, ui);
        }

        s1gui::DrawSaveDialog(engine, ui);
        ImGui::End();

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
