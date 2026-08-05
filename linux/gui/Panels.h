//
//  Panels.h
//  AudioKitSynthOne - Linux port
//

#pragma once

#include <string>
#include <vector>

#include "Engine.h"
#include "Widgets.h"

namespace s1gui {

/// Panel identity and ordering, mirroring Navigation/ChildPanel.swift.
enum class Panel { Generators = 0, Envelopes, TouchPad, Effects, Sequencer, Tunings, Count };

const char *PanelName(Panel panel);

/// UI state that lives outside the DSP.
struct UiState {
    Panel topPanel = Panel::Generators;
    Panel bottomPanel = Panel::Effects;
    bool  showKeyboard = true;
    bool  showPresets = false;
    /// One panel at a time, the way the iOS app does it, or two stacked.
    /// Follows `compact`: a short screen has room for one panel; a desktop has
    /// room for two and shows both rather than hiding one behind a tab.
    bool  singlePanel = true;
    /// The layout actually being drawn. Compact gets tighter chrome, a two-row
    /// header and one panel; knobs and buttons keep their size -- the space
    /// comes out of padding, not out of the touch targets.
    bool  compact = false;
    /// What decides `compact`: -1 reads it off the framebuffer each frame, 0
    /// forces the desktop layout, 1 forces the Pi one. Set by `--compact` /
    /// `--no-compact`, and by the header's PI VIEW toggle -- clicking that
    /// leaves auto for good, since a display-driven layout that overrode the
    /// button on the next resize would be the button not working.
    int   layoutOverride = -1;

    // Keyboard
    int   firstOctave = 3;
    int   octaveCount = 3;
    bool  holdMode = false;
    int   keyboardNote = -1;
    int   keyboardNotePrev = -1;

    // Touch pad latch positions (snap toggle off == latched)
    bool  padLatch = true;
    float pad1X = 0.5f, pad1Y = 0.5f;
    float pad2X = 0.5f, pad2Y = 0.5f;

    // Wheels
    float modWheel = 0.0f;
    float pitchBend = 0.5f;

    // Preset browser
    std::string currentBank;
    int         currentPreset = 0;
    std::string currentPresetName;
    char        searchText[64] = {0};

    // Live DSP state, refreshed from notifications
    bool heldNotes[128] = {false};
    int  arpBeat = -1;
    int  voiceCount = 0;

    // Tunings
    char tuningSearch[64] = {0};

    // Save dialog
    bool showSaveDialog = false;
    char saveName[64] = {0};
    char saveBank[64] = "User";
    int  savePosition = 0;

    // MIDI learn
    bool midiLearnMode = false;

    // Status line
    std::string message;
    double      messageTime = 0.0;

    void notify(const std::string &text);
};

void DrawHeader(s1::Engine &engine, UiState &ui);
void DrawPanel(Panel panel, s1::Engine &engine, UiState &ui);
void DrawPresetBrowser(s1::Engine &engine, UiState &ui);
void DrawKeyboardBar(s1::Engine &engine, UiState &ui);
void DrawSaveDialog(s1::Engine &engine, UiState &ui);

} // namespace s1gui
