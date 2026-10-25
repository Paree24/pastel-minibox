#include "PresetBank.h"
#include "devices/SampleSlot.h"
#include "devices/sampler/SamplerDevice.h"
#include "devices/drumsampler/DrumSamplerDevice.h"
#include <map>

namespace mb::presets
{
namespace
{
void setSamples (DeviceProcessor& d, const juce::StringArray& refs, bool sync)
{
    if (d.type() == DeviceType::Sampler)
    {
        auto& s = static_cast<SamplerDevice&> (d);
        for (int z = 0; z < SamplerDevice::kZones; ++z) s.loadZone (z, z < refs.size() ? refs[z] : juce::String(), sync);
    }
    else if (d.type() == DeviceType::DrumSampler)
    {
        auto& s = static_cast<DrumSamplerDevice&> (d);
        for (int p = 0; p < DrumSamplerDevice::kPads; ++p) s.loadPad (p, p < refs.size() ? refs[p] : juce::String(), sync);
    }
}

/** "NNN Name [Family].mbx-id" */
bool parseFactoryName (const juce::String& fn, int& index, juce::String& name, juce::String& family)
{
    if (fn.length() < 6 || ! juce::CharacterFunctions::isDigit (fn[0]) || ! juce::CharacterFunctions::isDigit (fn[1])
        || ! juce::CharacterFunctions::isDigit (fn[2]) || fn[3] != ' ')
        return false;
    index = fn.substring (0, 3).getIntValue();
    auto rest = fn.substring (4).upToLastOccurrenceOf (".", false, false).trim();
    family = {};
    const int ob = rest.lastIndexOfChar ('[');
    if (ob > 0 && rest.endsWithChar (']'))
    {
        family = rest.substring (ob + 1, rest.length() - 1).trim();
        rest = rest.substring (0, ob).trim();
    }
    name = rest;
    return name.isNotEmpty();
}

std::map<int, std::vector<PresetInfo>>& cache()
{
    static std::map<int, std::vector<PresetInfo>> c;
    return c;
}
} // namespace

juce::File factoryDir (DeviceType t) { return mb::factoryDir().getChildFile ("presets").getChildFile (deviceTypeId (t)); }

juce::File& userRootOverride() { static juce::File f; return f; }

void setUserRootForTest (const juce::File& root) { userRootOverride() = root; rescan(); }

juce::File userDir (DeviceType t)
{
    const auto root = userRootOverride() != juce::File() ? userRootOverride()
                                                         : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Minibox Presets");
    // user folders keep the original device names, so existing user presets stay where they are
    return root.getChildFile (t == DeviceType::Mono ? juce::String ("Mono") : t == DeviceType::Poly ? juce::String ("Poly") : deviceTypeName (t));
}

juce::File shadowDir (DeviceType t) { return userDir (t).getChildFile ("Factory"); }

juce::String fileExtension (DeviceType t) { return ".mbx-" + deviceTypeId (t); }

void rescan() { cache().clear(); }

const std::vector<PresetInfo>& factory (DeviceType t)
{
    auto it = cache().find ((int) t);
    if (it != cache().end()) return it->second;
    std::map<int, PresetInfo> byIndex;
    auto collect = [&] (const juce::File& dir, bool shadowed)
    {
        if (! dir.isDirectory()) return;
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*" + fileExtension (t)))
        {
            PresetInfo p;
            if (! parseFactoryName (f.getFileName(), p.index, p.name, p.family)) continue;
            p.factory = true; p.shadowed = shadowed; p.file = f;
            byIndex[p.index] = p;
        }
    };
    collect (factoryDir (t), false);
    collect (shadowDir (t), true);   // the user's overwrites replace the shipped files
    auto& out = cache()[(int) t];
    for (auto& [i, p] : byIndex) out.push_back (p);
    return out;
}

std::vector<PresetInfo> user (DeviceType t)
{
    std::vector<PresetInfo> out;
    for (auto& f : userDir (t).findChildFiles (juce::File::findFiles, false, "*" + fileExtension (t)))
    {
        PresetInfo p;
        p.name = f.getFileNameWithoutExtension();
        p.family = "User";
        p.factory = false; p.file = f;
        out.push_back (p);
    }
    std::sort (out.begin(), out.end(), [] (const PresetInfo& a, const PresetInfo& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return out;
}

std::vector<PresetInfo> list (DeviceType t)
{
    auto out = factory (t);
    for (auto& p : user (t)) out.push_back (p);
    return out;
}

std::vector<juce::String> families (DeviceType t)
{
    std::vector<juce::String> fams;
    for (auto& p : factory (t))
        if (p.family.isNotEmpty() && std::find (fams.begin(), fams.end(), p.family) == fams.end()) fams.push_back (p.family);
    return fams;
}

bool apply (DeviceProcessor& d, const PresetInfo& info, bool syncSamples)
{
    const auto json = juce::JSON::parse (info.file);
    if (! json.isObject() || deviceTypeFromId (json.getProperty ("type", "").toString()) != d.type()) return false;
    d.resetToDefaults();
    if (auto* vals = json.getProperty ("values", {}).getDynamicObject())
        for (auto& prop : vals->getProperties()) d.set (prop.name.toString(), (float) prop.value);
    d.setMacroString (json.getProperty ("macros", "").toString());
    juce::StringArray refs;
    if (auto* arr = json.getProperty ("samples", {}).getArray()) for (auto& s : *arr) refs.add (s.toString());
    if (d.type() == DeviceType::Sampler || d.type() == DeviceType::DrumSampler) setSamples (d, refs, syncSamples);
    d.presetName = info.name;
    return true;
}

void loadDefault (DeviceProcessor& d, bool syncSamples)
{
    const auto& f = factory (d.type());
    if (! f.empty()) apply (d, f.front(), syncSamples);
    else d.resetToDefaults();
}

const PresetInfo* findFactory (const DeviceProcessor& d)
{
    for (auto& p : factory (d.type()))
        if (p.name == d.presetName) return &p;
    return nullptr;
}

juce::String sanitiseName (juce::String name)
{
    name = name.trim().substring (0, 64).retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-()&'+.").trim();
    return name.isEmpty() ? juce::String ("Untitled") : name;
}

bool userPresetExists (DeviceType t, const juce::String& name)
{
    return userDir (t).getChildFile (sanitiseName (name) + fileExtension (t)).existsAsFile();
}

juce::var toJson (const DeviceProcessor& d, const juce::String& name, const juce::String& family)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", deviceTypeId (d.type()));
    o->setProperty ("version", DeviceProcessor::kVersion);
    o->setProperty ("name", name);
    o->setProperty ("family", family);
    auto* vals = new juce::DynamicObject();
    for (int i = 0; i < d.params().size(); ++i) vals->setProperty (d.params()[i].id, d.get (i));
    o->setProperty ("values", juce::var (vals));
    o->setProperty ("macros", d.macroString());
    juce::Array<juce::var> samples;
    if (d.type() == DeviceType::Sampler)
        for (int z = 0; z < SamplerDevice::kZones; ++z) samples.add (static_cast<const SamplerDevice&> (d).zoneRef (z));
    if (d.type() == DeviceType::DrumSampler)
        for (int p = 0; p < DrumSamplerDevice::kPads; ++p) samples.add (static_cast<const DrumSamplerDevice&> (d).padRef (p));
    o->setProperty ("samples", samples);
    return juce::var (o);
}

bool fromJson (DeviceProcessor& d, const juce::var& json)
{
    if (! json.isObject() || deviceTypeFromId (json.getProperty ("type", "").toString()) != d.type()) return false;
    d.resetToDefaults();
    if (auto* vals = json.getProperty ("values", {}).getDynamicObject())
        for (auto& prop : vals->getProperties()) d.set (prop.name.toString(), (float) prop.value);
    d.setMacroString (json.getProperty ("macros", "").toString());
    juce::StringArray refs;
    if (auto* arr = json.getProperty ("samples", {}).getArray()) for (auto& s : *arr) refs.add (s.toString());
    if (! refs.isEmpty()) setSamples (d, refs, false);
    d.presetName = json.getProperty ("name", "User").toString();
    if (json.getProperty ("type", "").toString() == "va") d.set ("engine", 1.0f);
    return true;
}

bool save (const DeviceProcessor& d, const juce::String& name)
{
    const auto clean = sanitiseName (name);
    auto dir = userDir (d.type());
    if (! dir.isDirectory() && ! dir.createDirectory()) return false;
    return dir.getChildFile (clean + fileExtension (d.type())).replaceWithText (juce::JSON::toString (toJson (d, clean, "User")));
}

bool overwriteFactory (const DeviceProcessor& d, const PresetInfo& p)
{
    if (! p.factory) return false;
    auto dir = shadowDir (d.type());
    if (! dir.isDirectory() && ! dir.createDirectory()) return false;
    const bool ok = dir.getChildFile (p.file.getFileName()).replaceWithText (juce::JSON::toString (toJson (d, p.name, p.family)));
    rescan();
    return ok;
}

bool remove (const PresetInfo& p)
{
    if (p.factory && ! p.shadowed) return false;   // shipped presets can't be deleted
    const auto dir = p.factory ? shadowDir (deviceTypeFromId (p.file.getFileExtension().fromFirstOccurrenceOf ("-", false, false)))
                               : userDir (deviceTypeFromId (p.file.getFileExtension().fromFirstOccurrenceOf ("-", false, false)));
    if (p.file.getParentDirectory() != dir) return false;   // own folders only
    const bool ok = p.file.deleteFile();
    rescan();
    return ok;
}
} // namespace mb::presets
