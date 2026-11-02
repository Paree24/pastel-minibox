#include "EditorSpecs.h"
#include "devices/drums/DrumsDevice.h"

namespace mb::ui
{
namespace
{
using VK = VisualKind;
KnobSpec K (const juce::String& id, const juce::String& label, const juce::String& alt = {}, const juce::String& when = {}) { return { id, label, alt, when }; }
VisualSpec V (VK k, std::vector<juce::String> ids = {}, int variant = 0) { return { k, std::move (ids), variant }; }
SectionSpec S (juce::String title, std::vector<KnobSpec> knobs, juce::String choice = {}, VisualSpec vis = {}, float w = 1.0f,
               juce::String toggle = {}, juce::String toggleLabel = {})
{
    SectionSpec s;
    s.title = std::move (title); s.knobs = std::move (knobs); s.choiceId = std::move (choice); s.visual = std::move (vis);
    s.weight = w; s.toggleId = std::move (toggle); s.toggleLabel = std::move (toggleLabel);
    return s;
}

/** The Reverb card every device carries; `mix` is the device's own Mix (or return) parameter. */
SectionSpec reverb (const juce::String& mix, float w = 0.9f)
{
    return S ("Reverb", { K (mix, "Mix"), K ("revSize", "Size"), K ("revTone", "Tone"), K ("revPre", "Pre-delay") }, {},
              V (VK::Reverb, { mix, "revSize", "revTone", "revPre" }), w);
}

EditorSpec mono()
{
    EditorSpec e;
    e.rows = {
        { S ("Oscillator", { K ("mixSaw", "Saw"), K ("mixPulse", "Pulse"), K ("mixSub", "Sub"), K ("mixNoise", "Noise"), K ("range", "Range"), K ("tune", "Tune") },
             "subType", V (VK::Wave, { "mixPulse", "mixSaw", "mixSub", "mixNoise", "pw" }, 0), 1.2f),
          S ("Pulse", { K ("pw", "Width"), K ("pwm", "PWM") }, "pwmSrc", V (VK::Wave, { "pw", "pwm" }, 4), 0.8f),
          S ("Filter", { K ("cutoff", "Cutoff"), K ("reso", "Reso"), K ("envAmt", "Env"), K ("lfoCut", "LFO"), K ("key", "Key"), K ("drive", "Drive") }, {},
             V (VK::Filter, { "cutoff", "reso" }, 0), 1.2f),
          S ("Envelope", { K ("attack", "Attack"), K ("decay", "Decay"), K ("sustain", "Sustain"), K ("release", "Release") }, "ampMode",
             V (VK::Adsr, { "attack", "decay", "sustain", "release" })) },
        { S ("LFO", { K ("lfoRate", "Rate", "lfoDiv", "lfoSync"), K ("lfoFade", "Fade"), K ("lfoPitch", "Pitch"), K ("lfoPwm", "PWM"), K ("lfoCut", "Cutoff") }, "lfoShape",
             V (VK::Lfo, { "lfoShape" }, 0), 1.1f, "lfoSync", "Sync"),
          S ("Glide", { K ("glideTime", "Time") }, "glideMode", V (VK::Slide, { "glideTime", "glideMode" }, 1), 0.8f),
          S ("Voice", { K ("detune", "Detune"), K ("chord", "Chord") }, "voiceMode", V (VK::Voices, {}, 4), 0.9f),
          S ("FX", { K ("fxChorus", "Chorus"), K ("fxDelay", "Delay"), K ("fxTime", "Time"), K ("fxFeedback", "Feedback"), K ("level", "Level") }, {},
             V (VK::Delay, { "fxFeedback" })),
          reverb ("fxReverb") } };
    return e;
}

std::vector<SectionSpec> polyShared()
{
    return { S ("LFO", { K ("lfoRate", "Rate", "lfoDiv", "lfoSync"), K ("lfoDelay", "Delay"), K ("lfoPitch", "Pitch"), K ("lfoPwm", "PWM"), K ("lfoCut", "Cutoff") },
                "lfoShape", V (VK::Lfo, { "lfoShape" }, 1), 1.0f, "lfoSync", "Sync"),
             S ("Voice", { K ("detune", "Detune"), K ("glide", "Glide"), K ("chord", "Chord") }, "voiceMode", V (VK::Voices, {}, 6)),
             S ("Arp", { K ("arpRange", "Range"), K ("arpRate", "Rate"), K ("arpGate", "Gate") }, "arpMode", V (VK::Arp, { "arpMode", "arpRange" })),
             S ("Drive", { K ("driveAmt", "Amount"), K ("driveTone", "Tone") }, "driveType", V (VK::Drive, { "driveAmt", "driveType" }), 0.8f),
             S ("Chorus", { K ("chorusMix", "Mix") }, "chorusMode", V (VK::Chorus, { "chorusMode" }, 0), 0.8f),
             S ("Delay", { K ("delayDiv", "Time", "delayFree", "delaySync"), K ("delayFb", "Feedback"), K ("delayTone", "Tone"), K ("delayMix", "Mix") }, "delaySync",
                V (VK::Delay, { "delayFb" })),
             reverb ("reverb") };
}

EditorSpec poly (const DeviceProcessor& d)
{
    EditorSpec e;
    const bool va = d.get ("engine") > 0.5f;
    auto filterEnv = S ("Filter Env", { K ("fAttack", "Attack"), K ("fDecay", "Decay"), K ("fSustain", "Sustain"), K ("fRelease", "Release") }, {},
                        V (VK::Adsr, { "fAttack", "fDecay", "fSustain", "fRelease" }));
    auto amp = S ("Amp", { K ("aAttack", "Attack"), K ("aDecay", "Decay"), K ("aSustain", "Sustain"), K ("aRelease", "Release"), K ("level", "Level"), K ("velocity", "Velocity") },
                  "ampSrc", V (VK::Adsr, { "aAttack", "aDecay", "aSustain", "aRelease" }), 1.1f);
    if (! va)
        e.rows.push_back ({ S ("DCO", { K ("dcoPw", "PW"), K ("dcoPwm", "PWM"), K ("sub", "Sub"), K ("noise", "Noise"), K ("drift", "Drift") }, "dcoWave",
                               V (VK::Wave, { "dcoWave", "dcoPw", "sub" }, 1), 1.2f),
                            S ("HPF", {}, "hpf", V (VK::Hpf, { "hpf" }), 0.7f),
                            S ("Filter", { K ("cutoff", "Cutoff"), K ("reso", "Reso"), K ("envAmt", "Env"), K ("key", "Key"), K ("filterVel", "Velocity") }, {},
                               V (VK::Filter, { "cutoff", "reso" }, 0), 1.2f),
                            filterEnv, amp });
    else
        e.rows.push_back ({ S ("Oscillators", { K ("vaOctave", "Octave"), K ("vaPw", "PW"), K ("vaPwm", "PWM"), K ("sub", "Sub"), K ("noise", "Noise"), K ("vaFine", "Fine") },
                               "vaWave", V (VK::Wave, { "vaWave", "vaPw", "sub" }, 2), 1.2f),
                            S ("Filter", { K ("cutoff", "Cutoff"), K ("reso", "Peak"), K ("envAmt", "Env"), K ("key", "Key"), K ("filterDrive", "Drive") }, "filterMode",
                               V (VK::Filter, { "cutoff", "reso" }, 1), 1.2f),
                            filterEnv, amp });
    e.rows.push_back (polyShared());
    return e;
}

EditorSpec acid()
{
    EditorSpec e;
    e.strip = EditorSpec::Strip::Acid;
    e.rows = {
        { S ("Osc", { K ("oscWave", "Wave"), K ("pw", "PW"), K ("sub", "Sub"), K ("noise", "Noise"), K ("tune", "Tune"), K ("fine", "Fine") }, "stack",
             V (VK::Wave, { "oscWave", "pw" }, 3)),
          S ("Stack", { K ("stackDetune", "Detune"), K ("stackSpread", "Spread"), K ("stackMix", "Mix") }, "stackMode", V (VK::Voices, {}, 3), 0.8f),
          S ("Filter", { K ("cutoff", "Cutoff"), K ("reso", "Reso"), K ("envMod", "Env Mod"), K ("decay", "Decay"), K ("key", "Key"), K ("bassComp", "Bass Comp") },
             "filterType", V (VK::Filter, { "cutoff", "reso" }, 0), 1.2f),
          S ("Accent", { K ("accAmt", "Amount"), K ("sweep", "Sweep"), K ("accDecay", "Acc Decay") }, "artic", V (VK::Accent, { "accAmt", "sweep", "artic" })),
          S ("Slide", { K ("slideTime", "Time") }, "slideMode", V (VK::Slide, { "slideTime", "artic" }, 0), 0.8f) },
        { S ("Amp", { K ("level", "Level"), K ("velocity", "Velocity"), K ("ampDecay", "Decay") }, "ampMode", V (VK::Decay, { "ampMode", "ampDecay" }, 1)),
          S ("LFO", { K ("lfoRate", "Rate", "lfoDiv", "lfoSync"), K ("lfoCut", "Cutoff"), K ("lfoPitch", "Pitch"), K ("lfoFade", "Fade") }, "lfoShape",
             V (VK::Lfo, { "lfoShape" }, 1), 1.0f, "lfoSync", "Sync"),
          S ("Drive", { K ("driveAmt", "Amount"), K ("driveTone", "Tone"), K ("driveMix", "Mix") }, "driveType", V (VK::Drive, { "driveAmt", "driveType" }, 1)),
          S ("Delay", { K ("delayDiv", "Time", "delayFree", "delaySync"), K ("delayFb", "Feedback"), K ("delayDuck", "Duck"), K ("delayTone", "Tone"), K ("delayMix", "Mix") },
             "delaySync", V (VK::Delay, { "delayFb" })),
          reverb ("reverb") } };
    return e;
}

EditorSpec fm (int op)
{
    EditorSpec e;
    const juce::String o = "op" + juce::String (op + 1);
    std::vector<juce::String> envIds;
    for (auto f : { "R1", "R2", "R3", "R4", "L1", "L2", "L3", "L4" }) envIds.push_back (o + f);
    e.rows = {
        { S ("Algorithm", { K ("algorithm", "Algorithm"), K ("feedback", "Feedback") }, {}, V (VK::Algo, { "algorithm" }), 0.9f),
          S ("Operators", {}, {}, V (VK::Ops, {}), 1.7f),
          S ("Operator " + juce::String (op + 1), { K (o + "Ratio", "Ratio", o + "Freq", o + "Fixed"), K (o + "Detune", "Detune"), K (o + "Level", "Level"),
                                                     K (o + "Vel", "Vel Sens"), K (o + "KeyScale", "Key Scale"), K (o + "RateScale", "Rate Scale") },
             o + "Fixed", V (VK::Rate4, envIds), 1.3f),
          reverb ("reverb") },
        { S ("Modulators", { K ("modAttack", "Mod Attack"), K ("modDecay", "Mod Decay"), K ("modLevel", "Mod Level") }, {}, V (VK::MacroEnv, { "modAttack", "modDecay", "modLevel" })),
          S ("Carriers", { K ("carAttack", "Car Attack"), K ("carDecay", "Car Decay"), K ("carLevel", "Car Level") }, {}, V (VK::MacroEnv, { "carAttack", "carDecay", "carLevel" })),
          S ("Pitch EG", { K ("pegR1", "Rate 1"), K ("pegR2", "Rate 2"), K ("pegR3", "Rate 3"), K ("pegL1", "Level 1"), K ("pegL2", "Level 2"), K ("pegL3", "Level 3") }, {},
             V (VK::PitchEnv, { "pegR1", "pegR2", "pegR3", "pegL1", "pegL2", "pegL3" }, 3)),
          S ("LFO", { K ("lfoRate", "Rate"), K ("lfoDelay", "Delay"), K ("lfoPitch", "Pitch"), K ("lfoAmp", "Amp") }, "lfoShape", V (VK::Lfo, { "lfoShape" }, 2)),
          S ("Voice", { K ("voices", "Voices"), K ("transpose", "Transpose"), K ("glide", "Glide") }, "voiceMode", V (VK::Voices, {}, 8)),
          S ("Chorus", { K ("chorusRate", "Rate"), K ("chorusDepth", "Depth"), K ("chorusMix", "Mix") }, "chorusMode", V (VK::Chorus, { "chorusMode" }, 1)) } };
    return e;
}

EditorSpec sampler (int zone)
{
    EditorSpec e;
    const juce::String z = "z" + juce::String (zone + 1);
    e.rows = {
        { S ("Sample", { K ("start", "Start"), K ("length", "Length"), K ("loopStart", "Loop Start"), K ("loopLen", "Loop Len"), K (z + "Root", "Root") }, "playMode",
             V (VK::Sample, {}, 0), 2.3f, "reverse", "Reverse"),
          S ("Pitch", { K ("tune", "Tune"), K ("fine", "Fine"), K ("pegInt", "PEG Int"), K ("pegAttack", "PEG Attack"), K ("pegDecay", "PEG Decay") }, {},
             V (VK::PitchEnv, { "pegInt", "pegAttack", "pegDecay" }, 1)),
          S ("Character", { K ("bits", "Bits"), K ("rate", "Rate"), K ("drive", "Drive") }, "character", V (VK::Bits, { "bits", "rate", "character" }, 0)),
          reverb ("reverb") },
        { S ("Filter", { K ("cutoff", "Cutoff"), K ("reso", "Reso"), K ("envAmt", "Env"), K ("key", "Key") }, "filterType", V (VK::Filter, { "cutoff", "reso", "filterType" }, 2), 1.2f),
          S ("Amp", { K ("attack", "Attack"), K ("decay", "Decay"), K ("sustain", "Sustain"), K ("release", "Release"), K ("velocity", "Velocity") }, {},
             V (VK::Adsr, { "attack", "decay", "sustain", "release" }), 1.1f),
          S ("LFO", { K ("lfoRate", "Rate"), K ("lfoPitch", "Pitch"), K ("lfoCut", "Cutoff") }, "lfoShape", V (VK::Lfo, { "lfoShape" }, 1)),
          S ("Voice", { K ("voices", "Voices"), K ("glide", "Glide"), K ("level", "Level") }, "voiceMode", V (VK::Voices, {}, 8)) } };
    return e;
}

EditorSpec drums (const DeviceProcessor& d, int part)
{
    EditorSpec e;
    e.strip = EditorSpec::Strip::Drums;
    const juce::String p = "p" + juce::String (part + 1);
    const auto& dd = static_cast<const DrumsDevice&> (d);
    const bool layered = dd.partEngine (part) == drums::Layer;
    std::vector<KnobSpec> engineKnobs;
    if (layered)
        engineKnobs = { K (p + "Pitch", "Pitch"), K (p + "Bend", "Bend"), K (p + "BendTime", "Bend Time"), K (p + "Decay", "Decay"),
                        K (p + "LayMix", "Mix"), K (p + "LayRatio", "B Pitch") };
    else   // only the knobs this engine reads (a knob that changes nothing is not shown)
        for (const auto& [f, label] : dd.engineKnobs (part))
            engineKnobs.push_back (K (d.params()[dd.partParam (part, f)].id, label));
    std::vector<KnobSpec> shape { K (p + "Accent", "Accent"), K (p + "Drive", "Drive") };
    if (layered) for (auto [f, l] : { std::pair { "LayA", "Source A" }, { "LayB", "Source B" }, { "LayMod", "Mod" }, { "LayEnv", "Envelope" } }) shape.push_back (K (p + f, l));
    e.rows = {
        { S ("Parts", {}, {}, V (VK::Pads, {}, 0), 1.0f),
          S (d.laneName (part), engineKnobs, p + "Eng", V (VK::PitchEnv, { p + "Bend", p + "BendTime", p + "Decay" }, 0), 1.4f),
          S (layered ? "Layers" : "Shape", shape, p + "DriveType", V (VK::Drive, { p + "Drive", p + "DriveType" }), layered ? 1.1f : 0.8f),
          S ("Part Mix", { K (p + "Level", "Level"), K (p + "Pan", "Pan"), K (p + "SendDelay", "Delay"), K (p + "SendReverb", "Reverb"), K (p + "VelSens", "Vel Sens"), K (p + "Choke", "Choke") }, {}, {}, 1.0f) },
        { S ("Kit Resonator", { K ("resTune", "Tune"), K ("resDecay", "Decay"), K ("resDamp", "Damp"), K (p + "ResSend", "Send"), K ("resMix", "Mix") }, "resType",
             V (VK::Ring, { "resTune", "resDecay" }), 1.1f),
          S ("Kit", { K ("kitComp", "Comp"), K ("kitDrive", "Drive"), K ("kitWidth", "Width"), K ("kitLevel", "Level") }, {}, {}, 0.8f),
          reverb ("kitReverb"),
          S (d.laneName (part) + " Lane", {}, "ui:lane", V (VK::Lane, {}), 1.7f) } };
    return e;
}

EditorSpec drumSampler (const DeviceProcessor& d, int pad)
{
    EditorSpec e;
    e.strip = EditorSpec::Strip::Drums;
    const juce::String p = "pd" + juce::String (pad + 1);
    e.rows = {
        { S ("Pads", {}, {}, V (VK::Pads, {}, 1), 1.2f),
          S ("Sample", { K (p + "Start", "Start"), K (p + "Length", "Length") }, p + "Mode", V (VK::Sample, {}, 1), 1.6f, p + "Reverse", "Reverse"),
          S ("Pitch", { K (p + "Pitch", "Pitch"), K (p + "PegInt", "PEG Int"), K (p + "PegDecay", "PEG Decay") }, {}, V (VK::PitchEnv, { p + "PegInt", p + "PegDecay" }, 2), 0.9f),
          S ("Amp", { K (p + "Attack", "Attack"), K (p + "Decay", "Decay"), K (p + "Level", "Level"), K (p + "Pan", "Pan") }, {}, V (VK::Decay, { p + "Attack", p + "Decay" }, 0), 0.9f) },
        { S ("Tone", { K (p + "HiCut", "Hi-Cut"), K (p + "Bits", "Bits"), K (p + "Drive", "Drive") }, {}, V (VK::Filter, { p + "HiCut" }, 3), 0.9f),
          S ("Sends", { K (p + "SendDelay", "Delay"), K (p + "SendReverb", "Reverb"), K (p + "Choke", "Choke") }, {}, {}, 0.8f),
          S ("Slice", { K ("sliceSens", "Sensitivity"), K ("kitLevel", "Level") }, "sliceMode", V (VK::Slices, {}), 1.4f),
          reverb ("kitReverb"),
          S (d.laneName (pad) + " Lane", {}, "ui:lane", V (VK::Lane, {}), 1.3f) } };
    return e;
}
} // namespace

EditorSpec editorSpecFor (const DeviceProcessor& d, int sel)
{
    switch (d.type())
    {
        case DeviceType::Mono: return mono();
        case DeviceType::Poly: return poly (d);
        case DeviceType::Acid: return acid();
        case DeviceType::FM: return fm (std::clamp (sel, 0, 5));
        case DeviceType::Sampler: return sampler (std::clamp (sel, 0, 7));
        case DeviceType::Drums: return drums (d, std::clamp (sel, 0, 7));
        case DeviceType::DrumSampler: return drumSampler (d, std::clamp (sel, 0, 7));
        default: return {};
    }
}
} // namespace mb::ui
