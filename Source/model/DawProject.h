#pragma once
#include "engine/Engine.h"

namespace mb
{
/** DAWproject export (docs/dawproject.md): the whole project as a .dawproject file another DAW opens with a
    Minibox plugin on every track, each holding its instrument, preset and mix. */
namespace dawproject
{
struct Result { bool ok = false; juce::String error; int tracks = 0, patterns = 0, notes = 0; };

/** Patterns that hold notes go on the timeline one after another (a marker names each) and into a scene each;
    a track whose instrument differs between patterns becomes one DAW track per instrument. */
Result exportProject (Engine&, const juce::File&);

/** A note as the DAW receives it: quarter notes from the start of its pattern. */
struct Note { double time = 0.0, duration = 0.25; int key = 60; float vel = 0.8f; };

/** What a pattern's track plays, as notes (steps, ratchets, offsets and swing baked in; muted notes left out). */
std::vector<Note> notesOf (Engine&, int pattern, int track);

/** The VST3 class ID of the Minibox plugin (32 hex digits) and a .vstpreset holding `componentState`. */
juce::String pluginClassId();
juce::MemoryBlock vstPreset (const juce::MemoryBlock& componentState);
} // namespace dawproject
} // namespace mb
