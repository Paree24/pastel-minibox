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
    juce::String choiceId;           // a device choice parameter, or "ui:lane" for the lane view
    juce::String toggleId, toggleLabel;
    VisualSpec visual;
    float weight = 1.0f;
};

struct EditorSpec
{
    std::vector<std::vector<SectionSpec>> rows;
    enum class Strip { Notes, Acid, Drums } strip = Strip::Notes;
};

/** Builds the spec for a device in its current state (engine, selected part / operator / zone / pad). */
EditorSpec editorSpecFor (const DeviceProcessor&, int selection);
} // namespace mb::ui
