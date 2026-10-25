#pragma once
#include "engine/Engine.h"

namespace mb
{
/** Standard MIDI files (docs/sequencer.md P1): a pattern out, a file's track into an instrument track. */
namespace midifile
{
struct Result { bool ok = false; juce::String error; int notes = 0, dropped = 0; };

/** One MIDI track per instrument (its name, its own channel; drums on 10) with the project's tempo and time
    signature, 960 ticks a quarter. Step sequences are written as they play (offsets, ratchets); muted notes are left out. */
Result exportPattern (Engine&, int pattern, const juce::File&);

/** The tracks of a file that hold notes, to pick from. */
struct TrackInfo { int index = 0; juce::String name; int notes = 0; };
std::vector<TrackInfo> tracksWithNotes (const juce::File&);

/** A file's track (or every track merged: `fileTrack` −1) into a pattern's track, replacing its notes: a
    piano-roll clip for melodic instruments, the nearest steps of the drum lanes for drums. The pattern grows to
    fit, up to its longest; notes beyond are dropped (counted). The project's tempo is kept. */
Result importTrack (Engine&, int pattern, int track, const juce::File&, int fileTrack);
} // namespace midifile
} // namespace mb
