//
//  Widgets.cpp
//  AudioKitSynthOne - Linux port
//

#include "Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace s1gui {

namespace {

constexpr float kTwoPi = 6.28318530718f;
// Knob sweep: 270 degrees, opening downward like the iOS control.
constexpr float kStartAngle = 2.35619449f;  // 135 deg
constexpr float kSweep = 4.71238898f;       // 270 deg

void drawKnobFace(ImDrawList *dl, const ImVec2 &centre, float radius, float position,
                  bool active, ImU32 accent) {
    const float a0 = kStartAngle;
    const float a1 = kStartAngle + kSweep;

    dl->PathArcTo(centre, radius, a0, a1, 48);
    dl->PathStroke(color::kTrack, 0, 3.0f);

    if (position > 0.0f) {
        dl->PathArcTo(centre, radius, a0, a0 + kSweep * position, 48);
        dl->PathStroke(accent, 0, 3.0f);
    }

    dl->AddCircleFilled(centre, radius - 4.0f, color::kKnobBody, 32);
    dl->AddCircle(centre, radius - 4.0f, active ? accent : color::kKnobEdge, 32, 1.0f);

    const float angle = a0 + kSweep * position;
    const ImVec2 tip(centre.x + std::cos(angle) * (radius - 6.0f),
                     centre.y + std::sin(angle) * (radius - 6.0f));
    const ImVec2 root(centre.x + std::cos(angle) * (radius * 0.35f),
                      centre.y + std::sin(angle) * (radius * 0.35f));
    dl->AddLine(root, tip, active ? accent : color::kText, 2.0f);
}

/// Shared drag behaviour for the rotary controls. Returns true when `position`
/// changed; `reset` is set on a double-click.
bool knobBehaviour(const char *id, const ImVec2 &size, float &position, bool &reset) {
    ImGui::InvisibleButton(id, size);
    reset = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    bool changed = false;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImGuiIO &io = ImGui::GetIO();
        // Vertical drag; 200 px covers the full sweep, 4x finer with shift.
        const float scale = io.KeyShift ? 800.0f : 200.0f;
        const float delta = -io.MouseDelta.y / scale;
        if (delta != 0.0f) {
            position = std::clamp(position + delta, 0.0f, 1.0f);
            changed = true;
        }
    }
    return changed;
}

bool gLearnMode = false;
bool gCompactWidgets = false;
// Smallest compact knob face. Panels with room to spare raise it; see
// SetCompactKnobFloor.
float gCompactKnobFloor = 32.0f;

/// Height of the label + readout block above a knob face.
float knobLabelBlock() { return gCompactWidgets ? 20.0f : 28.0f; }

void labelledReadout(const ImVec2 &topLeft, float width, const char *label,
                     const std::string &readout, bool hovered) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const bool showValue = hovered && !readout.empty();

    // Roomy stacks the value under the caption. Compact has only one line to
    // spend, so the value takes the caption's place while the knob is being
    // touched: at the moment you are turning something you know which control
    // it is, and the number is what you need to see.
    if (gCompactWidgets && showValue) {
        const ImVec2 valueSize = ImGui::CalcTextSize(readout.c_str());
        dl->AddText(ImVec2(topLeft.x + (width - valueSize.x) * 0.5f, topLeft.y),
                    color::kAccent, readout.c_str());
        return;
    }

    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(topLeft.x + (width - labelSize.x) * 0.5f, topLeft.y),
                color::kTextDim, label);

    if (showValue) {
        const ImVec2 valueSize = ImGui::CalcTextSize(readout.c_str());
        dl->AddText(ImVec2(topLeft.x + (width - valueSize.x) * 0.5f, topLeft.y + 14.0f),
                    color::kAccent, readout.c_str());
    }
}

} // namespace

void SetLearnMode(bool on) { gLearnMode = on; }
bool LearnMode() { return gLearnMode; }

void SetCompactWidgets(bool on) { gCompactWidgets = on; }
bool CompactWidgets() { return gCompactWidgets; }

// 0.70 takes the standard 46px face to 32px and the emphasis 58px to 41px,
// which is what lets the busiest panel (FX: four sections, thirty controls)
// finish above the fold. The floor stops a knob becoming a dot on some future
// smaller display.
float KnobCellHeight(float requested) {
    // The caption block and the face are two separate items inside the knob's
    // group, so ImGui inserts ItemSpacing.y between them. Leaving that out made
    // every block a few pixels short and clipped the bottom of the circles --
    // 3px when compact, 7px on the desktop spacing.
    return knobLabelBlock() + ImGui::GetStyle().ItemSpacing.y + KnobDiameter(requested);
}

float KnobDiameter(float requested) {
    // Compact also drops the emphasis size: a few panels ask for 58px to
    // highlight CUTOFF, VOLUME and the like, and that one oversized knob sets
    // the height of its whole row. Uniform cells buy a row back on FX, and the
    // emphasis reads poorly at this scale anyway.
    if (!gCompactWidgets) return requested;
    return std::max(gCompactKnobFloor, std::min(requested, 46.0f) * 0.70f);
}

void SetCompactKnobFloor(float px) { gCompactKnobFloor = px; }
float CompactKnobFloor() { return gCompactKnobFloor; }

std::string FormatValue(float value, Units units) {
    char buf[64];
    switch (units) {
        case Units::Hertz:
            if (value >= 1000.0f) std::snprintf(buf, sizeof(buf), "%.2f kHz", value / 1000.0f);
            else                  std::snprintf(buf, sizeof(buf), "%.1f Hz", value);
            break;
        case Units::Seconds:
            if (value < 1.0f) std::snprintf(buf, sizeof(buf), "%.0f ms", value * 1000.0f);
            else              std::snprintf(buf, sizeof(buf), "%.2f s", value);
            break;
        case Units::Percent:   std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0f); break;
        case Units::Semitones: std::snprintf(buf, sizeof(buf), "%+.2f st", value); break;
        case Units::BPM:       std::snprintf(buf, sizeof(buf), "%.1f BPM", value); break;
        case Units::Decibels:  std::snprintf(buf, sizeof(buf), "%.1f dB", value); break;
        case Units::Index:     std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(value)); break;
        case Units::Raw:
        default:               std::snprintf(buf, sizeof(buf), "%.3f", value); break;
    }
    return buf;
}

bool Knob(s1::Engine &engine, const KnobSpec &spec, float requested) {
    ImGui::PushID(static_cast<int>(spec.parameter));

    const float diameter = KnobDiameter(requested);
    // The cell keeps at least 56px whatever the face does: that width is the
    // drag target, so the control stays finger-sized while the paint shrinks.
    // It also never goes narrower than its own caption, or neighbouring labels
    // run into each other once the knobs are small.
    const float width = std::max({diameter + 8.0f, 56.0f,
                                  ImGui::CalcTextSize(spec.label).x + 6.0f});
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(width, knobLabelBlock())); // room for label + readout

    const ImVec2 knobTopLeft = ImGui::GetCursorScreenPos();
    const float radius = diameter * 0.5f;
    const ImVec2 centre(knobTopLeft.x + width * 0.5f, knobTopLeft.y + radius);

    const float value = engine.getParameter(spec.parameter);
    float position = engine.valueToPosition(spec.parameter, value, spec.taper);

    bool reset = false;
    const bool dragged = knobBehaviour("knob", ImVec2(width, diameter), position, reset);
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered() || active;

    bool changed = false;
    const int boundCc = gLearnMode ? engine.ccForParameter(spec.parameter) : -1;

    if (gLearnMode) {
        // In learn mode a click arms the parameter; right-click clears it.
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            engine.setMidiLearnTaper(spec.parameter, spec.taper);
            engine.armMidiLearn(spec.parameter);
        } else if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            engine.clearMidiLearn(spec.parameter);
        }
    } else if (reset) {
        engine.setParameter(spec.parameter, engine.defaultValue(spec.parameter));
        changed = true;
    } else if (dragged) {
        engine.setParameter(spec.parameter,
                            engine.positionToValue(spec.parameter, position, spec.taper));
        changed = true;
    }

    const float shown = engine.valueToPosition(
        spec.parameter, engine.getParameter(spec.parameter), spec.taper);
    const bool armed = gLearnMode && engine.midiLearnArmed() &&
                       engine.midiLearnTarget() == spec.parameter;
    drawKnobFace(ImGui::GetWindowDrawList(), centre, radius, shown, active || armed,
                 armed ? color::kLED : (boundCc >= 0 ? color::kOn : color::kAccent));

    std::string readout = FormatValue(engine.getParameter(spec.parameter), spec.units);
    if (gLearnMode) {
        char buf[32];
        if (armed)            std::snprintf(buf, sizeof(buf), "learn...");
        else if (boundCc >= 0) std::snprintf(buf, sizeof(buf), "CC %d", boundCc);
        else                   std::snprintf(buf, sizeof(buf), "--");
        readout = buf;
    }
    labelledReadout(origin, width, spec.label, readout, hovered || gLearnMode);
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

bool DependentKnob(s1::Engine &engine, S1Parameter parameter, const char *label, int payload,
                   const char *readout, float requested, Units units) {
    ImGui::PushID(static_cast<int>(parameter) + 10000);

    const float diameter = KnobDiameter(requested);
    const float width = std::max({diameter + 8.0f, 56.0f,
                                  ImGui::CalcTextSize(label).x + 6.0f});
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(width, knobLabelBlock()));

    const ImVec2 knobTopLeft = ImGui::GetCursorScreenPos();
    const float radius = diameter * 0.5f;
    const ImVec2 centre(knobTopLeft.x + width * 0.5f, knobTopLeft.y + radius);

    // With tempo sync on, the kernel snaps these to discrete divisions: ask for
    // 0.25 and it stores 0.222. Dragging from the snapped value each frame made
    // the knob unusable -- a slow drag adds ~0.01 per frame, gets snapped back
    // to where it started, and the control never moves however long you pull.
    // Only a fast flick crossed a step boundary.
    //
    // So carry the un-snapped position across frames for as long as the drag
    // lasts, and re-sync to the engine once it is let go.
    ImGuiStorage *store = ImGui::GetStateStorage();
    const ImGuiID dragKey = ImGui::GetID("depdragging");
    const ImGuiID posKey  = ImGui::GetID("depdragpos");
    const float engineValue = engine.getDependentParameter(parameter);
    const bool wasDragging = store->GetBool(dragKey, false);
    float position = wasDragging ? store->GetFloat(posKey, engineValue) : engineValue;

    bool reset = false;
    const bool dragged = knobBehaviour("dep", ImVec2(width, diameter), position, reset);
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered() || active;

    store->SetBool(dragKey, active);
    if (active) store->SetFloat(posKey, position);

    bool changed = false;
    const int boundCc = gLearnMode ? engine.ccForParameter(parameter) : -1;

    // These behave like any other knob, which they previously did not: learn
    // mode used to fall through to the drag, so clicking a tempo-syncable knob
    // to bind it moved the value instead of arming it, and a double-click did
    // nothing because `reset` was computed and then ignored.
    if (gLearnMode) {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            engine.setMidiLearnTaper(parameter, 1.0f);
            engine.armMidiLearn(parameter);
        } else if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            engine.clearMidiLearn(parameter);
        }
    } else if (reset) {
        // The plain setter is right here: setSynthParameter routes dependent
        // parameters through the kernel's rate helper, so the tempo-synced
        // state follows.
        engine.setParameter(parameter, engine.defaultValue(parameter));
        changed = true;
    } else if (dragged) {
        engine.setDependentParameter(parameter, position, payload);
        changed = true;
    }

    const bool armed = gLearnMode && engine.midiLearnArmed() &&
                       engine.midiLearnTarget() == parameter;
    // Cyan stays the tempo-syncable knob's identity; armed shows as the learn
    // colour, and the binding itself is reported in the readout.
    drawKnobFace(ImGui::GetWindowDrawList(), centre, radius,
                 engine.getDependentParameter(parameter), active || armed,
                 armed ? color::kLED : color::kOn);

    // Without this these knobs were the only ones that never showed a value:
    // every caller passes nullptr, which read as "no readout" rather than
    // "derive one".
    std::string text = readout ? std::string(readout)
                               : FormatValue(engine.getParameter(parameter), units);
    if (gLearnMode) {
        char buf[32];
        if (armed)             std::snprintf(buf, sizeof(buf), "learn...");
        else if (boundCc >= 0) std::snprintf(buf, sizeof(buf), "CC %d", boundCc);
        else                   std::snprintf(buf, sizeof(buf), "--");
        text = buf;
    }
    labelledReadout(origin, width, label, text, hovered || gLearnMode);
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

bool Toggle(s1::Engine &engine, S1Parameter parameter, const char *label, const ImVec2 &size) {
    const bool on = engine.getParameter(parameter) > 0.5f;

    ImGui::PushStyleColor(ImGuiCol_Button, on ? color::kOnDim : color::kPanel);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? color::kOn : color::kPanelEdge);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color::kOn);
    ImGui::PushStyleColor(ImGuiCol_Text, on ? color::kText : color::kTextDim);

    ImGui::PushID(static_cast<int>(parameter));
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopID();
    ImGui::PopStyleColor(4);

    if (clicked) {
        engine.setParameter(parameter, on ? 0.0f : 1.0f);
    }
    return clicked;
}

bool ToggleValue(bool &value, const char *label, const ImVec2 &size) {
    ImGui::PushStyleColor(ImGuiCol_Button, value ? color::kOnDim : color::kPanel);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, value ? color::kOn : color::kPanelEdge);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color::kOn);
    ImGui::PushStyleColor(ImGuiCol_Text, value ? color::kText : color::kTextDim);
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    if (clicked) value = !value;
    return clicked;
}

bool Stepper(s1::Engine &engine, S1Parameter parameter, const char *label, float width) {
    ImGui::PushID(static_cast<int>(parameter));
    ImGui::BeginGroup();

    ImGui::TextColored(ImColor(color::kTextDim), "%s", label);

    const float lo = engine.minimum(parameter);
    const float hi = engine.maximum(parameter);
    float value = engine.getParameter(parameter);

    bool changed = false;
    const float buttonWidth = 24.0f;
    if (ImGui::Button("-", ImVec2(buttonWidth, 0))) {
        value = std::max(lo, value - 1.0f);
        changed = true;
    }
    ImGui::SameLine();

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(value)));
    const float textWidth = std::max(28.0f, width - 2.0f * buttonWidth - 16.0f);
    const ImVec2 textSize = ImGui::CalcTextSize(buf);
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(cursor.x + (textWidth - textSize.x) * 0.5f, cursor.y + 3.0f),
        color::kAccent, buf);
    ImGui::Dummy(ImVec2(textWidth, ImGui::GetFrameHeight()));
    ImGui::SameLine();

    if (ImGui::Button("+", ImVec2(buttonWidth, 0))) {
        value = std::min(hi, value + 1.0f);
        changed = true;
    }

    if (changed) engine.setParameter(parameter, value);

    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

namespace {

/// One matrix cell: a rounded square that fills with `accent` when routed.
bool modCell(const char *id, bool on, ImU32 accent) {
    const ImVec2 size(30.0f, 20.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    ImGui::PushID(id);
    ImGui::InvisibleButton("cell", size);
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 br(origin.x + size.x, origin.y + size.y);

    if (on) {
        dl->AddRectFilled(origin, br, accent, 4.0f);
        // A dark tick so the lit cell reads as "on" and not just coloured.
        const ImVec2 c((origin.x + br.x) * 0.5f, (origin.y + br.y) * 0.5f);
        dl->AddLine(ImVec2(c.x - 4, c.y), ImVec2(c.x - 1, c.y + 4), color::kBackground, 2.0f);
        dl->AddLine(ImVec2(c.x - 1, c.y + 4), ImVec2(c.x + 5, c.y - 4), color::kBackground, 2.0f);
    } else {
        dl->AddRectFilled(origin, br, color::kKnobBody, 4.0f);
        dl->AddRect(origin, br, hovered ? accent : color::kKnobEdge, 4.0f);
    }
    return clicked;
}

} // namespace

bool ModMatrix(s1::Engine &engine, const ModTarget *targets, int count, int blocks) {
    if (blocks < 1) blocks = 1;
    const int rows = (count + blocks - 1) / blocks;

    bool changed = false;
    ImGui::PushID("modmatrix");

    if (ImGui::BeginTable("mm", blocks * 3,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
        // Header: destination column stays blank, then the two LFO columns.
        ImGui::TableNextRow();
        for (int b = 0; b < blocks; ++b) {
            ImGui::TableSetColumnIndex(b * 3);
            // Four blocks of headings need the short form to stay in width.
            ImGui::TextColored(ImColor(color::kTextDim), blocks > 2 ? "DEST" : "DESTINATION");
            ImGui::TableSetColumnIndex(b * 3 + 1);
            ImGui::TextColored(ImColor(color::kOn), "LFO1");
            ImGui::TableSetColumnIndex(b * 3 + 2);
            ImGui::TextColored(ImColor(color::kAccent), "LFO2");
        }

        for (int r = 0; r < rows; ++r) {
            ImGui::TableNextRow();
            for (int b = 0; b < blocks; ++b) {
                const int index = b * rows + r;
                if (index >= count) continue;

                const ModTarget &target = targets[index];
                const int value = static_cast<int>(std::lround(engine.getParameter(target.parameter)));
                const bool lfo1 = (value & 1) != 0;
                const bool lfo2 = (value & 2) != 0;

                ImGui::PushID(index);

                ImGui::TableSetColumnIndex(b * 3);
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImColor(value != 0 ? color::kText : color::kTextDim), "%s",
                                   target.label);

                ImGui::TableSetColumnIndex(b * 3 + 1);
                if (modCell("l1", lfo1, color::kOn)) {
                    engine.setParameter(target.parameter,
                                        static_cast<float>((value ^ 1) & 3));
                    changed = true;
                }

                ImGui::TableSetColumnIndex(b * 3 + 2);
                if (modCell("l2", lfo2, color::kAccent)) {
                    engine.setParameter(target.parameter,
                                        static_cast<float>((value ^ 2) & 3));
                    changed = true;
                }

                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    ImGui::PopID();
    return changed;
}

bool Selector(s1::Engine &engine, S1Parameter parameter, const char *label,
              const char *const *options, int count) {
    ImGui::PushID(static_cast<int>(parameter));
    ImGui::BeginGroup();
    ImGui::TextColored(ImColor(color::kTextDim), "%s", label);

    const int current = static_cast<int>(std::lround(engine.getParameter(parameter)));
    bool changed = false;

    for (int i = 0; i < count; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, 2.0f);
        const bool selected = (i == current);
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? color::kOnDim : color::kPanel);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? color::kOn : color::kPanelEdge);
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? color::kText : color::kTextDim);
        ImGui::PushID(i);
        if (ImGui::Button(options[i])) {
            engine.setParameter(parameter, static_cast<float>(i));
            changed = true;
        }
        ImGui::PopID();
        ImGui::PopStyleColor(3);
    }

    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

bool ADSREditor(s1::Engine &engine, S1Parameter attack, S1Parameter decay, S1Parameter sustain,
                S1Parameter release, const ImVec2 &size, ImU32 fill) {
    ImGui::PushID(static_cast<int>(attack) + 20000);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("adsr", size);
    ImDrawList *dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                      color::kBackground, 4.0f);
    dl->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                color::kPanelEdge, 4.0f);

    // Normalised segment widths; the sustain plateau gets a fixed share so the
    // shape stays readable at extreme settings.
    const float a = engine.valueToPosition(attack, engine.getParameter(attack), 1.0f);
    const float d = engine.valueToPosition(decay, engine.getParameter(decay), 1.0f);
    const float r = engine.valueToPosition(release, engine.getParameter(release), 1.0f);
    const float s = std::clamp(engine.getParameter(sustain), 0.0f, 1.0f);

    const float pad = 6.0f;
    const float w = size.x - 2.0f * pad;
    const float h = size.y - 2.0f * pad;
    const float total = a + d + r + 1.0f;
    const float ax = w * (a / total);
    const float dx = w * (d / total);
    const float sx = w * (1.0f / total);
    const float rx = w * (r / total);

    const ImVec2 p0(origin.x + pad, origin.y + pad + h);
    const ImVec2 p1(p0.x + ax, origin.y + pad);
    const ImVec2 p2(p1.x + dx, origin.y + pad + h * (1.0f - s));
    const ImVec2 p3(p2.x + sx, p2.y);
    const ImVec2 p4(p3.x + rx, origin.y + pad + h);

    dl->AddQuadFilled(p0, p1, p2, ImVec2(p2.x, p0.y), (fill & 0x00FFFFFF) | 0x40000000);
    dl->AddQuadFilled(ImVec2(p2.x, p0.y), p2, p3, ImVec2(p3.x, p0.y),
                      (fill & 0x00FFFFFF) | 0x40000000);
    dl->AddTriangleFilled(ImVec2(p3.x, p0.y), p3, p4, (fill & 0x00FFFFFF) | 0x40000000);

    const ImVec2 pts[5] = {p0, p1, p2, p3, p4};
    dl->AddPolyline(pts, 5, fill, 0, 2.0f);
    for (const ImVec2 &p : {p1, p2, p3}) dl->AddCircleFilled(p, 3.0f, fill, 12);

    ImGui::PopID();
    return false; // handles are indicators; the knobs below do the editing
}

bool TouchPadXY(const char *id, const ImVec2 &size, float &x, float &y, bool latched,
                const char *xLabel, const char *yLabel) {
    ImGui::PushID(id);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("pad", size);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                      color::kBackground, 4.0f);
    dl->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), color::kPanelEdge, 4.0f);

    for (int i = 1; i < 4; ++i) {
        const float fx = origin.x + size.x * (i / 4.0f);
        const float fy = origin.y + size.y * (i / 4.0f);
        dl->AddLine(ImVec2(fx, origin.y), ImVec2(fx, origin.y + size.y), color::kPanel);
        dl->AddLine(ImVec2(origin.x, fy), ImVec2(origin.x + size.x, fy), color::kPanel);
    }

    bool changed = false;
    const bool active = ImGui::IsItemActive();
    if (active) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        x = std::clamp((mouse.x - origin.x) / size.x, 0.0f, 1.0f);
        y = std::clamp(1.0f - (mouse.y - origin.y) / size.y, 0.0f, 1.0f);
        changed = true;
    }

    if (active || latched) {
        const ImVec2 dot(origin.x + x * size.x, origin.y + (1.0f - y) * size.y);
        dl->AddLine(ImVec2(origin.x, dot.y), ImVec2(origin.x + size.x, dot.y), color::kAccentDim);
        dl->AddLine(ImVec2(dot.x, origin.y), ImVec2(dot.x, origin.y + size.y), color::kAccentDim);
        dl->AddCircleFilled(dot, 8.0f, color::kAccent, 20);
        dl->AddCircle(dot, 12.0f, color::kAccent, 20, 1.5f);
    }

    dl->AddText(ImVec2(origin.x + 6.0f, origin.y + size.y - 16.0f), color::kTextDim, xLabel);
    dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 4.0f), color::kTextDim, yLabel);

    ImGui::PopID();
    return changed;
}

bool SequencerGrid(s1::Engine &engine, int totalSteps, int currentStep, const ImVec2 &size) {
    bool changed = false;

    constexpr int kSteps = 16;
    constexpr float kGap = 3.0f;
    constexpr float kLedH = 10.0f;

    const ImGuiStyle &st = ImGui::GetStyle();
    const float cellWidth = size.x > 0.0f
                                ? std::max(28.0f, (size.x - kGap * (kSteps - 1)) / kSteps)
                                : 44.0f;

    // Column: beat LED, transpose slider, octave boost, note on. The slider
    // takes whatever height is left once the rest is accounted for.
    const float fixed = kLedH + st.ItemSpacing.y * 3.0f + ImGui::GetFrameHeight() * 2.0f +
                        ImGui::GetTextLineHeightWithSpacing();
    const float sliderH = size.y > 0.0f ? std::max(40.0f, size.y - fixed) : 64.0f;

    // S1Sequencer::getArpBeatCount() hands out a free-running counter -- it is
    // mBeatTime divided by the tempo multiplier, and nothing ever folds it back
    // (a single held note walks it into the hundreds). Wrapping it is the UI's
    // job, exactly as SequencerPanelController.updateLED does on iOS:
    //
    //     let notePosition = (beatCounter + seqTotalSteps) % seqTotalSteps
    //
    // Without this the lit step simply stopped once a held note outlasted one
    // pass of the sequence, because no column index ever equalled the counter
    // again. -1 is the "no beat reported yet" value and stays unlit.
    const int wrapSteps = totalSteps > 0 ? totalSteps : kSteps;
    const int litStep = currentStep >= 0 ? ((currentStep % wrapSteps) + wrapSteps) % wrapSteps : -1;

    ImGui::BeginGroup();
    ImGui::TextColored(ImColor(color::kTextDim), "STEP");
    for (int i = 0; i < kSteps; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, kGap);
        ImGui::BeginGroup();
        ImGui::PushID(i);

        const bool inRange = i < totalSteps;
        const bool playing = (i == litStep);

        // Step-position LED
        const ImVec2 ledPos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(cellWidth, kLedH));
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(ledPos.x + cellWidth * 0.5f, ledPos.y + kLedH * 0.5f), 4.0f,
            playing ? color::kLED : (inRange ? color::kTrack : color::kPanel), 12);

        // Transpose: a vertical slider over the parameter's own range, which is
        // -12..+12 semitones, matching the iOS VerticalSlider for this step.
        const S1Parameter noteParam = static_cast<S1Parameter>(sequencerPattern00 + i);
        int transpose = static_cast<int>(std::lround(engine.getParameter(noteParam)));
        const int lo = static_cast<int>(std::lround(engine.minimum(noteParam)));
        const int hi = static_cast<int>(std::lround(engine.maximum(noteParam)));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,
                              ImColor(inRange ? color::kKnobBody : color::kPanel).Value);
        if (ImGui::VSliderInt("##t", ImVec2(cellWidth, sliderH), &transpose, lo, hi, "%+d")) {
            engine.setParameter(noteParam, static_cast<float>(transpose));
            changed = true;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("step %d: %+d semitones", i + 1, transpose);

        // Octave boost
        const S1Parameter octParam = static_cast<S1Parameter>(sequencerOctBoost00 + i);
        if (Toggle(engine, octParam, "8va", ImVec2(cellWidth, 0))) changed = true;

        // Note on/off
        const S1Parameter onParam = static_cast<S1Parameter>(sequencerNoteOn00 + i);
        if (Toggle(engine, onParam, "on", ImVec2(cellWidth, 0))) changed = true;

        ImGui::PopID();
        ImGui::EndGroup();
    }
    ImGui::EndGroup();
    return changed;
}

void Keyboard(const ImVec2 &size, int firstOctave, int octaveCount, const bool *heldNotes,
              int &noteDown, int &notePrev) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("keyboard", size);
    ImDrawList *dl = ImGui::GetWindowDrawList();

    const int whiteCount = octaveCount * 7;
    const float whiteWidth = size.x / static_cast<float>(whiteCount);
    const float blackWidth = whiteWidth * 0.62f;
    const float blackHeight = size.y * 0.62f;

    static const int kWhiteSemitone[7] = {0, 2, 4, 5, 7, 9, 11};
    static const int kBlackSemitone[5] = {1, 3, 6, 8, 10};
    static const int kBlackAfterWhite[5] = {0, 1, 3, 4, 5};

    const bool active = ImGui::IsItemActive();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    notePrev = noteDown;
    noteDown = -1;

    // White keys
    for (int i = 0; i < whiteCount; ++i) {
        const int octave = firstOctave + i / 7;
        const int note = (octave + 1) * 12 + kWhiteSemitone[i % 7];
        const ImVec2 tl(origin.x + i * whiteWidth, origin.y);
        const ImVec2 br(tl.x + whiteWidth - 1.0f, origin.y + size.y);

        const bool hit = active && mouse.x >= tl.x && mouse.x < br.x &&
                         mouse.y >= tl.y && mouse.y <= br.y;
        const bool lit = (note >= 0 && note < 128 && heldNotes && heldNotes[note]);
        dl->AddRectFilled(tl, br, lit ? color::kAccent : IM_COL32(232, 232, 236, 255), 2.0f);
        dl->AddRect(tl, br, IM_COL32(60, 60, 66, 255), 2.0f);
        if (hit) noteDown = note;
    }

    // Black keys, drawn over the white ones
    for (int o = 0; o < octaveCount; ++o) {
        for (int b = 0; b < 5; ++b) {
            const int octave = firstOctave + o;
            const int note = (octave + 1) * 12 + kBlackSemitone[b];
            const float centre = origin.x + (o * 7 + kBlackAfterWhite[b] + 1) * whiteWidth;
            const ImVec2 tl(centre - blackWidth * 0.5f, origin.y);
            const ImVec2 br(centre + blackWidth * 0.5f, origin.y + blackHeight);

            const bool hit = active && mouse.x >= tl.x && mouse.x < br.x &&
                             mouse.y >= tl.y && mouse.y <= br.y;
            const bool lit = (note >= 0 && note < 128 && heldNotes && heldNotes[note]);
            dl->AddRectFilled(tl, br, lit ? color::kAccent : IM_COL32(20, 20, 24, 255), 2.0f);
            if (hit) noteDown = note; // black wins where they overlap
        }
    }
}

void PitchWheel(const ImVec2 &size, const float *frequencies, int count, const bool *playing) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("pitchwheel", size);
    ImDrawList *dl = ImGui::GetWindowDrawList();

    const ImVec2 centre(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
    const float radius = std::min(size.x, size.y) * 0.44f;

    dl->AddCircle(centre, radius, color::kPanelEdge, 64, 1.5f);
    dl->AddCircleFilled(centre, 3.0f, color::kTextDim, 12);

    if (frequencies == nullptr || count <= 0) return;

    // Middle C sits at twelve o'clock; one rotation is one octave.
    const float base = frequencies[0];
    if (base <= 0.0f) return;

    for (int i = 0; i < count; ++i) {
        const float f = frequencies[i];
        if (f <= 0.0f) continue;
        float octaveFraction = std::log2(f / base);
        octaveFraction -= std::floor(octaveFraction);

        const float angle = -1.57079633f + octaveFraction * kTwoPi;
        const ImVec2 outer(centre.x + std::cos(angle) * radius,
                           centre.y + std::sin(angle) * radius);
        const ImVec2 inner(centre.x + std::cos(angle) * radius * 0.55f,
                           centre.y + std::sin(angle) * radius * 0.55f);

        const bool lit = playing != nullptr && playing[i];
        dl->AddLine(inner, outer, lit ? color::kAccent : color::kTrack, lit ? 3.0f : 1.5f);
        dl->AddCircleFilled(outer, lit ? 5.0f : 3.0f, lit ? color::kAccent : color::kTextDim, 12);
    }
}

void SectionLabel(const char *text) {
    // The blank line above a heading is the first thing to go when the whole
    // panel has to fit; the rule under it carries the grouping on its own.
    if (!gCompactWidgets) ImGui::Spacing();
    ImGui::TextColored(ImColor(color::kAccent), "%s", text);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y),
                color::kPanelEdge);
    if (!gCompactWidgets) ImGui::Spacing();
}

} // namespace s1gui
