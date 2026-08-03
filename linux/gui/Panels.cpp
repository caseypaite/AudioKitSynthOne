//
//  Panels.cpp
//  AudioKitSynthOne - Linux port
//
//  Every control here is transcribed from the corresponding Swift panel
//  controller's conductor.bind(...) calls, so the parameter each widget drives
//  matches the iOS app. Knob tapers come from the `.taper =` assignments in
//  those same files (default 1.0 == linear).
//

#include "Panels.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace s1gui {

namespace {

// Payload IDs, matching Conductor.swift so the DSP echo can be attributed.
constexpr int kLfo1RateEffectsPanelID = 1;
constexpr int kLfo2RateEffectsPanelID = 2;
constexpr int kAutoPanEffectsPanelID = 3;
constexpr int kDelayTimeEffectsPanelID = 4;
constexpr int kLfo1RateTouchPadID = 5;
constexpr int kArpSeqTempoMultiplierID = 9;

const char *const kLfoWaveforms[] = {"SIN", "SQR", "SAW", "RSAW"};
const char *const kFilterTypes[] = {"LP", "BP", "HP"};
const char *const kArpDirections[] = {"DOWN", "UP", "UP+DN"};

void beginPanelChild(const char *id, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImColor(color::kPanel).Value);
    ImGui::BeginChild(id, ImVec2(0, height), ImGuiChildFlags_Borders);
}

void endPanelChild() {
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace

void UiState::notify(const std::string &text) {
    message = text;
    messageTime = ImGui::GetTime();
}

const char *PanelName(Panel panel) {
    switch (panel) {
        case Panel::Generators: return "MAIN";
        case Panel::Envelopes:  return "ENV";
        case Panel::TouchPad:   return "PAD";
        case Panel::Effects:    return "FX";
        case Panel::Sequencer:  return "SEQ";
        case Panel::Tunings:    return "TUNE";
        default:                return "?";
    }
}

// ---------------------------------------------------------------------------
// Generators / MAIN
// ---------------------------------------------------------------------------

static void drawGenerators(s1::Engine &engine, UiState &) {
    SectionLabel("OSCILLATORS");
    ImGui::BeginGroup();
    Knob(engine, {index1, "MORPH 1", 1.0f, Units::Raw});             ImGui::SameLine();
    Knob(engine, {morph1SemitoneOffset, "SEMI 1", 1.0f, Units::Semitones}); ImGui::SameLine();
    Knob(engine, {morph1Volume, "VOL 1", 1.0f, Units::Percent});     ImGui::SameLine();
    ImGui::Dummy(ImVec2(18, 1));                                      ImGui::SameLine();
    Knob(engine, {index2, "MORPH 2", 1.0f, Units::Raw});             ImGui::SameLine();
    Knob(engine, {morph2SemitoneOffset, "SEMI 2", 1.0f, Units::Semitones}); ImGui::SameLine();
    Knob(engine, {morph2Detuning, "DETUNE", 1.0f, Units::Raw});      ImGui::SameLine();
    Knob(engine, {morph2Volume, "VOL 2", 1.0f, Units::Percent});     ImGui::SameLine();
    ImGui::Dummy(ImVec2(18, 1));                                      ImGui::SameLine();
    Knob(engine, {morphBalance, "MIX 1<>2", 1.0f, Units::Percent});
    ImGui::EndGroup();

    SectionLabel("SUB / FM / NOISE");
    ImGui::BeginGroup();
    Knob(engine, {subVolume, "SUB", 1.0f, Units::Percent});          ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(1, 22));
    Toggle(engine, subOctaveDown, "-24", ImVec2(52, 0));
    Toggle(engine, subIsSquare, "SQR", ImVec2(52, 0));
    ImGui::EndGroup();                                                ImGui::SameLine();
    ImGui::Dummy(ImVec2(18, 1));                                      ImGui::SameLine();
    Knob(engine, {fmVolume, "FM MIX", 1.0f, Units::Percent});        ImGui::SameLine();
    Knob(engine, {fmAmount, "FM MOD", 1.0f, Units::Raw});            ImGui::SameLine();
    ImGui::Dummy(ImVec2(18, 1));                                      ImGui::SameLine();
    Knob(engine, {noiseVolume, "NOISE", 1.0f, Units::Percent});
    ImGui::EndGroup();

    SectionLabel("FILTER");
    ImGui::BeginGroup();
    Knob(engine, {cutoff, "CUTOFF", 2.0f, Units::Hertz}, 58.0f);     ImGui::SameLine();
    Knob(engine, {resonance, "RES", 1.0f, Units::Percent});          ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(1, 22));
    Selector(engine, filterType, "TYPE", kFilterTypes, 3);
    ImGui::EndGroup();
    ImGui::EndGroup();

    SectionLabel("MASTER");
    ImGui::BeginGroup();
    Knob(engine, {masterVolume, "VOLUME", 2.0f, Units::Percent}, 58.0f); ImGui::SameLine();
    Knob(engine, {glide, "GLIDE", 2.0f, Units::Seconds});               ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(1, 22));
    Toggle(engine, isMono, "MONO", ImVec2(64, 0));
    Toggle(engine, monoIsLegato, "LEGATO", ImVec2(64, 0));
    ImGui::EndGroup();                                                   ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(1, 22));
    Toggle(engine, widen, "WIDEN", ImVec2(74, 0));
    Toggle(engine, oscBandlimitEnable, "BANDLIM", ImVec2(74, 0));
    ImGui::EndGroup();                                                   ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(1, 12));
    Stepper(engine, arpRate, "TEMPO (BPM)", 130.0f);
    ImGui::EndGroup();
    ImGui::EndGroup();
}

// ---------------------------------------------------------------------------
// Envelopes / ENV
// ---------------------------------------------------------------------------

static void drawEnvelopes(s1::Engine &engine, UiState &) {
    const float available = ImGui::GetContentRegionAvail().x;
    const float halfWidth = std::max(260.0f, available * 0.5f - 12.0f);

    ImGui::BeginGroup();
    SectionLabel("AMPLITUDE ENVELOPE");
    ADSREditor(engine, attackDuration, decayDuration, sustainLevel, releaseDuration,
               ImVec2(halfWidth, 110.0f), color::kAccent);
    Knob(engine, {attackDuration, "ATTACK", 1.0f, Units::Seconds});   ImGui::SameLine();
    Knob(engine, {decayDuration, "DECAY", 1.0f, Units::Seconds});     ImGui::SameLine();
    Knob(engine, {sustainLevel, "SUSTAIN", 1.0f, Units::Percent});    ImGui::SameLine();
    Knob(engine, {releaseDuration, "RELEASE", 1.0f, Units::Seconds});
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 24.0f);

    ImGui::BeginGroup();
    SectionLabel("FILTER ENVELOPE");
    ADSREditor(engine, filterAttackDuration, filterDecayDuration, filterSustainLevel,
               filterReleaseDuration, ImVec2(halfWidth, 110.0f), color::kOn);
    Knob(engine, {filterAttackDuration, "ATTACK", 1.0f, Units::Seconds});  ImGui::SameLine();
    Knob(engine, {filterDecayDuration, "DECAY", 1.0f, Units::Seconds});    ImGui::SameLine();
    Knob(engine, {filterSustainLevel, "SUSTAIN", 1.0f, Units::Percent});   ImGui::SameLine();
    Knob(engine, {filterReleaseDuration, "RELEASE", 1.0f, Units::Seconds});
    ImGui::EndGroup();

    SectionLabel("ENVELOPE AMOUNT");
    Knob(engine, {filterADSRMix, "FILTER ENV", 1.0f, Units::Percent}, 58.0f); ImGui::SameLine();
    Knob(engine, {adsrPitchTracking, "PITCH TRACK", 3.0f, Units::Percent}, 58.0f);
}

// ---------------------------------------------------------------------------
// TouchPad / PAD
// ---------------------------------------------------------------------------

static void drawTouchPad(s1::Engine &engine, UiState &ui) {
    SectionLabel("TOUCH PADS");

    ImGui::BeginGroup();
    ImGui::TextColored(ImColor(color::kTextDim), "PAD 1  -  LFO1 RATE / LFO1 AMOUNT");
    if (TouchPadXY("pad1", ImVec2(300, 220), ui.pad1X, ui.pad1Y, ui.padLatch,
                   "LFO1 RATE", "LFO1 AMT")) {
        engine.setDependentParameter(lfo1Rate, ui.pad1X, kLfo1RateTouchPadID);
        engine.setParameter(lfo1Amplitude, ui.pad1Y);
    }
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 24.0f);

    ImGui::BeginGroup();
    ImGui::TextColored(ImColor(color::kTextDim), "PAD 2  -  CUTOFF / RESONANCE");
    if (TouchPadXY("pad2", ImVec2(300, 220), ui.pad2X, ui.pad2Y, ui.padLatch,
                   "CUTOFF", "RESONANCE")) {
        engine.setParameter(cutoff, engine.positionToValue(cutoff, ui.pad2X, 2.0f));
        // iOS scales the vertical axis so the top of the pad stops short of
        // self-oscillation; resonance maxes at 0.98 in the parameter table.
        engine.setParameter(resonance, engine.positionToValue(resonance, ui.pad2Y, 1.0f));
    }
    ImGui::EndGroup();

    ImGui::Spacing();
    ToggleValue(ui.padLatch, ui.padLatch ? "LATCH: ON" : "LATCH: OFF", ImVec2(120, 0));
    ImGui::SameLine();
    ImGui::TextColored(ImColor(color::kTextDim),
                       "with latch off the pad snaps back on release");
}

// ---------------------------------------------------------------------------
// Effects / FX
// ---------------------------------------------------------------------------

static void drawEffects(s1::Engine &engine, UiState &) {
    SectionLabel("LFO");
    ImGui::BeginGroup();
    Selector(engine, lfo1Index, "LFO1 WAVE", kLfoWaveforms, 4);
    ImGui::SameLine(0.0f, 16.0f);
    DependentKnob(engine, lfo1Rate, "LFO1 RATE", kLfo1RateEffectsPanelID, nullptr);
    ImGui::SameLine();
    Knob(engine, {lfo1Amplitude, "LFO1 AMT", 1.0f, Units::Percent});
    ImGui::SameLine(0.0f, 24.0f);
    Selector(engine, lfo2Index, "LFO2 WAVE", kLfoWaveforms, 4);
    ImGui::SameLine(0.0f, 16.0f);
    DependentKnob(engine, lfo2Rate, "LFO2 RATE", kLfo2RateEffectsPanelID, nullptr);
    ImGui::SameLine();
    Knob(engine, {lfo2Amplitude, "LFO2 AMT", 1.0f, Units::Percent});
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(1, 14));
    Toggle(engine, tempoSyncToArpRate, "TEMPO SYNC", ImVec2(110, 0));
    ImGui::EndGroup();
    ImGui::EndGroup();

    SectionLabel("LFO ROUTING");
    static const ModTarget kLfoTargets2[] = {
        {cutoffLFO,    "CUTOFF"},
        {resonanceLFO, "RESONANCE"},
        {oscMixLFO,    "OSC MIX"},
        {reverbMixLFO, "REVERB MIX"},
        {decayLFO,     "DECAY"},
        {noiseLFO,     "NOISE"},
        {fmLFO,        "FM"},
        {detuneLFO,    "DETUNE"},
        {filterEnvLFO, "FILTER ENV"},
        {pitchLFO,     "PITCH"},
        {bitcrushLFO,  "BITCRUSH"},
        {tremoloLFO,   "TREMOLO"},
    };
    // Two side-by-side blocks of six; both LFOs lit on one row is the DSP's
    // "LFO1+2", which averages them.
    ModMatrix(engine, kLfoTargets2,
              static_cast<int>(sizeof(kLfoTargets2) / sizeof(kLfoTargets2[0])), 2);

    SectionLabel("REVERB / DELAY");
    ImGui::BeginGroup();
    Toggle(engine, reverbOn, "REVERB", ImVec2(80, 0));               ImGui::SameLine();
    Knob(engine, {reverbFeedback, "SIZE", 1.0f, Units::Percent});    ImGui::SameLine();
    Knob(engine, {reverbHighPass, "LOW CUT", 1.0f, Units::Hertz});   ImGui::SameLine();
    Knob(engine, {reverbMix, "MIX", 1.0f, Units::Percent});
    ImGui::SameLine(0.0f, 24.0f);
    Toggle(engine, delayOn, "DELAY", ImVec2(80, 0));                 ImGui::SameLine();
    DependentKnob(engine, delayTime, "TIME", kDelayTimeEffectsPanelID, nullptr); ImGui::SameLine();
    Knob(engine, {delayFeedback, "FEEDBACK", 1.0f, Units::Percent}); ImGui::SameLine();
    Knob(engine, {delayMix, "MIX", 1.0f, Units::Percent});
    ImGui::EndGroup();

    SectionLabel("PHASER / AUTOPAN / BITCRUSH");
    ImGui::BeginGroup();
    Knob(engine, {phaserMix, "PHASE MIX", 1.0f, Units::Percent});          ImGui::SameLine();
    Knob(engine, {phaserRate, "RATE", 2.0f, Units::Hertz});                ImGui::SameLine();
    Knob(engine, {phaserFeedback, "FEEDBACK", 1.0f, Units::Percent});      ImGui::SameLine();
    Knob(engine, {phaserNotchWidth, "NOTCH", 1.0f, Units::Hertz});
    ImGui::SameLine(0.0f, 24.0f);
    Knob(engine, {autoPanAmount, "PAN AMT", 1.0f, Units::Percent});        ImGui::SameLine();
    DependentKnob(engine, autoPanFrequency, "PAN RATE", kAutoPanEffectsPanelID, nullptr);
    ImGui::SameLine(0.0f, 24.0f);
    Knob(engine, {bitCrushSampleRate, "BITCRUSH", 4.6f, Units::Hertz}, 58.0f);
    ImGui::EndGroup();
}

// ---------------------------------------------------------------------------
// Sequencer / SEQ
// ---------------------------------------------------------------------------

static void drawSequencer(s1::Engine &engine, UiState &ui) {
    SectionLabel("ARPEGGIATOR");
    ImGui::BeginGroup();
    Toggle(engine, arpIsOn, "ARP ON", ImVec2(90, 0));            ImGui::SameLine(0.0f, 16.0f);
    Toggle(engine, arpIsSequencer, "SEQUENCER", ImVec2(110, 0)); ImGui::SameLine(0.0f, 16.0f);
    Selector(engine, arpDirection, "DIRECTION", kArpDirections, 3);
    ImGui::SameLine(0.0f, 16.0f);
    Knob(engine, {arpInterval, "INTERVAL", 1.0f, Units::Index}); ImGui::SameLine();
    DependentKnob(engine, arpSeqTempoMultiplier, "TEMPO x", kArpSeqTempoMultiplierID, nullptr);
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::BeginGroup();
    Stepper(engine, arpOctave, "OCTAVES", 120.0f);      ImGui::SameLine(0.0f, 24.0f);
    Stepper(engine, arpTotalSteps, "STEPS", 120.0f);    ImGui::SameLine(0.0f, 24.0f);
    Stepper(engine, arpRate, "TEMPO (BPM)", 140.0f);
    ImGui::EndGroup();

    SectionLabel("16-STEP SEQUENCER");
    const int totalSteps = static_cast<int>(std::lround(engine.getParameter(arpTotalSteps)));
    SequencerGrid(engine, totalSteps, ui.arpBeat);
}

// ---------------------------------------------------------------------------
// Tunings / TUNE
// ---------------------------------------------------------------------------

static void drawTunings(s1::Engine &engine, UiState &ui) {
    ImGui::BeginGroup();
    SectionLabel("PITCH WHEEL");

    // One octave of the tuning table from middle C, as the iOS
    // TuningsPitchWheelView draws it: log2(f) mod 1, middle C at 12 o'clock.
    const int npo = std::min(engine.tuningNotesPerOctave(), 64);
    float frequencies[64];
    bool  playing[64];
    for (int i = 0; i < npo; ++i) {
        frequencies[i] = engine.tuningTableFrequency(60 + i);
        playing[i] = (60 + i) < 128 ? ui.heldNotes[60 + i] : false;
    }
    PitchWheel(ImVec2(230, 230), frequencies, npo, playing);

    ImGui::Spacing();
    Knob(engine, {frequencyA4, "A4 MASTER", 1.0f, Units::Hertz}, 54.0f);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(1, 22));
    ImGui::TextColored(ImColor(color::kTextDim), "notes/octave  %d", engine.tuningNotesPerOctave());
    if (ImGui::Button("Reset to 12-ET", ImVec2(150, 0))) {
        engine.setTuning12ET();
        ui.notify("tuning: 12 ET");
    }
    ImGui::EndGroup();
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 24.0f);

    ImGui::BeginGroup();
    SectionLabel("TUNING LIBRARY");

    const auto &tunings = engine.tunings();
    if (tunings.empty()) {
        ImGui::TextColored(ImColor(color::kTextDim),
                           "No tuning library loaded (linux/data/tunings.json missing).");
    } else {
        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputTextWithHint("##tsearch", "search scales", ui.tuningSearch,
                                 sizeof(ui.tuningSearch));
        ImGui::SameLine();
        if (ImGui::Button("Clear##t")) ui.tuningSearch[0] = '\0';

        std::string needle = ui.tuningSearch;
        std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);

        ImGui::BeginChild("tuninglist", ImVec2(430, 240), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < tunings.size(); ++i) {
            if (!needle.empty()) {
                std::string name = tunings[i].name;
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name.find(needle) == std::string::npos) continue;
            }
            char row[160];
            std::snprintf(row, sizeof(row), "%-3d %s  (%d)", static_cast<int>(i),
                          tunings[i].name.c_str(),
                          static_cast<int>(tunings[i].masterSet.size()));
            if (ImGui::Selectable(row, static_cast<int>(i) == engine.currentTuningIndex())) {
                engine.applyTuning(static_cast<int>(i));
                ui.notify("tuning: " + tunings[i].name);
            }
        }
        ImGui::EndChild();
        ImGui::TextColored(ImColor(color::kTextDim),
                           "%zu scales from the iOS tuning library", tunings.size());
    }
    ImGui::EndGroup();

    SectionLabel("PITCH BEND RANGE");
    Knob(engine, {pitchbendMinSemitones, "BEND DOWN", 1.0f, Units::Semitones}); ImGui::SameLine();
    Knob(engine, {pitchbendMaxSemitones, "BEND UP", 1.0f, Units::Semitones});
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void DrawPanel(Panel panel, s1::Engine &engine, UiState &ui) {
    switch (panel) {
        case Panel::Generators: drawGenerators(engine, ui); break;
        case Panel::Envelopes:  drawEnvelopes(engine, ui); break;
        case Panel::TouchPad:   drawTouchPad(engine, ui); break;
        case Panel::Effects:    drawEffects(engine, ui); break;
        case Panel::Sequencer:  drawSequencer(engine, ui); break;
        case Panel::Tunings:    drawTunings(engine, ui); break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

void DrawHeader(s1::Engine &engine, UiState &ui) {
    ImGui::BeginGroup();

    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(color::kAccent).Value);
    ImGui::Text("SYNTH ONE");
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 20.0f);

    if (ImGui::Button("<", ImVec2(30, 0))) {
        const auto presets = engine.presetsInBank(ui.currentBank);
        if (!presets.empty()) {
            int index = 0;
            for (size_t i = 0; i < presets.size(); ++i) {
                if (presets[i].position == ui.currentPreset) index = static_cast<int>(i);
            }
            index = (index - 1 + static_cast<int>(presets.size())) % static_cast<int>(presets.size());
            std::string error;
            if (engine.applyPreset(ui.currentBank, presets[index].position, error)) {
                ui.currentPreset = presets[index].position;
                ui.currentPresetName = presets[index].name;
            }
        }
    }
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImColor(color::kBackground).Value);
    char label[128];
    std::snprintf(label, sizeof(label), "%d: %s", ui.currentPreset,
                  ui.currentPresetName.empty() ? "(init)" : ui.currentPresetName.c_str());
    if (ImGui::Button(label, ImVec2(320, 0))) ui.showPresets = !ui.showPresets;
    ImGui::PopStyleColor();
    ImGui::SameLine();

    if (ImGui::Button(">", ImVec2(30, 0))) {
        const auto presets = engine.presetsInBank(ui.currentBank);
        if (!presets.empty()) {
            int index = 0;
            for (size_t i = 0; i < presets.size(); ++i) {
                if (presets[i].position == ui.currentPreset) index = static_cast<int>(i);
            }
            index = (index + 1) % static_cast<int>(presets.size());
            std::string error;
            if (engine.applyPreset(ui.currentBank, presets[index].position, error)) {
                ui.currentPreset = presets[index].position;
                ui.currentPresetName = presets[index].name;
            }
        }
    }

    ImGui::SameLine(0.0f, 20.0f);
    if (ImGui::Button(ui.showPresets ? "CLOSE PRESETS" : "PRESETS", ImVec2(130, 0))) {
        ui.showPresets = !ui.showPresets;
    }
    ImGui::SameLine(0.0f, 10.0f);
    if (ImGui::Button("SAVE", ImVec2(80, 0))) {
        std::snprintf(ui.saveName, sizeof(ui.saveName), "%s",
                      ui.currentPresetName.empty() ? "New Preset" : ui.currentPresetName.c_str());
        // Default to the next free slot in the User bank.
        int next = 0;
        for (const auto &p : engine.presetsInBank(ui.saveBank)) next = std::max(next, p.position + 1);
        ui.savePosition = next;
        ui.showSaveDialog = true;
    }

    ImGui::SameLine(0.0f, 10.0f);
    if (ImGui::Button(ui.sideBySide ? "LAYOUT: SIDE" : "LAYOUT: STACK", ImVec2(130, 0))) {
        ui.sideBySide = !ui.sideBySide;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("stacked = panels top/bottom; side = split by a vertical divider");
    }

    ImGui::SameLine(0.0f, 10.0f);
    ToggleValue(ui.showKeyboard, "KEYBOARD", ImVec2(100, 0));

    ImGui::SameLine(0.0f, 10.0f);
    const bool wasLearn = ui.midiLearnMode;
    ToggleValue(ui.midiLearnMode, "MIDI LEARN", ImVec2(120, 0));
    if (wasLearn && !ui.midiLearnMode) {
        engine.armMidiLearn(S1ParameterCount); // disarm on leaving learn mode
    }

    ImGui::SameLine(0.0f, 20.0f);
    ImGui::TextColored(ImColor(color::kTextDim), "voices %d", ui.voiceCount);
    if (ui.midiLearnMode) {
        ImGui::SameLine(0.0f, 12.0f);
        if (engine.midiLearnArmed()) {
            ImGui::TextColored(ImColor(color::kLED), "move a CC to bind '%s'",
                               engine.parameterName(engine.midiLearnTarget()).c_str());
        } else {
            ImGui::TextColored(ImColor(color::kOn), "click a knob to arm it");
        }
    }
    if (!ui.message.empty()) {
        ImGui::SameLine(0.0f, 20.0f);
        ImGui::TextColored(ImColor(color::kAccent), "%s", ui.message.c_str());
    }

    ImGui::EndGroup();
}

// ---------------------------------------------------------------------------
// Preset browser
// ---------------------------------------------------------------------------

void DrawPresetBrowser(s1::Engine &engine, UiState &ui) {
    beginPanelChild("presets", 0.0f);

    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##search", "search presets", ui.searchText,
                             sizeof(ui.searchText));
    ImGui::SameLine();
    if (ImGui::Button("Clear")) ui.searchText[0] = '\0';
    ImGui::Separator();

    std::string needle = ui.searchText;
    std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);

    ImGui::Columns(2, "presetcols");
    ImGui::SetColumnWidth(0, 220.0f);

    ImGui::TextColored(ImColor(color::kAccent), "BANKS");
    for (const auto &bank : engine.bankNames()) {
        const bool selected = (bank == ui.currentBank);
        if (ImGui::Selectable(bank.c_str(), selected)) ui.currentBank = bank;
    }

    ImGui::NextColumn();
    ImGui::TextColored(ImColor(color::kAccent), "PRESETS");
    for (const auto &preset : engine.presetsInBank(ui.currentBank)) {
        if (!needle.empty()) {
            std::string name = preset.name;
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (name.find(needle) == std::string::npos) continue;
        }
        char row[160];
        std::snprintf(row, sizeof(row), "%3d  %s", preset.position, preset.name.c_str());
        const bool selected = (preset.position == ui.currentPreset);
        if (ImGui::Selectable(row, selected)) {
            std::string error;
            if (engine.applyPreset(ui.currentBank, preset.position, error)) {
                ui.currentPreset = preset.position;
                ui.currentPresetName = preset.name;
                ui.message = "loaded " + preset.name;
                ui.messageTime = ImGui::GetTime();
            } else {
                ui.message = error;
                ui.messageTime = ImGui::GetTime();
            }
        }
    }

    ImGui::Columns(1);
    endPanelChild();
}

// ---------------------------------------------------------------------------
// Save dialog
// ---------------------------------------------------------------------------

void DrawSaveDialog(s1::Engine &engine, UiState &ui) {
    if (!ui.showSaveDialog) return;

    ImGui::OpenPopup("Save Preset");
    ImGui::SetNextWindowSize(ImVec2(420, 0));
    if (ImGui::BeginPopupModal("Save Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImColor(color::kTextDim),
                           "Writes the current synth state to your preset directory:");
        ImGui::TextWrapped("%s", engine.userDataDir().c_str());
        ImGui::Spacing();

        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText("name", ui.saveName, sizeof(ui.saveName));
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText("bank", ui.saveBank, sizeof(ui.saveBank));
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("position", &ui.savePosition);
        if (ui.savePosition < 0) ui.savePosition = 0;

        // Warn when this would replace something.
        for (const auto &p : engine.presetsInBank(ui.saveBank)) {
            if (p.position == ui.savePosition) {
                ImGui::TextColored(ImColor(color::kAccent), "overwrites '%s'", p.name.c_str());
                break;
            }
        }
        if (!engine.bankIsWritable(ui.saveBank)) {
            ImGui::TextColored(ImColor(color::kOn),
                               "'%s' is a factory bank -- a copy is made in your", ui.saveBank);
            ImGui::TextColored(ImColor(color::kOn),
                               "preset directory; the shipped one is left alone.");
        }

        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            std::string error;
            if (engine.savePreset(ui.saveBank, ui.savePosition, ui.saveName, error)) {
                ui.currentBank = ui.saveBank;
                ui.currentPreset = ui.savePosition;
                ui.currentPresetName = ui.saveName;
                ui.notify(std::string("saved '") + ui.saveName + "' to " + ui.saveBank);
            } else {
                ui.notify("save failed: " + error);
            }
            ui.showSaveDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ui.showSaveDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// Keyboard bar
// ---------------------------------------------------------------------------

void DrawKeyboardBar(s1::Engine &engine, UiState &ui) {
    ImGui::BeginGroup();

    // Wheels on the left, keys filling the rest.
    ImGui::BeginGroup();
    ImGui::TextColored(ImColor(color::kTextDim), "PITCH");
    ImGui::SetNextItemWidth(60.0f);
    if (ImGui::VSliderFloat("##bend", ImVec2(28, 74), &ui.pitchBend, 0.0f, 1.0f, "")) {
        const float lo = engine.minimum(pitchbend);
        const float hi = engine.maximum(pitchbend);
        engine.setParameter(pitchbend, lo + (hi - lo) * ui.pitchBend);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        ui.pitchBend = 0.5f; // spring back to centre
        engine.setParameter(pitchbend, engine.defaultValue(pitchbend));
    }
    ImGui::EndGroup();
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::TextColored(ImColor(color::kTextDim), "MOD");
    if (ImGui::VSliderFloat("##mod", ImVec2(28, 74), &ui.modWheel, 0.0f, 1.0f, "")) {
        engine.setParameter(lfo1Amplitude, ui.modWheel);
    }
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 14.0f);

    ImGui::BeginGroup();
    ImGui::BeginGroup();
    if (ImGui::Button("OCT -", ImVec2(56, 0))) ui.firstOctave = std::max(0, ui.firstOctave - 1);
    ImGui::SameLine();
    if (ImGui::Button("OCT +", ImVec2(56, 0))) ui.firstOctave = std::min(7, ui.firstOctave + 1);
    ImGui::SameLine();
    ToggleValue(ui.holdMode, "HOLD", ImVec2(56, 0));
    ImGui::SameLine();
    if (ImGui::Button("PANIC", ImVec2(60, 0))) {
        engine.panic();
        ui.message = "all notes off";
        ui.messageTime = ImGui::GetTime();
    }
    ImGui::SameLine();
    Stepper(engine, transpose, "TRANSPOSE", 120.0f);
    ImGui::EndGroup();

    const float width = std::max(400.0f, ImGui::GetContentRegionAvail().x - 8.0f);
    Keyboard(ImVec2(width, 96.0f), ui.firstOctave, ui.octaveCount, ui.heldNotes,
             ui.keyboardNote, ui.keyboardNotePrev);
    ImGui::EndGroup();

    ImGui::EndGroup();
}

} // namespace s1gui
