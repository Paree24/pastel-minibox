#pragma once
#include "devices/DeviceProcessor.h"
#include "model/Project.h"

namespace mb
{
/** The Minibox plugin's saved state: what MiniboxProcessor::getStateInformation writes and setStateInformation
    reads, and what a DAWproject export hands each plugin it places (docs/dawproject.md). */
namespace pluginstate
{
/** `hostParams`: parameter ID → normalised value (the processor's host parameters). The strip's sends and the
    master delay / reverb settings ride along, so an instrument from the groovebox sounds the same in the plugin. */
inline juce::ValueTree make (const DeviceProcessor* d, const std::vector<std::pair<juce::String, float>>& hostParams,
                             const Mixer& mix, const MasterSettings& master)
{
    juce::ValueTree state ("MiniboxPlugin");
    state.setProperty ("version", 1, nullptr);
    if (d != nullptr)
    {
        juce::ValueTree dt ("Device");
        d->save (dt);
        state.setProperty ("instrument", deviceTypeId (d->type()), nullptr);
        state.setProperty ("preset", d->presetName, nullptr);
        state.appendChild (dt, nullptr);
    }
    for (auto& [id, v] : hostParams) state.setProperty ("p_" + id, v, nullptr);
    juce::ValueTree mx ("Mix");
    mx.setProperty ("sendA", mix.sendA, nullptr);
    mx.setProperty ("sendB", mix.sendB, nullptr);
    mx.setProperty ("delayFeedback", master.delayFeedback, nullptr);
    mx.setProperty ("delayTone", master.delayTone, nullptr);
    mx.setProperty ("delayDiv", master.delayDiv, nullptr);
    mx.setProperty ("reverbDecay", master.reverbDecay, nullptr);
    mx.setProperty ("reverbDamp", master.reverbDamp, nullptr);
    state.appendChild (mx, nullptr);
    return state;
}

inline juce::MemoryBlock toBytes (const juce::ValueTree& state)
{
    juce::MemoryBlock out;
    juce::MemoryOutputStream os (out, false);
    state.writeToStream (os);
    return out;
}

/** Restores what make() wrote besides the device and the host parameters (absent in older sessions: kept). */
inline void applyMix (const juce::ValueTree& state, Mixer& mix, MasterSettings& master)
{
    const auto mx = state.getChildWithName ("Mix");
    if (! mx.isValid()) return;
    mix.sendA = mx.getProperty ("sendA", mix.sendA);
    mix.sendB = mx.getProperty ("sendB", mix.sendB);
    master.delayFeedback = mx.getProperty ("delayFeedback", master.delayFeedback);
    master.delayTone = mx.getProperty ("delayTone", master.delayTone);
    master.delayDiv = mx.getProperty ("delayDiv", master.delayDiv);
    master.reverbDecay = mx.getProperty ("reverbDecay", master.reverbDecay);
    master.reverbDamp = mx.getProperty ("reverbDamp", master.reverbDamp);
}
} // namespace pluginstate
} // namespace mb
