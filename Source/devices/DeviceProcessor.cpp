#include "DeviceProcessor.h"

namespace mb
{
juce::String deviceTypeName (DeviceType t)
{
    switch (t)
    {
        case DeviceType::Mono: return "Bassline";
        case DeviceType::Poly: return "VA";
        case DeviceType::Acid: return "Acid";
        case DeviceType::FM: return "FM";
        case DeviceType::Sampler: return "Sampler";
        case DeviceType::Drums: return "Drums";
        case DeviceType::DrumSampler: return "Drum Sampler";
        default: return "Empty";
    }
}

juce::String deviceTypeId (DeviceType t)
{
    // stable ids for files, presets and host state: they predate the display names (Mono → Bassline, Poly → VA)
    if (t == DeviceType::Mono) return "mono";
    if (t == DeviceType::Poly) return "poly";
    return deviceTypeName (t).toLowerCase().removeCharacters (" ");
}

DeviceType deviceTypeFromId (const juce::String& id)
{
    const auto s = id.toLowerCase().removeCharacters (" ");
    if (s == "va") return DeviceType::Poly;   // retired device type, migrates to Poly (VA engine)
    for (int i = 0; i < (int) DeviceType::Count; ++i)
        if (deviceTypeId ((DeviceType) i) == s) return (DeviceType) i;
    return DeviceType::Count;
}

bool isDrumDevice (DeviceType t) { return t == DeviceType::Drums || t == DeviceType::DrumSampler; }

// ---------------------------------------------------------------------------------------------
float ParamInfo::toNorm (float v) const
{
    if (max <= min) return 0.0f;
    v = dsp::clamp (v, min, max);
    if (log && min > 0.0f) return std::log (v / min) / std::log (max / min);
    return (v - min) / (max - min);
}

float ParamInfo::fromNorm (float n) const
{
    n = dsp::clamp (n, 0.0f, 1.0f);
    float v = (log && min > 0.0f) ? min * std::pow (max / min, n) : min + (max - min) * n;
    return snap (v);
}

float ParamInfo::snap (float v) const
{
    v = dsp::clamp (v, min, max);
    return stepped() ? std::round (v) : v;
}

juce::String ParamInfo::format (float v) const
{
    auto num = [] (float x, int dp) { return juce::String (x, dp); };
    switch (unit)
    {
        case Unit::Percent: return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        case Unit::Hz:
            return v >= 1000.0f ? num (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                : num (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " Hz";
        case Unit::KHz: return num (v, 0) + " kHz";
        case Unit::Seconds: return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : num (v, 2) + " s";
        case Unit::Semis: return (v > 0.0f ? "+" : "") + num (v, 0) + " st";
        case Unit::Cents: return (v > 0.0f ? "+" : "") + num (v, 0) + " c";
        case Unit::Db: return num (v, 1) + " dB";
        case Unit::Ratio: return num (v, 2);
        case Unit::Int: return juce::String (juce::roundToInt (v));
        case Unit::Bits: return juce::String (juce::roundToInt (v)) + " bit";
        case Unit::Footage: { static const char* f[] = { "16'", "8'", "4'", "2'" }; return f[std::clamp (juce::roundToInt (v), 0, 3)]; }
        case Unit::Division: return dsp::divisions()[std::clamp (juce::roundToInt (v), 0, dsp::kNumDivisions - 1)].name;
        case Unit::Choice: return choices[std::clamp (juce::roundToInt (v), 0, choices.size() - 1)];
        case Unit::Pan:
        {
            const int p = juce::roundToInt (v * 100.0f);
            return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
        }
        default:
            if (bipolar) return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + "%";
            return juce::String (juce::roundToInt ((v - min) / (max - min) * 100.0f)) + "%";
    }
}

int ParamLayout::add (ParamInfo p)
{
    jassert (byId.find (p.id.toStdString()) == byId.end());
    byId[p.id.toStdString()] = (int) infos.size();
    infos.push_back (std::move (p));
    return (int) infos.size() - 1;
}

int ParamLayout::cont (const juce::String& id, const juce::String& name, float mn, float mx, float def, Unit u,
                       bool log, bool bipolar)
{
    ParamInfo p; p.id = id; p.name = name; p.min = mn; p.max = mx; p.def = def; p.unit = u; p.log = log; p.bipolar = bipolar;
    return add (p);
}

int ParamLayout::choice (const juce::String& id, const juce::String& name, const juce::StringArray& opts, int def)
{
    ParamInfo p; p.id = id; p.name = name; p.min = 0; p.max = (float) (opts.size() - 1); p.def = (float) def;
    p.unit = Unit::Choice; p.choices = opts;
    return add (p);
}

int ParamLayout::integer (const juce::String& id, const juce::String& name, int mn, int mx, int def, Unit u)
{
    ParamInfo p; p.id = id; p.name = name; p.min = (float) mn; p.max = (float) mx; p.def = (float) def; p.unit = u;
    return add (p);
}

int ParamLayout::indexOf (const juce::String& id) const
{
    auto it = byId.find (id.toStdString());
    return it == byId.end() ? -1 : it->second;
}

// ---------------------------------------------------------------------------------------------
void DeviceProcessor::finishLayout()
{
    values = std::vector<std::atomic<float>> ((size_t) layout.size());
    locks.assign ((size_t) layout.size(), std::numeric_limits<float>::quiet_NaN());
    held = std::vector<std::atomic<bool>> ((size_t) layout.size());
    resetToDefaults();
}

void DeviceProcessor::resetToDefaults()
{
    for (int i = 0; i < layout.size(); ++i) values[(size_t) i].store (layout[i].def);
    presetName = "Init";
    macroMaps = defaultMacroMaps;
}

void DeviceProcessor::macro (int m, const juce::String& name, std::initializer_list<std::tuple<const char*, float, float>> t)
{
    auto& mm = macroMaps[(size_t) m];
    mm.name = name;
    mm.count = 0;
    for (auto& [id, lo, hi] : t)
    {
        const int idx = layout.indexOf (id);
        jassert (idx >= 0);
        if (idx >= 0 && mm.count < 8) mm.targets[(size_t) mm.count++] = { idx, lo, hi };
    }
    defaultMacroMaps[(size_t) m] = mm;
}

std::atomic<bool>& oversampleVoices() { static std::atomic<bool> o { true }; return o; }

std::atomic<int>& qualitySetting() { static std::atomic<int> q { (int) Quality::Full }; return q; }

float DeviceProcessor::getMacro (int m) const
{
    const auto& mm = macroMaps[(size_t) m];
    if (mm.count == 0) return 0.0f;
    const auto& t = mm.targets[0];
    const float n = getNorm (t.param);
    if (std::abs (t.hi - t.lo) < 1.0e-6f) return 0.0f;
    return dsp::clamp ((n - t.lo) / (t.hi - t.lo), 0.0f, 1.0f);
}

float DeviceProcessor::shownMacro (int m) const
{
    const auto& mm = macroMaps[(size_t) m];
    if (mm.count == 0) return 0.0f;
    const auto& t = mm.targets[0];
    if (std::abs (t.hi - t.lo) < 1.0e-6f) return 0.0f;
    return dsp::clamp ((shownNorm (t.param) - t.lo) / (t.hi - t.lo), 0.0f, 1.0f);
}

void DeviceProcessor::setMacro (int m, float v01)
{
    const auto& mm = macroMaps[(size_t) m];
    for (int i = 0; i < mm.count; ++i)
    {
        const auto& t = mm.targets[(size_t) i];
        setNorm (t.param, t.lo + (t.hi - t.lo) * dsp::clamp (v01, 0.0f, 1.0f));
    }
}

juce::String DeviceProcessor::macroString() const
{
    juce::StringArray parts;
    for (auto& mm : macroMaps)
    {
        juce::StringArray t;
        for (int i = 0; i < mm.count; ++i)
            t.add (layout[mm.targets[(size_t) i].param].id + ":" + juce::String (mm.targets[(size_t) i].lo) + ":"
                   + juce::String (mm.targets[(size_t) i].hi));
        parts.add (mm.name + ":" + t.joinIntoString (","));
    }
    return parts.joinIntoString ("|");
}

void DeviceProcessor::setMacroString (const juce::String& s)
{
    if (s.isEmpty()) return;
    auto maps = juce::StringArray::fromTokens (s, "|", "");
    for (int m = 0; m < std::min (8, maps.size()); ++m)
    {
        auto& mm = macroMaps[(size_t) m];
        const auto name = maps[m].upToFirstOccurrenceOf (":", false, false);
        auto targets = juce::StringArray::fromTokens (maps[m].fromFirstOccurrenceOf (":", false, false), ",", "");
        MacroMap fresh;
        fresh.name = name;
        for (auto& tgt : targets)
        {
            auto f = juce::StringArray::fromTokens (tgt, ":", "");
            if (f.size() < 3) continue;
            const int idx = layout.indexOf (f[0]);
            if (idx >= 0 && fresh.count < 8) fresh.targets[(size_t) fresh.count++] = { idx, f[1].getFloatValue(), f[2].getFloatValue() };
        }
        if (fresh.count > 0) mm = fresh;
    }
}

void DeviceProcessor::save (juce::ValueTree& tree) const
{
    tree.setProperty ("type", deviceTypeId (devType), nullptr);
    tree.setProperty ("version", kVersion, nullptr);
    tree.setProperty ("preset", presetName, nullptr);
    juce::ValueTree ps ("Params");
    for (int i = 0; i < layout.size(); ++i)
        ps.setProperty (layout[i].id, get (i), nullptr);
    tree.appendChild (ps, nullptr);
    tree.setProperty ("macros", macroString(), nullptr);
}

void DeviceProcessor::load (const juce::ValueTree& tree)
{
    resetToDefaults();   // reset-then-apply, so no value leaks from the previous state
    presetName = tree.getProperty ("preset", "Init").toString();
    auto ps = tree.getChildWithName ("Params");
    for (int i = 0; i < ps.getNumProperties(); ++i)
    {
        const auto name = ps.getPropertyName (i);
        const int idx = layout.indexOf (name.toString());
        if (idx >= 0) set (idx, (float) ps.getProperty (name));
    }
    setMacroString (tree.getProperty ("macros").toString());
    // Migration: device trees saved by the retired VA device select the Poly VA engine.
    if (tree.getProperty ("type").toString() == "va" && devType == DeviceType::Poly)
        set ("engine", 1.0f);
}

void DeviceProcessor::setExpression (int lane, float value)
{
    switch (lane)
    {
        case kBend: bend = juce::jlimit (-1.0f, 1.0f, value); break;
        case kModWheel: modWheel = juce::jlimit (0.0f, 1.0f, value); break;
        case kExpression: expressionGain = juce::jlimit (0.0f, 1.0f, value); break;
        case kPressure: pressure = juce::jlimit (0.0f, 1.0f, value); break;
        default: break;
    }
}

float DeviceProcessor::expressionSemis (int n)
{
    const float depth = 0.5f * std::max (modWheel, pressure);
    const float vib = depth > 0.0f ? depth * (float) std::sin (vibPhase) : 0.0f;
    vibPhase = std::fmod (vibPhase + juce::MathConstants<double>::twoPi * 5.5 * n / std::max (1.0, sampleRate), juce::MathConstants<double>::twoPi);
    return bend * bendRange + vib;
}

void DeviceProcessor::markNote (int note, bool on)
{
    if (note < 0 || note > 127) return;
    auto& w = heldNotes[(size_t) (note >> 5)];
    const uint32_t bit = 1u << (note & 31);
    if (on) w.fetch_or (bit); else w.fetch_and (~bit);
}

void DeviceProcessor::trackSilence (const float* L, const float* R, int n)
{
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) peak = std::max ({ peak, std::abs (L[i]), std::abs (R[i]) });
    silentBlocks = peak < 3.2e-5f ? silentBlocks + 1 : 0;   // −90 dB
}
} // namespace mb
