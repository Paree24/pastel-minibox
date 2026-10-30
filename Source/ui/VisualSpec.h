#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace mb::ui
{
/** What a section's visual draws. GUI-free so the editor specs build headless (the Android app reads them). */
enum class VisualKind
{
    None, Filter, Hpf, Adsr, Decay, Rate4, MacroEnv, PitchEnv, Wave, Lfo, Algo, Ops, Sample, Slices, Pads, Lane,
    Accent, Ring, Chorus, Delay, Slide, Drive, Voices, Arp, Bits, Reverb
};

/** A visual reads device parameters by id (resolved for the selected part / operator / zone). */
struct VisualSpec
{
    VisualKind kind = VisualKind::None;
    std::vector<juce::String> ids;
    int variant = 0;
};
} // namespace mb::ui
