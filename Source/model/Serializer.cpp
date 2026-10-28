#include "Serializer.h"
#include "seq/Scales.h"
#include "devices/sampler/SamplerDevice.h"
#include "devices/drumsampler/DrumSamplerDevice.h"
#include "devices/SampleSlot.h"
#include "app/presets/PresetBank.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace mb::serializer
{
namespace
{
const char* kPatternNames = "ABCDEFGH";
juce::String deviceEntry (int p, int t) { return "devices/" + juce::String::charToString (kPatternNames[p]) + juce::String (t + 1) + ".xml"; }

juce::var mixerToJson (const Mixer& m)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("level", m.level); o->setProperty ("pan", m.pan);
    o->setProperty ("sendA", m.sendA); o->setProperty ("sendB", m.sendB);
    o->setProperty ("mute", m.mute); o->setProperty ("solo", m.solo);
    return juce::var (o);
}

/** A number from a file, inside its range (a damaged or hand-edited file can hold anything: NaN, inf, 1e9). */
float num (const juce::var& v, const char* key, float def, float lo, float hi)
{
    const float x = (float) v.getProperty (key, def);
    return std::isfinite (x) ? std::clamp (x, lo, hi) : def;
}

void mixerFromJson (Mixer& m, const juce::var& v)
{
    m.level = num (v, "level", 0.8f, 0.0f, 1.0f); m.pan = num (v, "pan", 0.0f, -1.0f, 1.0f);
    m.sendA = num (v, "sendA", 0.0f, 0.0f, 1.0f); m.sendB = num (v, "sendB", 0.0f, 0.0f, 1.0f);
    m.mute = (bool) v.getProperty ("mute", false); m.solo = (bool) v.getProperty ("solo", false);
}

juce::var trackToJson (const Track& tr, const DeviceProcessor* dev)
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> steps, lanes, locks;
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const auto& s = tr.steps[(size_t) i];
        if (s.noteCount == 0) continue;
        auto* so = new juce::DynamicObject();
        juce::Array<juce::var> notes, vels, lens;
        for (int k = 0; k < s.noteCount; ++k) { notes.add ((int) s.notes[k]); vels.add ((int) s.vel[k]); lens.add ((int) s.len[k]); }
        so->setProperty ("i", i); so->setProperty ("n", notes); so->setProperty ("v", vels);
        so->setProperty ("g", lens); so->setProperty ("m", (int) s.microTiming);
        so->setProperty ("p", (int) s.probability); so->setProperty ("r", (int) s.ratchet); so->setProperty ("f", (int) s.flags);
        if (s.cond != 0) so->setProperty ("c", (int) s.cond);
        if (s.muted != 0) so->setProperty ("mu", (int) s.muted);
        if (s.noteLock != 0) so->setProperty ("nl", (int) s.noteLock);
        {
            juce::Array<juce::var> offs; bool any = false;
            for (int k = 0; k < s.noteCount; ++k) { offs.add ((int) s.off[k]); any = any || s.off[k] != 0; }
            if (any) so->setProperty ("o", offs);
        }
        steps.add (juce::var (so));
    }
    for (int l = 0; l < kLanes; ++l)
        for (int i = 0; i < kMaxSteps; ++i)
        {
            const auto& d = tr.lanes[(size_t) l][(size_t) i];
            if (! d.on) continue;
            auto* dob = new juce::DynamicObject();
            dob->setProperty ("l", l); dob->setProperty ("i", i); dob->setProperty ("v", (int) d.velocity);
            dob->setProperty ("p", (int) d.probability); dob->setProperty ("r", (int) d.ratchet);
            dob->setProperty ("m", (int) d.microTiming); dob->setProperty ("f", (int) d.flags);
            if (d.cond != 0) dob->setProperty ("c", (int) d.cond);
            lanes.add (juce::var (dob));
        }
    for (auto& lane : tr.locks)
    {
        if (dev == nullptr || lane.param < 0 || lane.param >= dev->params().size() || ! lane.any()) continue;
        auto* lo = new juce::DynamicObject();
        lo->setProperty ("param", dev->params()[lane.param].id);
        auto* vals = new juce::DynamicObject();
        for (int i = 0; i < kMaxSteps; ++i)
            if (! std::isnan (lane.values[(size_t) i])) vals->setProperty (juce::String (i), lane.values[(size_t) i]);
        lo->setProperty ("values", juce::var (vals));
        locks.add (juce::var (lo));
    }
    o->setProperty ("steps", steps); o->setProperty ("lanes", lanes); o->setProperty ("locks", locks);
    if (tr.pianoRoll)
    {
        // a piano-roll clip: [start, length, note, velocity, flags, muted] in kPPQ ticks
        juce::Array<juce::var> clip;
        for (auto& n : tr.clip)
            clip.add (juce::Array<juce::var> { n.start, n.length, (int) n.note, (int) n.vel, (int) n.flags, n.muted ? 1 : 0, n.locked ? 1 : 0,
                                               (int) n.probability, (int) n.cond });
        o->setProperty ("pianoRoll", true);
        o->setProperty ("ppq", kPPQ);
        o->setProperty ("clip", clip);
        juce::Array<juce::var> lanes;
        for (auto& p : tr.cc) lanes.add (juce::Array<juce::var> { (int) p.lane, p.tick, p.value });
        if (! lanes.isEmpty()) o->setProperty ("cc", lanes);
    }
    return juce::var (o);
}

void trackFromJson (Track& tr, const juce::var& v, const DeviceProcessor* dev)
{
    tr.clear();
    if ((bool) v.getProperty ("pianoRoll", false))
    {
        tr.pianoRoll = true;
        const double scale = kPPQ / std::max (1.0, (double) v.getProperty ("ppq", kPPQ));
        if (auto* arr = v.getProperty ("clip", {}).getArray())
            for (auto& e : *arr)
            {
                if (! e.isArray() || e.size() < 4) continue;
                ClipNote n;
                n.start = std::max (0, (int) std::lround ((int) e[0] * scale));
                n.length = std::max (1, (int) std::lround ((int) e[1] * scale));
                n.note = (uint8_t) std::clamp ((int) e[2], 0, 127);
                n.vel = (uint8_t) std::clamp ((int) e[3], 1, 127);
                n.flags = e.size() > 4 ? (uint8_t) ((int) e[4] & 7) : 0;
                n.muted = e.size() > 5 && (int) e[5] != 0;
                n.locked = e.size() > 6 && (int) e[6] != 0;
                n.probability = (uint8_t) (e.size() > 7 ? std::clamp ((int) e[7], 0, 100) : 100);
                n.cond = (uint8_t) (e.size() > 8 ? std::clamp ((int) e[8], 0, kNumTrigConditions - 1) : 0);
                tr.addClipNote (n);
            }
        if (auto* arr = v.getProperty ("cc", {}).getArray())
            for (auto& e : *arr)
                if (e.isArray() && e.size() >= 3)
                    tr.setPoint (std::clamp ((int) e[0], 0, 255), std::max (0, (int) std::lround ((int) e[1] * scale)), (float) (double) e[2]);
    }
    if (auto* arr = v.getProperty ("steps", {}).getArray())
        for (auto& s : *arr)
        {
            const int i = (int) s.getProperty ("i", -1);
            if (i < 0 || i >= kMaxSteps) continue;
            auto& st = tr.steps[(size_t) i];
            // velocity and length per note (older files: one value for the whole step)
            const auto v = s.getProperty ("v", 100), g = s.getProperty ("g", 96);
            if (auto* notes = s.getProperty ("n", {}).getArray())
                for (int k = 0; k < notes->size(); ++k)
                    st.addNote ((int) (*notes)[k], v.isArray() ? (int) v[k] : (int) v, g.isArray() ? (int) g[k] : (int) g);
            st.microTiming = (int8_t) std::clamp ((int) s.getProperty ("m", 0), -24, 24);
            st.probability = (uint8_t) std::clamp ((int) s.getProperty ("p", 100), 0, 100);
            st.ratchet = (uint8_t) std::clamp ((int) s.getProperty ("r", 1), 1, 4);
            st.flags = (uint8_t) (int) s.getProperty ("f", 0);
            st.cond = (uint8_t) std::clamp ((int) s.getProperty ("c", 0), 0, kNumTrigConditions - 1);
            st.muted = (uint8_t) ((int) s.getProperty ("mu", 0) & 63);
            st.noteLock = (uint8_t) ((int) s.getProperty ("nl", 0) & 63);
            if (auto* offs = s.getProperty ("o", {}).getArray())
                for (int k = 0; k < std::min ((int) st.noteCount, offs->size()); ++k) st.off[k] = (int8_t) std::clamp ((int) (*offs)[k], -24, 24);
        }
    if (auto* arr = v.getProperty ("lanes", {}).getArray())
        for (auto& d : *arr)
        {
            const int l = (int) d.getProperty ("l", -1), i = (int) d.getProperty ("i", -1);
            if (l < 0 || l >= kLanes || i < 0 || i >= kMaxSteps) continue;
            auto& ds = tr.lanes[(size_t) l][(size_t) i];
            ds.on = true;
            ds.velocity = (uint8_t) std::clamp ((int) d.getProperty ("v", 100), 1, 127);
            ds.probability = (uint8_t) std::clamp ((int) d.getProperty ("p", 100), 0, 100);
            ds.ratchet = (uint8_t) std::clamp ((int) d.getProperty ("r", 1), 1, 4);
            ds.microTiming = (int8_t) std::clamp ((int) d.getProperty ("m", 0), -24, 24);
            ds.flags = (uint8_t) (int) d.getProperty ("f", 0);
            ds.cond = (uint8_t) std::clamp ((int) d.getProperty ("c", 0), 0, kNumTrigConditions - 1);
        }
    if (auto* arr = v.getProperty ("locks", {}).getArray())
        for (auto& lo : *arr)
        {
            if (dev == nullptr) break;
            const int param = dev->params().indexOf (lo.getProperty ("param", "").toString());
            if (param < 0) continue;
            auto* lane = tr.lockFor (param, true);
            if (lane == nullptr) break;
            if (auto* vals = lo.getProperty ("values", {}).getDynamicObject())
                for (auto& p : vals->getProperties())
                {
                    const int i = p.name.toString().getIntValue();
                    const float x = (float) p.value;
                    if (i >= 0 && i < kMaxSteps && std::isfinite (x)) lane->values[(size_t) i] = dev->params()[param].snap (x);
                }
        }
}

/** Every sample ref held by a device's state tree. */
void collectRefs (juce::ValueTree t, juce::StringArray& refs)
{
    for (auto name : { "Zones", "Pads" })
    {
        auto z = t.getChildWithName (name);
        for (int i = 0; i < z.getNumProperties(); ++i) refs.addIfNotAlreadyThere (z.getProperty (z.getPropertyName (i)).toString());
    }
}

void rewriteRefs (juce::ValueTree t, const std::map<juce::String, juce::String>& map)
{
    for (auto name : { "Zones", "Pads" })
    {
        auto z = t.getChildWithName (name);
        for (int i = 0; i < z.getNumProperties(); ++i)
        {
            const auto key = z.getPropertyName (i);
            auto it = map.find (z.getProperty (key).toString());
            if (it != map.end()) z.setProperty (key, it->second, nullptr);
        }
    }
}

juce::File sampleCacheDir (const juce::String& projectName)
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("Pastel Minibox").getChildFile ("ProjectSamples")
                 .getChildFile (juce::File::createLegalFileName (projectName) + "_" + juce::String (juce::Time::currentTimeMillis()));
    d.createDirectory();
    return d;
}

/** Builds a project and its devices from the parts of a .minibox archive. */
Result build (Engine& engine, const juce::var& json, std::function<juce::String (const juce::String&)> readEntry,
              std::function<juce::File (const juce::String&)> extractSample, const juce::File& projectFile)
{
    Result r;
    if (! json.isObject()) { r.error = "The project file is damaged."; return r; }
    auto proj = std::make_unique<Project>();
    std::array<std::array<std::unique_ptr<DeviceProcessor>, kTracks>, kPatterns> devs;
    projectFromJson (*proj, json);
    std::map<juce::String, juce::String> remap;
    for (int p = 0; p < kPatterns; ++p)
        for (int t = 0; t < kTracks; ++t)
        {
            const auto xml = readEntry (deviceEntry (p, t));
            if (xml.isEmpty()) continue;
            auto tree = juce::ValueTree::fromXml (xml);
            const auto type = deviceTypeFromId (tree.getProperty ("type").toString());
            if (type == DeviceType::Count) continue;
            juce::StringArray refs;
            collectRefs (tree, refs);
            for (auto& ref : refs)
            {
                if (ref.isEmpty() || ref.startsWith ("factory:") || remap.count (ref)) continue;
                if (ref.startsWith ("project:"))
                {
                    const auto f = extractSample (ref.fromFirstOccurrenceOf ("project:", false, false));
                    remap[ref] = f.existsAsFile() ? f.getFullPathName() : ref;
                    if (! f.existsAsFile()) r.missingSamples.add (ref);
                }
                else if (! juce::File (ref).existsAsFile())
                {
                    // relink by file name next to the project
                    auto near = projectFile.getParentDirectory().findChildFiles (juce::File::findFiles, true, juce::File (ref).getFileName());
                    if (! near.isEmpty()) remap[ref] = near[0].getFullPathName();
                    else r.missingSamples.add (ref);
                }
            }
            rewriteRefs (tree, remap);
            auto d = createDevice (type);
            d->load (tree);
            devs[(size_t) p][(size_t) t] = std::move (d);
        }
    // tracks need their devices to resolve lock parameter ids
    if (auto* pats = json.getProperty ("patterns", {}).getArray())
        for (int p = 0; p < std::min (kPatterns, pats->size()); ++p)
            if (auto* slots = (*pats)[p].getProperty ("slots", {}).getArray())
                for (int t = 0; t < std::min (kTracks, slots->size()); ++t)
                    trackFromJson (proj->patterns[(size_t) p].slots[(size_t) t].track, (*slots)[t].getProperty ("track", {}),
                                   devs[(size_t) p][(size_t) t].get());
    engine.replaceProject (std::move (proj), std::move (devs));
    r.ok = true;
    return r;
}
} // namespace

juce::var playToJson (const PlaySettings& ps)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("chordOn", ps.chordOn);
    juce::Array<juce::var> types;
    for (auto t : ps.chordType) types.add ((int) t);
    o->setProperty ("chordType", types);
    o->setProperty ("inversion", ps.inversion); o->setProperty ("invLower", ps.invLower);
    o->setProperty ("bassBelow", ps.bassBelow); o->setProperty ("strumMs", ps.strumMs);
    o->setProperty ("arpOn", ps.arpOn); o->setProperty ("arpLatch", ps.arpLatch); o->setProperty ("arpMode", ps.arpMode);
    o->setProperty ("arpRate", ps.arpRate); o->setProperty ("arpOct", ps.arpOct);
    o->setProperty ("arpSwing", ps.arpSwing); o->setProperty ("arpGate", ps.arpGate);
    return juce::var (o);
}

void playFromJson (PlaySettings& ps, const juce::var& v)
{
    if (! v.isObject()) return;
    ps.chordOn = (bool) v.getProperty ("chordOn", false);
    if (auto* types = v.getProperty ("chordType", {}).getArray())
        for (int i = 0; i < std::min (12, types->size()); ++i) ps.chordType[(size_t) i] = (int8_t) std::clamp ((int) (*types)[i], -1, 12);
    ps.inversion = std::clamp ((int) v.getProperty ("inversion", 0), 0, 2);
    ps.invLower = (bool) v.getProperty ("invLower", false);
    ps.bassBelow = (bool) v.getProperty ("bassBelow", false);
    ps.strumMs = num (v, "strumMs", 0.0f, 0.0f, 40.0f);
    ps.arpOn = (bool) v.getProperty ("arpOn", false);
    ps.arpLatch = (bool) v.getProperty ("arpLatch", false);
    ps.arpMode = std::clamp ((int) v.getProperty ("arpMode", 0), 0, 5);
    ps.arpRate = std::clamp ((int) v.getProperty ("arpRate", 6), 0, 14);
    ps.arpOct = std::clamp ((int) v.getProperty ("arpOct", 0), 0, 3);
    ps.arpSwing = num (v, "arpSwing", 0.0, 0.0f, 60.0f);
    ps.arpGate = num (v, "arpGate", 90.0, 10.0f, 100.0f);
}

juce::var projectToJson (const Project& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "minibox-project");
    o->setProperty ("version", 1);
    o->setProperty ("name", p.name);
    o->setProperty ("bpm", p.bpm);
    o->setProperty ("beats", p.beats); o->setProperty ("unit", p.unit);
    o->setProperty ("swing", p.swing); o->setProperty ("countIn", p.countIn);
    o->setProperty ("metronome", p.metronome); o->setProperty ("clickLevel", p.clickLevel);
    o->setProperty ("switchQuantize", p.switchQuantize);
    o->setProperty ("recordQuantize", p.recordQuantize); o->setProperty ("overdub", p.overdub);
    o->setProperty ("perform", p.perform);
    o->setProperty ("recordMode", p.recordMode); o->setProperty ("stepLength", p.stepLength);
    o->setProperty ("channelPerTrack", p.channelPerTrack);
    auto* sc = new juce::DynamicObject();
    sc->setProperty ("lock", p.scale.lock); sc->setProperty ("root", p.scale.root); sc->setProperty ("scale", p.scale.scale);
    sc->setProperty ("snap", p.scale.snap); sc->setProperty ("whiteKeys", p.scale.whiteKeys);
    o->setProperty ("scale", juce::var (sc));
    auto* m = new juce::DynamicObject();
    const auto& ms = p.master;
    m->setProperty ("delayReturn", ms.delayReturn); m->setProperty ("delayFeedback", ms.delayFeedback);
    m->setProperty ("delayTone", ms.delayTone); m->setProperty ("delayDiv", ms.delayDiv);
    m->setProperty ("reverbReturn", ms.reverbReturn); m->setProperty ("reverbDecay", ms.reverbDecay);
    m->setProperty ("reverbDamp", ms.reverbDamp); m->setProperty ("glue", ms.glue);
    m->setProperty ("limitDb", ms.limitDb); m->setProperty ("volume", ms.volume); m->setProperty ("lookAhead", ms.lookAhead);
    o->setProperty ("master", juce::var (m));
    auto slots = [] (const auto& arr)
    {
        juce::Array<juce::var> out;
        for (const auto& fx : arr)
        {
            auto* so = new juce::DynamicObject();
            so->setProperty ("effect", fx.effect);
            juce::Array<juce::var> norm;
            for (float v : fx.norm) norm.add (v);
            so->setProperty ("norm", norm);
            so->setProperty ("mode", fx.mode); so->setProperty ("mix", fx.mix); so->setProperty ("bypass", fx.bypass);
            out.add (juce::var (so));
        }
        return out;
    };
    o->setProperty ("masterFx", slots (p.masterFx));
    o->setProperty ("performFx", slots (p.performFx));
    juce::Array<juce::var> pats;
    for (auto& pat : p.patterns)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("lengthBars", pat.lengthBars);
        po->setProperty ("stepRes", pat.stepRes);
        if (pat.loopOn || pat.loopEnd > pat.loopStart)
        {
            po->setProperty ("loopOn", pat.loopOn);
            po->setProperty ("loopStart", pat.loopStart);
            po->setProperty ("loopEnd", pat.loopEnd);
        }
        juce::Array<juce::var> slots;
        for (auto& s : pat.slots)
        {
            auto* so = new juce::DynamicObject();
            so->setProperty ("mixer", mixerToJson (s.mixer));
            so->setProperty ("play", playToJson (s.play));
            slots.add (juce::var (so));
        }
        po->setProperty ("slots", slots);
        pats.add (juce::var (po));
    }
    o->setProperty ("patterns", pats);
    return juce::var (o);
}

void projectFromJson (Project& p, const juce::var& v)
{
    p.name = v.getProperty ("name", "Untitled").toString();
    p.bpm = std::clamp ((double) v.getProperty ("bpm", 120.0), 30.0, 300.0);
    p.beats = std::clamp ((int) v.getProperty ("beats", 4), 1, 16);
    p.unit = (int) v.getProperty ("unit", 4);
    if (p.unit != 4 && p.unit != 8 && p.unit != 16) p.unit = 4;
    p.swing = num (v, "swing", 0.5f, 0.5f, 0.75f);
    p.countIn = (bool) v.getProperty ("countIn", true);
    p.metronome = (bool) v.getProperty ("metronome", false);
    p.clickLevel = num (v, "clickLevel", 0.5f, 0.0f, 1.0f);
    p.switchQuantize = std::clamp ((int) v.getProperty ("switchQuantize", 0), 0, 2);
    p.recordQuantize = num (v, "recordQuantize", 1.0f, 0.0f, 1.0f);
    p.overdub = (bool) v.getProperty ("overdub", false);
    p.perform = (bool) v.getProperty ("perform", false);
    p.recordMode = std::clamp ((int) v.getProperty ("recordMode", 0), 0, 1);
    p.stepLength = std::clamp ((int) v.getProperty ("stepLength", 0), 0, 3);
    p.channelPerTrack = (bool) v.getProperty ("channelPerTrack", false);
    auto sc = v.getProperty ("scale", {});
    p.scale.lock = (bool) sc.getProperty ("lock", false);
    p.scale.root = std::clamp ((int) sc.getProperty ("root", 0), 0, 11);
    p.scale.scale = std::clamp ((int) sc.getProperty ("scale", 1), 0, (int) Scales::factoryScales().size() - 1);
    p.scale.snap = std::clamp ((int) sc.getProperty ("snap", 0), 0, 2);
    p.scale.whiteKeys = (bool) sc.getProperty ("whiteKeys", false);
    auto m = v.getProperty ("master", {});
    auto& ms = p.master;
    ms.delayReturn = num (m, "delayReturn", ms.delayReturn, 0.0f, 1.0f);
    ms.delayFeedback = num (m, "delayFeedback", ms.delayFeedback, 0.0f, 0.92f);
    ms.delayTone = num (m, "delayTone", ms.delayTone, 0.0f, 1.0f);
    ms.delayDiv = std::clamp ((int) m.getProperty ("delayDiv", ms.delayDiv), 0, dsp::kNumDivisions - 1);
    ms.reverbReturn = num (m, "reverbReturn", ms.reverbReturn, 0.0f, 1.0f);
    ms.reverbDecay = num (m, "reverbDecay", ms.reverbDecay, 0.0f, 1.0f);
    ms.reverbDamp = num (m, "reverbDamp", ms.reverbDamp, 0.0f, 1.0f);
    ms.glue = num (m, "glue", ms.glue, 0.0f, 1.0f);
    ms.limitDb = num (m, "limitDb", ms.limitDb, -12.0f, 0.0f);
    ms.volume = num (m, "volume", ms.volume, 0.0f, 1.2f);
    ms.lookAhead = (bool) m.getProperty ("lookAhead", true);
    auto readSlots = [&v] (const char* key, auto& arr)
    {
        auto* a = v.getProperty (key, {}).getArray();
        for (size_t i = 0; i < arr.size(); ++i)
        {
            auto& fx = arr[i];
            fx = FxSlot();
            if (a == nullptr || (int) i >= a->size()) continue;
            const auto& so = (*a)[(int) i];
            fx.effect = so.getProperty ("effect", "").toString();
            if (auto* norm = so.getProperty ("norm", {}).getArray())
                for (int k = 0; k < std::min (FxSlot::kParams, norm->size()); ++k) fx.norm[(size_t) k] = std::clamp ((float) (*norm)[k], 0.0f, 1.0f);
            fx.mode = (int) so.getProperty ("mode", 0);
            fx.mix = std::clamp ((float) so.getProperty ("mix", 1.0f), 0.0f, 1.0f);
            fx.bypass = (bool) so.getProperty ("bypass", false);
        }
    };
    readSlots ("masterFx", p.masterFx);
    readSlots ("performFx", p.performFx);
    if (auto* pats = v.getProperty ("patterns", {}).getArray())
        for (int i = 0; i < std::min (kPatterns, pats->size()); ++i)
        {
            auto& pat = p.patterns[(size_t) i];
            pat.stepRes = std::clamp ((int) (*pats)[i].getProperty ("stepRes", 1), 0, 2);
            pat.lengthBars = std::clamp ((int) (*pats)[i].getProperty ("lengthBars", 1), 1, p.maxBarsFor (i));
            pat.loopOn = (bool) (*pats)[i].getProperty ("loopOn", false);
            pat.loopStart = std::clamp ((int) (*pats)[i].getProperty ("loopStart", 0), 0, kMaxSteps - 1);
            pat.loopEnd = std::clamp ((int) (*pats)[i].getProperty ("loopEnd", 0), 0, kMaxSteps);
            if (auto* slots = (*pats)[i].getProperty ("slots", {}).getArray())
                for (int t = 0; t < std::min (kTracks, slots->size()); ++t)
                {
                    mixerFromJson (pat.slots[(size_t) t].mixer, (*slots)[t].getProperty ("mixer", {}));
                    playFromJson (pat.slots[(size_t) t].play, (*slots)[t].getProperty ("play", {}));
                }
        }
}

Result save (Engine& engine, const juce::File& file)
{
    Result r;
    juce::ZipFile::Builder zip;
    std::vector<std::unique_ptr<juce::MemoryBlock>> keep;
    auto addText = [&] (const juce::String& path, const juce::String& text)
    {
        auto mb = std::make_unique<juce::MemoryBlock> (text.toRawUTF8(), text.getNumBytesAsUTF8());
        zip.addEntry (new juce::MemoryInputStream (*mb, false), 9, path, juce::Time::getCurrentTime());
        keep.push_back (std::move (mb));
    };
    juce::var json;
    std::map<juce::String, juce::String> remap;
    {
        const Engine::ScopedEdit edit (engine);
        auto& proj = engine.project();
        if (proj.name == "Untitled" || proj.name.isEmpty()) proj.name = file.getFileNameWithoutExtension();
        json = projectToJson (proj);
        // tracks carry lock parameter ids from their devices
        if (auto* pats = json.getProperty ("patterns", {}).getArray())
            for (int p = 0; p < kPatterns; ++p)
                if (auto* slots = (*pats)[p].getProperty ("slots", {}).getArray())
                    for (int t = 0; t < kTracks; ++t)
                        if (auto* so = (*slots)[t].getDynamicObject())
                            so->setProperty ("track", trackToJson (proj.patterns[(size_t) p].slots[(size_t) t].track, engine.device (p, t)));
        for (int p = 0; p < kPatterns; ++p)
            for (int t = 0; t < kTracks; ++t)
                if (auto* d = engine.device (p, t))
                {
                    juce::ValueTree tree ("Device");
                    d->save (tree);
                    juce::StringArray refs;
                    collectRefs (tree, refs);
                    for (auto& ref : refs)
                    {
                        if (ref.isEmpty() || ref.startsWith ("factory:") || remap.count (ref)) continue;
                        const juce::File f (ref.startsWith ("project:") ? juce::String() : ref);
                        if (! f.existsAsFile()) { r.missingSamples.add (ref); continue; }
                        juce::String name = f.getFileName();
                        int n = 1;
                        for (auto& kv : remap) if (kv.second == "project:" + name) name = f.getFileNameWithoutExtension() + "_" + juce::String (++n) + f.getFileExtension();
                        remap[ref] = "project:" + name;
                        zip.addFile (f, 0, "samples/" + name);
                    }
                    rewriteRefs (tree, remap);
                    addText (deviceEntry (p, t), tree.toXmlString());
                }
    }
    addText ("project.json", juce::JSON::toString (json));
    auto tmp = file.getSiblingFile (file.getFileName() + ".tmp");
    {
        juce::FileOutputStream out (tmp);
        if (! out.openedOk()) { r.error = "Can't write to " + file.getFullPathName(); return r; }
        out.setPosition (0); out.truncate();
        if (! zip.writeToStream (out, nullptr)) { r.error = "Writing the project failed."; return r; }
    }
    if (! tmp.moveFileTo (file)) { r.error = "Can't replace " + file.getFullPathName(); return r; }
    r.ok = true;
    return r;
}

Result load (Engine& engine, const juce::File& file)
{
    Result r;
    juce::ZipFile zip (file);
    if (zip.getNumEntries() == 0) { r.error = "Not a Minibox project: " + file.getFileName(); return r; }
    auto readEntry = [&zip] (const juce::String& path) -> juce::String
    {
        if (auto* e = zip.getEntry (path))
            if (std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (*e)); s) return s->readEntireStreamAsString();
        return {};
    };
    const auto json = juce::JSON::parse (readEntry ("project.json"));
    juce::File cache;
    auto extract = [&] (const juce::String& name) -> juce::File
    {
        if (cache == juce::File()) cache = sampleCacheDir (file.getFileNameWithoutExtension());
        auto* e = zip.getEntry ("samples/" + name);
        auto out = cache.getChildFile (name);
        if (e != nullptr)
            if (std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (*e)); s)
            {
                juce::FileOutputStream fo (out);
                if (fo.openedOk()) fo.writeFromInputStream (*s, -1);
            }
        return out;
    };
    return build (engine, json, readEntry, extract, file);
}

juce::MemoryBlock snapshot (Engine& engine)
{
    auto tmp = juce::File::createTempFile (".minibox");
    save (engine, tmp);
    juce::MemoryBlock mb;
    tmp.loadFileAsData (mb);
    tmp.deleteFile();
    return mb;
}

Result restore (Engine& engine, const juce::MemoryBlock& data)
{
    auto tmp = juce::File::createTempFile (".minibox");
    tmp.replaceWithData (data.getData(), data.getSize());
    auto r = load (engine, tmp);
    tmp.deleteFile();
    return r;
}

Result exportWav (Engine& live, const juce::File& file, int loops, double sr)
{
    Result r;
    const auto snap = snapshot (live);
    const int pattern = live.activePattern();
    Engine off;
    off.prepare (sr, 512);
    forceSyncSampleLoads() = true;
    r = restore (off, snap);
    forceSyncSampleLoads() = false;
    if (! r.ok) return r;
    off.queuePattern (pattern);
    off.focusPattern = pattern;
    std::vector<float> l (512), rr (512);
    off.process (l.data(), rr.data(), 512, nullptr);   // applies the pattern selection
    off.play();
    const auto& proj = off.project();
    const double seconds = proj.patternSteps (pattern) * proj.stepQuarters (pattern) * 60.0 / proj.bpm * std::max (1, loops) + 3.0;
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> buf (2, total);
    for (int pos = 0; pos < total; pos += 512)
    {
        const int n = std::min (512, total - pos);
        off.process (l.data(), rr.data(), n, nullptr);
        buf.copyFrom (0, pos, l.data(), n);
        buf.copyFrom (1, pos, rr.data(), n);
        if (pos + n >= total - (int) (3.0 * sr) && off.isPlaying()) off.stop();   // let the tails ring for 3 s
    }
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
    if (os == nullptr) { r.ok = false; r.error = "Can't write " + file.getFullPathName(); return r; }
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
    if (w == nullptr) { r.ok = false; r.error = "WAV writer failed."; return r; }
    os.release();
    w->writeFromAudioSampleBuffer (buf, 0, total);
    r.ok = true;
    return r;
}

Result saveRack (Engine& engine, int pattern, const juce::File& file)
{
    Result r;
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "minibox-rack");
    juce::Array<juce::var> slots;
    {
        const Engine::ScopedEdit edit (engine);
        for (int t = 0; t < kTracks; ++t)
        {
            auto* so = new juce::DynamicObject();
            so->setProperty ("mixer", mixerToJson (engine.project().patterns[(size_t) pattern].slots[(size_t) t].mixer));
            if (auto* d = engine.device (pattern, t)) so->setProperty ("device", presets::toJson (*d, d->presetName, "Rack"));
            slots.add (juce::var (so));
        }
    }
    o->setProperty ("slots", slots);
    r.ok = file.replaceWithText (juce::JSON::toString (juce::var (o)));
    if (! r.ok) r.error = "Can't write " + file.getFullPathName();
    return r;
}

Result loadRack (Engine& engine, int pattern, const juce::File& file)
{
    Result r;
    const auto json = juce::JSON::parse (file);
    auto* slots = json.getProperty ("slots", {}).getArray();
    if (slots == nullptr) { r.error = "Not a rack preset."; return r; }
    for (int t = 0; t < std::min (kTracks, slots->size()); ++t)
    {
        const auto dv = (*slots)[t].getProperty ("device", {});
        std::unique_ptr<DeviceProcessor> d;
        if (dv.isObject())
        {
            const auto type = deviceTypeFromId (dv.getProperty ("type", "").toString());
            if (type != DeviceType::Count) { d = createDevice (type); presets::fromJson (*d, dv); }
        }
        auto old = engine.setDevice (pattern, t, std::move (d));
        const Engine::ScopedEdit edit (engine);
        mixerFromJson (engine.project().patterns[(size_t) pattern].slots[(size_t) t].mixer, (*slots)[t].getProperty ("mixer", {}));
    }
    r.ok = true;
    return r;
}
} // namespace mb::serializer
