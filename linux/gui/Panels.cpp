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

// Standard knob cell footprint: the wider of the drag target and the typical
// label so neighbouring groups don't collide.
float kw() { return std::max({KnobDiameter() + 8.0f, 56.0f}); }

// Width one knob cell will actually occupy. A caption wider than the face
// widens the cell (see Knob()), so measuring with the bare kw() under-counts a
// row and centreH() then shoves it right until the last knob runs off the
// column. Keep this formula in step with Knob().
float kw(const char *label) {
    return std::max({KnobDiameter() + 8.0f, 56.0f, ImGui::CalcTextSize(label).x + 6.0f});
}

// Total width of a row of knobs placed with SameLine(), captions included.
float kwRow(std::initializer_list<const char *> labels) {
    float w = 0.0f;
    for (const char *l : labels) w += kw(l);
    if (labels.size() > 1) w += ImGui::GetStyle().ItemSpacing.x * (labels.size() - 1);
    return w;
}

// Advance the cursor so that `w` pixels of content are centred within the
// current column / available region. Call immediately before placing controls.
void centreH(float w) {
    const float off = std::max(0.0f, (ImGui::GetContentRegionAvail().x - w) * 0.5f);
    if (off > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
}

// Table flags used for every horizontal-fill row.
constexpr ImGuiTableFlags kHFill =
    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX;

// ---------------------------------------------------------------------------
// Block flow
// ---------------------------------------------------------------------------
//
// A full-width row per section wastes whatever the widest row does not use:
// MASTER needs 500px and gets 776, and the leftover is dead space on every
// other row too. Instead each functional group becomes a self-contained block
// sized to its own contents, and blocks are packed left to right, wrapping to a
// new shelf when the next one will not fit.
//
// The result is a grid that fills both dimensions: a 2x2 stack of toggles sits
// beside a row of knobs beside a stepper, all on one shelf, and a panel that
// took four full-width rows takes two shelves. Each block keeps its own
// heading and border, so grouping by function is clearer than it was with
// headings stacked down the page.

/// Padding inside a block: ImGui child padding plus room for the heading.
constexpr float kBlockGap = 4.0f;
constexpr float kBlockPadX = 6.0f;
constexpr float kBlockPadY = 2.0f;

// Heading line plus the spacing ImGui inserts after it. Getting this one pixel
// short makes the child scrollable, and a scrollbar then steals width from the
// contents and truncates the captions -- so blocks also forbid scrolling.
float blockTitleH() { return ImGui::GetTextLineHeightWithSpacing(); }

class BlockFlow {
public:
    BlockFlow() : mAvail(ImGui::GetContentRegionAvail().x), mRowW(0.0f) {}

    /// Open a block whose *content* area is contentW x contentH. Wraps to the
    /// next shelf first if this block will not fit on the current one.
    void begin(const char *title, float contentW, float contentH) {
        const float w = contentW + kBlockPadX * 2.0f;
        const float h = contentH + blockTitleH() + kBlockPadY * 2.0f;

        if (mRowW > 0.0f) {
            if (mRowW + kBlockGap + w > mAvail) {
                mRowW = 0.0f;   // does not fit: let ImGui start a new line
            } else {
                ImGui::SameLine(0.0f, kBlockGap);
                mRowW += kBlockGap;
            }
        }
        mRowW += w;

        ImGui::PushID(title);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kBlockPadX, kBlockPadY));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImColor(color::kPanel).Value);
        ImGui::BeginChild("blk", ImVec2(w, h), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextColored(ImColor(color::kAccent), "%s", title);
    }

    void end() {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::PopID();
    }

private:
    float mAvail;
    float mRowW;   // width used on the current shelf, 0 == shelf is empty
};

/// Height of a block holding `rows` rows of knobs.
float knobBlockH(int rows = 1) {
    return rows * KnobCellHeight() + (rows - 1) * ImGui::GetStyle().ItemSpacing.y;
}

/// Height of a block holding `rows` stacked frame-height widgets (buttons).
float buttonBlockH(int rows) {
    const ImGuiStyle &st = ImGui::GetStyle();
    return rows * ImGui::GetFrameHeight() + (rows - 1) * st.ItemSpacing.y;
}

/// Height of a Stepper: it draws its own caption line above the -/+ row.
float stepperBlockH() {
    return ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeight();
}

/// One grid row: tall enough for whichever of a knob, a two-button stack or a
/// stepper is biggest, so tiles on a shelf line up instead of stepping up and
/// down by a few pixels each.
float blockRowH(int rows = 1) {
    const float one = std::max({KnobCellHeight(), buttonBlockH(2), stepperBlockH()});
    return rows * one + (rows - 1) * (ImGui::GetStyle().ItemSpacing.y + blockTitleH());
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
    // MAIN is the panel you actually play with, and at 800x480 it was using
    // about three quarters of the width and two thirds of the height. Bigger
    // faces spend both: wider blocks pack the shelves fuller, taller cells use
    // the space under them.
    KnobFloor bigKnobs(72.0f);

    BlockFlow flow;

    flow.begin("OSC 1", kwRow({"MORPH 1", "SEMI 1", "VOL 1"}), blockRowH());
    Knob(engine, {index1,               "MORPH 1", 1.0f, Units::Raw});       ImGui::SameLine();
    Knob(engine, {morph1SemitoneOffset, "SEMI 1",  1.0f, Units::Semitones}); ImGui::SameLine();
    Knob(engine, {morph1Volume,         "VOL 1",   1.0f, Units::Percent});
    flow.end();

    flow.begin("OSC 2", kwRow({"MORPH 2", "SEMI 2", "DETUNE", "VOL 2"}), blockRowH());
    Knob(engine, {index2,               "MORPH 2", 1.0f, Units::Raw});       ImGui::SameLine();
    Knob(engine, {morph2SemitoneOffset, "SEMI 2",  1.0f, Units::Semitones}); ImGui::SameLine();
    Knob(engine, {morph2Detuning,       "DETUNE",  1.0f, Units::Raw});       ImGui::SameLine();
    Knob(engine, {morph2Volume,         "VOL 2",   1.0f, Units::Percent});
    flow.end();

    flow.begin("MIX", kw("MIX 1<>2"), blockRowH());
    Knob(engine, {morphBalance, "MIX 1<>2", 1.0f, Units::Percent});
    flow.end();

    flow.begin("SUB", kw("SUB") + kBlockGap + 52.0f, blockRowH());
    Knob(engine, {subVolume, "SUB", 1.0f, Units::Percent}); ImGui::SameLine(0.0f, kBlockGap);
    ImGui::BeginGroup();
    Toggle(engine, subOctaveDown, "-24", ImVec2(52, 0));
    Toggle(engine, subIsSquare,   "SQR", ImVec2(52, 0));
    ImGui::EndGroup();
    flow.end();

    flow.begin("FM", kwRow({"FM MIX", "FM MOD"}), blockRowH());
    Knob(engine, {fmVolume, "FM MIX", 1.0f, Units::Percent}); ImGui::SameLine();
    Knob(engine, {fmAmount, "FM MOD", 1.0f, Units::Raw});
    flow.end();

    flow.begin("NOISE", kw("NOISE"), blockRowH());
    Knob(engine, {noiseVolume, "NOISE", 1.0f, Units::Percent});
    flow.end();

    flow.begin("FILTER", kwRow({"CUTOFF", "RES"}) + kBlockGap + 90.0f, blockRowH());
    Knob(engine, {cutoff,    "CUTOFF", 2.0f, Units::Hertz});  ImGui::SameLine();
    Knob(engine, {resonance, "RES",    1.0f, Units::Percent}); ImGui::SameLine(0.0f, kBlockGap);
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(1, 16));
    Selector(engine, filterType, "TYPE", kFilterTypes, 3);
    ImGui::EndGroup();
    flow.end();

    flow.begin("MASTER", kwRow({"VOLUME", "GLIDE"}), blockRowH());
    Knob(engine, {masterVolume, "VOLUME", 2.0f, Units::Percent}); ImGui::SameLine();
    Knob(engine, {glide,        "GLIDE",  2.0f, Units::Seconds});
    flow.end();

    // The last shelf holds only three blocks and had about 290px going spare.
    // Spend it on the targets rather than leaving it blank: these toggles and
    // the tempo stepper are the widgets you poke rather than twist, and they
    // were the smallest things on the panel.
    const float voiceW = 96.0f;
    flow.begin("VOICE", voiceW * 2.0f + kBlockGap, blockRowH());
    ImGui::BeginGroup();
    Toggle(engine, isMono,       "MONO",   ImVec2(voiceW, 0));
    Toggle(engine, monoIsLegato, "LEGATO", ImVec2(voiceW, 0));
    ImGui::EndGroup(); ImGui::SameLine(0.0f, kBlockGap);
    ImGui::BeginGroup();
    Toggle(engine, widen,              "WIDEN",   ImVec2(voiceW, 0));
    Toggle(engine, oscBandlimitEnable, "BANDLIM", ImVec2(voiceW, 0));
    ImGui::EndGroup();
    flow.end();

    flow.begin("TEMPO (BPM)", 240.0f, blockRowH());
    Stepper(engine, arpRate, "", 240.0f);
    flow.end();
}

// ---------------------------------------------------------------------------
// Envelopes / ENV
// ---------------------------------------------------------------------------

static void drawEnvelopes(s1::Engine &engine, UiState &) {
    BlockFlow flow;

    // Each envelope is one block: its curve editor with the four knobs that
    // drive it directly underneath, so the graph and its controls read as one
    // thing instead of two stacked sections.
    const float adsrKnobs = kwRow({"ATTACK", "DECAY", "SUSTAIN", "RELEASE"});
    const float graphH = 96.0f;
    const float envH = graphH + ImGui::GetStyle().ItemSpacing.y + KnobCellHeight();

    flow.begin("AMPLITUDE", adsrKnobs, envH);
    ADSREditor(engine, attackDuration, decayDuration, sustainLevel, releaseDuration,
               ImVec2(adsrKnobs, graphH), color::kAccent);
    Knob(engine, {attackDuration,  "ATTACK",  1.0f, Units::Seconds}); ImGui::SameLine();
    Knob(engine, {decayDuration,   "DECAY",   1.0f, Units::Seconds}); ImGui::SameLine();
    Knob(engine, {sustainLevel,    "SUSTAIN", 1.0f, Units::Percent}); ImGui::SameLine();
    Knob(engine, {releaseDuration, "RELEASE", 1.0f, Units::Seconds});
    flow.end();

    flow.begin("FILTER", adsrKnobs, envH);
    ADSREditor(engine, filterAttackDuration, filterDecayDuration, filterSustainLevel,
               filterReleaseDuration, ImVec2(adsrKnobs, graphH), color::kOn);
    Knob(engine, {filterAttackDuration,  "ATTACK",  1.0f, Units::Seconds}); ImGui::SameLine();
    Knob(engine, {filterDecayDuration,   "DECAY",   1.0f, Units::Seconds}); ImGui::SameLine();
    Knob(engine, {filterSustainLevel,    "SUSTAIN", 1.0f, Units::Percent}); ImGui::SameLine();
    Knob(engine, {filterReleaseDuration, "RELEASE", 1.0f, Units::Seconds});
    flow.end();

    flow.begin("ENV AMOUNT", kwRow({"FILTER ENV", "PITCH TRACK"}), knobBlockH());
    Knob(engine, {filterADSRMix,     "FILTER ENV",  1.0f, Units::Percent}); ImGui::SameLine();
    Knob(engine, {adsrPitchTracking, "PITCH TRACK", 3.0f, Units::Percent});
    flow.end();
}

// ---------------------------------------------------------------------------
// TouchPad / PAD
// ---------------------------------------------------------------------------

static void drawTouchPad(s1::Engine &engine, UiState &ui) {
    BlockFlow flow;

    // The pads are the one thing here worth spending space on, so they take
    // what the shelf will give them rather than a fixed 300px.
    const float padW = std::min(300.0f, (ImGui::GetContentRegionAvail().x -
                                         kBlockGap * 3.0f - kBlockPadX * 6.0f) * 0.5f);
    const float padH = 232.0f;

    flow.begin("PAD 1  -  LFO1 RATE / AMOUNT", padW, padH);
    if (TouchPadXY("pad1", ImVec2(padW, padH), ui.pad1X, ui.pad1Y, ui.padLatch,
                   "LFO1 RATE", "LFO1 AMT")) {
        engine.setDependentParameter(lfo1Rate, ui.pad1X, kLfo1RateTouchPadID);
        engine.setParameter(lfo1Amplitude, ui.pad1Y);
    }
    flow.end();

    flow.begin("PAD 2  -  CUTOFF / RESONANCE", padW, padH);
    if (TouchPadXY("pad2", ImVec2(padW, padH), ui.pad2X, ui.pad2Y, ui.padLatch,
                   "CUTOFF", "RESONANCE")) {
        engine.setParameter(cutoff, engine.positionToValue(cutoff, ui.pad2X, 2.0f));
        // iOS scales the vertical axis so the top of the pad stops short of
        // self-oscillation; resonance maxes at 0.98 in the parameter table.
        engine.setParameter(resonance, engine.positionToValue(resonance, ui.pad2Y, 1.0f));
    }
    flow.end();

    flow.begin("LATCH", 120.0f, buttonBlockH(1));
    ToggleValue(ui.padLatch, ui.padLatch ? "ON" : "OFF", ImVec2(120, 0));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("with latch off the pad snaps back on release");
    flow.end();
}

// ---------------------------------------------------------------------------
// Effects / FX
// ---------------------------------------------------------------------------

static void drawEffects(s1::Engine &engine, UiState &ui) {
    BlockFlow flow;

    // Selector width: four waveform buttons plus their caption line.
    const float selW = 150.0f;

    flow.begin("LFO 1", selW + kBlockGap + kwRow({"RATE", "AMT"}), knobBlockH());
    Selector(engine, lfo1Index, "WAVE", kLfoWaveforms, 4); ImGui::SameLine(0.0f, kBlockGap);
    DependentKnob(engine, lfo1Rate, "RATE", kLfo1RateEffectsPanelID, nullptr);
    ImGui::SameLine();
    Knob(engine, {lfo1Amplitude, "AMT", 1.0f, Units::Percent});
    flow.end();

    flow.begin("LFO 2", selW + kBlockGap + kwRow({"RATE", "AMT"}), knobBlockH());
    Selector(engine, lfo2Index, "WAVE", kLfoWaveforms, 4); ImGui::SameLine(0.0f, kBlockGap);
    DependentKnob(engine, lfo2Rate, "RATE", kLfo2RateEffectsPanelID, nullptr);
    ImGui::SameLine();
    Knob(engine, {lfo2Amplitude, "AMT", 1.0f, Units::Percent});
    flow.end();

    flow.begin("SYNC", 92.0f, knobBlockH());
    Toggle(engine, tempoSyncToArpRate, "TEMPO", ImVec2(92, 0));
    flow.end();

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
    // Four blocks of three: the matrix is the tallest thing on this panel, and
    // this shape spends width -- which the shelf has -- instead of height.
    const int kRoutingRows = 3;
    const float routingH = ImGui::GetTextLineHeightWithSpacing() +
                           kRoutingRows * ImGui::GetFrameHeightWithSpacing();
    flow.begin("LFO ROUTING", 640.0f, routingH);
    ModMatrix(engine, kLfoTargets2,
              static_cast<int>(sizeof(kLfoTargets2) / sizeof(kLfoTargets2[0])), 4);
    flow.end();

    flow.begin("REVERB", 80.0f + kBlockGap + kwRow({"SIZE", "LOW CUT", "MIX"}), knobBlockH());
    Toggle(engine, reverbOn, "ON", ImVec2(80, 0)); ImGui::SameLine(0.0f, kBlockGap);
    Knob(engine, {reverbFeedback, "SIZE",    1.0f, Units::Percent}); ImGui::SameLine();
    Knob(engine, {reverbHighPass, "LOW CUT", 1.0f, Units::Hertz});   ImGui::SameLine();
    Knob(engine, {reverbMix,      "MIX",     1.0f, Units::Percent});
    flow.end();

    flow.begin("DELAY", 80.0f + kBlockGap + kwRow({"TIME", "FEEDBACK", "MIX"}), knobBlockH());
    Toggle(engine, delayOn, "ON", ImVec2(80, 0)); ImGui::SameLine(0.0f, kBlockGap);
    DependentKnob(engine, delayTime, "TIME", kDelayTimeEffectsPanelID, nullptr); ImGui::SameLine();
    Knob(engine, {delayFeedback, "FEEDBACK", 1.0f, Units::Percent}); ImGui::SameLine();
    Knob(engine, {delayMix,      "MIX",      1.0f, Units::Percent});
    flow.end();

    flow.begin("PHASER", kwRow({"PHASE MIX", "RATE", "FEEDBACK", "NOTCH"}), knobBlockH());
    Knob(engine, {phaserMix,       "PHASE MIX", 1.0f, Units::Percent}); ImGui::SameLine();
    Knob(engine, {phaserRate,      "RATE",      2.0f, Units::Hertz});   ImGui::SameLine();
    Knob(engine, {phaserFeedback,  "FEEDBACK",  1.0f, Units::Percent}); ImGui::SameLine();
    Knob(engine, {phaserNotchWidth,"NOTCH",     1.0f, Units::Hertz});
    flow.end();

    flow.begin("AUTOPAN", kwRow({"PAN AMT", "PAN RATE"}), knobBlockH());
    Knob(engine, {autoPanAmount, "PAN AMT", 1.0f, Units::Percent}); ImGui::SameLine();
    DependentKnob(engine, autoPanFrequency, "PAN RATE", kAutoPanEffectsPanelID, nullptr);
    flow.end();

    flow.begin("BITCRUSH", kw("RATE"), knobBlockH());
    Knob(engine, {bitCrushSampleRate, "RATE", 4.6f, Units::Hertz});
    flow.end();
}

// ---------------------------------------------------------------------------
// Sequencer / SEQ
// ---------------------------------------------------------------------------

static void drawSequencer(s1::Engine &engine, UiState &ui) {
    BlockFlow flow;

    flow.begin("ARP", 90.0f, blockRowH());
    ImGui::BeginGroup();
    Toggle(engine, arpIsOn,        "ARP ON",    ImVec2(90, 0));
    Toggle(engine, arpIsSequencer, "SEQUENCER", ImVec2(90, 0));
    ImGui::EndGroup();
    flow.end();

    flow.begin("DIRECTION", 150.0f, blockRowH());
    Selector(engine, arpDirection, "", kArpDirections, 3);
    flow.end();

    flow.begin("RATE", kwRow({"INTERVAL", "TEMPO x"}), blockRowH());
    Knob(engine, {arpInterval, "INTERVAL", 1.0f, Units::Index}); ImGui::SameLine();
    DependentKnob(engine, arpSeqTempoMultiplier, "TEMPO x", kArpSeqTempoMultiplierID, nullptr);
    flow.end();

    flow.begin("OCTAVES", 96.0f, blockRowH());
    Stepper(engine, arpOctave, "", 96.0f);
    flow.end();

    flow.begin("STEPS", 96.0f, blockRowH());
    Stepper(engine, arpTotalSteps, "", 96.0f);
    flow.end();

    flow.begin("TEMPO (BPM)", 120.0f, blockRowH());
    Stepper(engine, arpRate, "", 120.0f);
    flow.end();

    // The grid is 16 steps wide whatever else happens, so it gets its own
    // shelf and the width of the panel.
    const float gridW = ImGui::GetContentRegionAvail().x - kBlockPadX * 2.0f - 2.0f;
    // "STEP" caption, the beat-dot strip, then the transpose / 8va / on rows.
    const float gridH = ImGui::GetTextLineHeightWithSpacing() + 13.0f +
                        3.0f * ImGui::GetFrameHeightWithSpacing();
    flow.begin("16-STEP SEQUENCER", gridW, gridH);
    const int totalSteps = static_cast<int>(std::lround(engine.getParameter(arpTotalSteps)));
    SequencerGrid(engine, totalSteps, ui.arpBeat);
    flow.end();
}

// ---------------------------------------------------------------------------
// Tunings / TUNE
// ---------------------------------------------------------------------------

static void drawTunings(s1::Engine &engine, UiState &ui) {
    // TUNE carries only three knobs and finishes well above the fold at
    // 800x480, so it does not need the 32px compact face that lets the busy
    // panels fit. Give its knobs back a proper touch target -- A4 and the two
    // bend ranges are exactly the controls you want to nudge by hand.
    KnobFloor bigKnobs(64.0f);

    BlockFlow flow;

    // One octave of the tuning table from middle C, as the iOS
    // TuningsPitchWheelView draws it: log2(f) mod 1, middle C at 12 o'clock.
    const int npo = std::min(engine.tuningNotesPerOctave(), 64);
    float frequencies[64];
    bool  playing[64];
    for (int i = 0; i < npo; ++i) {
        frequencies[i] = engine.tuningTableFrequency(60 + i);
        playing[i] = (60 + i) < 128 ? ui.heldNotes[60 + i] : false;
    }

    const float wheel = ui.compact ? 170.0f : 230.0f;
    flow.begin("PITCH WHEEL", wheel, wheel);
    PitchWheel(ImVec2(wheel, wheel), frequencies, npo, playing);
    flow.end();

    // Scale list: takes the rest of the shelf beside the wheel, and matches its
    // height so the two read as one row.
    const float listW = std::max(280.0f, ImGui::GetContentRegionAvail().x -
                                             wheel - kBlockGap * 2.0f - kBlockPadX * 4.0f - 6.0f);
    flow.begin("TUNING LIBRARY", listW, wheel);
    const auto &tunings = engine.tunings();
    if (tunings.empty()) {
        ImGui::TextColored(ImColor(color::kTextDim),
                           "No tuning library loaded (linux/data/tunings.json missing).");
    } else {
        ImGui::SetNextItemWidth(listW - 64.0f);
        ImGui::InputTextWithHint("##tsearch", "search scales", ui.tuningSearch,
                                 sizeof(ui.tuningSearch));
        ImGui::SameLine();
        if (ImGui::Button("Clear##t")) ui.tuningSearch[0] = '\0';

        std::string needle = ui.tuningSearch;
        std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);

        ImGui::BeginChild("tuninglist", ImVec2(listW, wheel - ImGui::GetFrameHeightWithSpacing()),
                          ImGuiChildFlags_Borders);
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
    }
    flow.end();

    flow.begin("MASTER TUNING", kw("A4") + kBlockGap + 150.0f, blockRowH());
    Knob(engine, {frequencyA4, "A4", 1.0f, Units::Hertz}); ImGui::SameLine(0.0f, kBlockGap);
    ImGui::BeginGroup();
    ImGui::TextColored(ImColor(color::kTextDim), "notes/oct %d", engine.tuningNotesPerOctave());
    if (ImGui::Button("Reset to 12-ET", ImVec2(150, 0))) {
        engine.setTuning12ET();
        ui.notify("tuning: 12 ET");
    }
    ImGui::EndGroup();
    flow.end();

    flow.begin("PITCH BEND", kwRow({"DOWN", "UP"}), blockRowH());
    Knob(engine, {pitchbendMinSemitones, "DOWN", 1.0f, Units::Semitones}); ImGui::SameLine();
    Knob(engine, {pitchbendMaxSemitones, "UP",   1.0f, Units::Semitones});
    flow.end();
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

    // Laid end to end the roomy header needs about 1200px. That is half again
    // as wide as the Pi's 7" panel, and ImGui does not wrap a SameLine run, so
    // on a narrow display it would simply run off the right edge taking the
    // keyboard and MIDI-learn toggles with it. Compact splits it over two rows,
    // drops the wordmark, shortens the labels and lets the preset button take
    // whatever width is left.
    const bool wide = !ui.compact;
    const float gapBig = wide ? 20.0f : 8.0f;
    const float gapSmall = wide ? 10.0f : 6.0f;
    const float stepW = wide ? 30.0f : 34.0f;   // touch: no smaller when compact

    if (wide) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImColor(color::kAccent).Value);
        ImGui::Text("SYNTH ONE");
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, gapBig);
    }

    if (ImGui::Button("<", ImVec2(stepW, 0))) {
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
    // Compact gives the preset name whatever is left after the two step
    // buttons, so it stays readable at any width instead of being cut off.
    const float presetW =
        wide ? 320.0f
             : std::max(120.0f, ImGui::GetContentRegionAvail().x - stepW -
                                    ImGui::GetStyle().ItemSpacing.x * 2.0f);
    if (ImGui::Button(label, ImVec2(presetW, 0))) ui.showPresets = !ui.showPresets;
    ImGui::PopStyleColor();
    ImGui::SameLine();

    if (ImGui::Button(">", ImVec2(stepW, 0))) {
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

    // Compact starts a second row here instead of continuing off the edge.
    if (wide) ImGui::SameLine(0.0f, gapBig);

    if (ImGui::Button(ui.showPresets ? (wide ? "CLOSE PRESETS" : "CLOSE") : "PRESETS",
                      ImVec2(wide ? 130.0f : 92.0f, 0))) {
        ui.showPresets = !ui.showPresets;
    }
    ImGui::SameLine(0.0f, gapSmall);
    if (ImGui::Button("SAVE", ImVec2(wide ? 80.0f : 64.0f, 0))) {
        std::snprintf(ui.saveName, sizeof(ui.saveName), "%s",
                      ui.currentPresetName.empty() ? "New Preset" : ui.currentPresetName.c_str());
        // Default to the next free slot in the User bank.
        int next = 0;
        for (const auto &p : engine.presetsInBank(ui.saveBank)) next = std::max(next, p.position + 1);
        ui.savePosition = next;
        ui.showSaveDialog = true;
    }

    // Layout toggle: SINGLE (default) <-> STACK. Side-by-side is removed from
    // the interactive cycle; --layout side on the CLI still works if needed.
    if (wide) {
        ImGui::SameLine(0.0f, gapSmall);
        const char *layoutLabel = ui.singlePanel ? "LAYOUT: SINGLE" : "LAYOUT: STACK";
        if (ImGui::Button(layoutLabel, ImVec2(120.0f, 0))) {
            ui.singlePanel = !ui.singlePanel;
            if (ui.singlePanel) ui.sideBySide = false;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("SINGLE: one panel at a time (default)\n"
                              "STACK: two panels top/bottom");
        }
    }

    ImGui::SameLine(0.0f, gapSmall);
    ToggleValue(ui.showKeyboard, wide ? "KEYBOARD" : "KEYS", ImVec2(wide ? 100.0f : 64.0f, 0));

    ImGui::SameLine(0.0f, gapSmall);
    const bool wasLearn = ui.midiLearnMode;
    ToggleValue(ui.midiLearnMode, wide ? "MIDI LEARN" : "LEARN", ImVec2(wide ? 120.0f : 76.0f, 0));
    if (wasLearn && !ui.midiLearnMode) {
        engine.armMidiLearn(S1ParameterCount); // disarm on leaving learn mode
    }

    // Row two is set by row one's full-width preset button, not by its own
    // contents, so there is room for the whole word even when compact.
    ImGui::SameLine(0.0f, gapBig);
    ImGui::TextColored(ImColor(color::kTextDim), "voices %d", ui.voiceCount);
    // Status text is transient and variable-length. Compact gives it its own
    // line rather than letting it push the row past the edge.
    if (ui.midiLearnMode) {
        if (wide) ImGui::SameLine(0.0f, 12.0f);
        if (engine.midiLearnArmed()) {
            ImGui::TextColored(ImColor(color::kLED), "move a CC to bind '%s'",
                               engine.parameterName(engine.midiLearnTarget()).c_str());
        } else {
            ImGui::TextColored(ImColor(color::kOn), "click a knob to arm it");
        }
    }
    if (!ui.message.empty()) {
        if (wide || !ui.midiLearnMode) ImGui::SameLine(0.0f, wide ? 20.0f : 8.0f);
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
