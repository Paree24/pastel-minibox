#pragma once
#include "engine/Engine.h"

namespace mb
{
/** Project files (.minibox): a zip holding project.json (the model), devices/<pattern><track>.xml
    and samples/ with every non-factory sample the project references. Missing samples are relinked
    by file name on load. */
namespace serializer
{
juce::var projectToJson (const Project&);
void projectFromJson (Project&, const juce::var&);

struct Result { bool ok = false; juce::String error; juce::StringArray missingSamples; };

Result save (Engine&, const juce::File&);
Result load (Engine&, const juce::File&);

/** Snapshot the engine's project into memory (for offline export and undo-free copies). */
juce::MemoryBlock snapshot (Engine&);
Result restore (Engine&, const juce::MemoryBlock&);

/** Offline render of the active pattern `loops` times plus a tail, into a 24-bit WAV. */
Result exportWav (Engine& live, const juce::File&, int loops, double sampleRate = 48000.0);

/** Rack preset (.mbrack): the 4 slot device presets plus the mixer of one pattern. */
Result saveRack (Engine&, int pattern, const juce::File&);
Result loadRack (Engine&, int pattern, const juce::File&);
} // namespace serializer
} // namespace mb
