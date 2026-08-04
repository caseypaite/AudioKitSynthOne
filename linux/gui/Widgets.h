//
//  Widgets.h
//  AudioKitSynthOne - Linux port
//
//  Synth controls drawn with ImGui's draw list. These stand in for the
//  PaintCode-generated StyleKit controls in Controls/ -- same behaviour and
//  parameter semantics (range + algebraic taper), drawn natively rather than
//  from the original vector assets.
//

#pragma once

#include <string>

#include "imgui.h"

#include "Engine.h"
#include "S1Parameter.h"

namespace s1gui {

/// Synth One's palette, sampled from the app's dark UI.
namespace color {
inline constexpr ImU32 kBackground   = IM_COL32(24, 24, 28, 255);
inline constexpr ImU32 kPanel        = IM_COL32(34, 34, 40, 255);
inline constexpr ImU32 kPanelEdge    = IM_COL32(52, 52, 60, 255);
inline constexpr ImU32 kKnobBody     = IM_COL32(46, 46, 54, 255);
inline constexpr ImU32 kKnobEdge     = IM_COL32(76, 76, 88, 255);
inline constexpr ImU32 kTrack        = IM_COL32(60, 60, 70, 255);
inline constexpr ImU32 kAccent       = IM_COL32(255, 149, 0, 255);   // orange
inline constexpr ImU32 kAccentDim    = IM_COL32(150, 88, 0, 255);
inline constexpr ImU32 kOn           = IM_COL32(90, 200, 250, 255);  // cyan
inline constexpr ImU32 kOnDim        = IM_COL32(40, 96, 122, 255);
inline constexpr ImU32 kText         = IM_COL32(220, 220, 228, 255);
inline constexpr ImU32 kTextDim      = IM_COL32(140, 140, 152, 255);
inline constexpr ImU32 kLED          = IM_COL32(120, 255, 140, 255);
} // namespace color

/// How a knob's value should be rendered in its readout.
enum class Units { Raw, Hertz, Seconds, Percent, Semitones, BPM, Decibels, Index };

struct KnobSpec {
    S1Parameter parameter;
    const char *label;
    float       taper = 1.0f;   // matches Knob.taper on iOS; 1.0 == linear
    Units       units = Units::Raw;
};

/// When learn mode is on, clicking a knob arms it for the next MIDI CC instead
/// of editing it, and knobs already bound to a CC show that binding.
void SetLearnMode(bool on);
bool LearnMode();

/// Compact mode shrinks the knob face and the label/readout block above it so
/// a whole panel fits an 800x480 display without scrolling. The cell stays
/// 56px wide and that whole width is draggable, so a fingertip still lands on
/// it even though the painted circle is smaller.
void SetCompactWidgets(bool on);
bool CompactWidgets();

/// Knob diameter after compact scaling. Panels use it when they need to know
/// how tall a row will be.
float KnobDiameter(float requested = 46.0f);

/// Rotary knob bound directly to a synth parameter. Returns true if edited.
/// Drag vertically to adjust; shift for fine; double-click resets to default.
bool Knob(s1::Engine &engine, const KnobSpec &spec, float diameter = 46.0f);

/// Knob over a tempo-syncable "dependent" parameter, which works in normalised
/// [0,1] space and can be rewritten by the DSP. `payload` identifies this
/// control so the echo can be ignored.
bool DependentKnob(s1::Engine &engine, S1Parameter parameter, const char *label,
                   int payload, const char *readout, float diameter = 46.0f);

/// On/off button bound to a parameter that the DSP treats as 0/1.
bool Toggle(s1::Engine &engine, S1Parameter parameter, const char *label,
            const ImVec2 &size = ImVec2(0, 0));

/// Plain latching button not bound to a parameter.
bool ToggleValue(bool &value, const char *label, const ImVec2 &size = ImVec2(0, 0));

/// Integer stepper with -/+ ends, bound to a parameter.
bool Stepper(s1::Engine &engine, S1Parameter parameter, const char *label,
             float width = 120.0f);

/// One destination row of the LFO modulation matrix.
struct ModTarget {
    S1Parameter parameter;
    const char *label;
};

/// Modulation matrix for the LFO routing parameters.
///
/// Each routing parameter is 0=off, 1=LFO1, 2=LFO2, 3=both -- a bitmask, since
/// the DSP averages the two into lfo3 for value 3. So rather than twelve
/// identical 4-way selectors, this draws destinations down the side and one
/// cell per LFO, which is both smaller and readable at a glance: you can see
/// what a given LFO drives by scanning its column.
///
/// `blocks` splits the rows into that many side-by-side groups.
bool ModMatrix(s1::Engine &engine, const ModTarget *targets, int count, int blocks = 2);

/// Radio strip for enumerated parameters (LFO waveform, filter type, ...).
bool Selector(s1::Engine &engine, S1Parameter parameter, const char *label,
              const char *const *options, int count);

/// Draws an ADSR envelope from four parameters and lets the corner handles be
/// dragged. Returns true if edited.
bool ADSREditor(s1::Engine &engine, S1Parameter attack, S1Parameter decay,
                S1Parameter sustain, S1Parameter release, const ImVec2 &size,
                ImU32 fill);

/// 2-D pad driving two parameters. `latched` keeps the last position when
/// released, matching the iOS snap toggle being off.
bool TouchPadXY(const char *id, const ImVec2 &size, float &x, float &y,
                bool latched, const char *xLabel, const char *yLabel);

/// 16-step sequencer grid: per-step transpose, octave boost and note-on.
/// `currentStep` highlights the playing step; -1 for none.
bool SequencerGrid(s1::Engine &engine, int totalSteps, int currentStep);

/// Playable keyboard. Returns the note under the pointer while held, else -1.
/// `heldNotes` marks keys lit by MIDI or the sequencer.
void Keyboard(const ImVec2 &size, int firstOctave, int octaveCount,
              const bool *heldNotes, int &noteDown, int &notePrev);

/// Pitch wheel showing one octave of the tuning table as log2(f) mod 1,
/// middle C at twelve o'clock -- the TuningsPitchWheelView equivalent.
void PitchWheel(const ImVec2 &size, const float *frequencies, int count,
                const bool *playing);

/// Section heading with a rule, used to group controls inside a panel.
void SectionLabel(const char *text);

/// Formats a parameter value for a knob readout.
std::string FormatValue(float value, Units units);

} // namespace s1gui
