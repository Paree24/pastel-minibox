#pragma once
#include "VisualSpec.h"
#include "devices/DeviceProcessor.h"

namespace mb::ui
{
/** Declarative editor layout (ARCHITECTURE.md §9.3), mirroring mockups/tools/specs.py. One renderer
    draws all seven editors from these specs; the desktop lays the sections out in rows. */
struct KnobSpec
{
    juce::String id, label;
    juce::String altId, altWhen;     // show altId instead when the altWhen parameter is non-zero
};

struct SectionSpec
{
    juce::String title;
    std::vector<KnobSpec> knobs;
    juce::String choiceId;           // a device choice parameter, or "ui:lane" (the lane view), "ui:env" / "ui:lfo" (which
                                     // envelope / LFO the card shows: a bit of the device's selection)
    juce::String toggleId, toggleLabel;   // "ui:random": the Randomize button
    juce::String menuId;             // a long choice (a wavetable) as a menu pill on the title line
    bool band = false;               // the macro band: a short full-width row (the phone: a strip above the tabs)
    bool compact = false;            // smaller knobs in one row (the visual gets the height: the Wavetable's cards)
    VisualSpec visual;
    float weight = 1.0f;
};

struct EditorSpec
{
    std::vector<std::vector<SectionSpec>> rows;
    std::vector<float> rowWeights;   // relative row heights (empty: equal)
    enum class Strip { Notes, Acid, Drums } strip = Strip::Notes;
};

/** Knob ids "macro:0" … "macro:7" stand for the device's macros. */
inline int macroIndexOf (const juce::String& id) { return id.startsWith ("macro:") ? id.substring (6).getIntValue() : -1; }

/** The Wavetable editor's selection: bit 0 the Filter envelope (else Amp), bit 1 LFO 2 (else LFO 1). */
inline int uiBit (const juce::String& choiceId) { return choiceId == "ui:env" ? 1 : (choiceId == "ui:lfo" ? 2 : 0); }
inline juce::StringArray uiChoices (const juce::String& choiceId)
{
    if (choiceId == "ui:env") return { "Amp", "Filter" };
    if (choiceId == "ui:lfo") return { "LFO 1", "LFO 2" };
    return {};
}

/** Builds the spec for a device in its current state (engine, selected part / operator / zone / pad). */
EditorSpec editorSpecFor (const DeviceProcessor&, int selection);
} // namespace mb::ui
