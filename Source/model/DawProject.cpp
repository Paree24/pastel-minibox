#include "DawProject.h"
#include "PluginState.h"
#include "devices/acid/AcidDevice.h"

namespace mb::dawproject
{
namespace
{
juce::String num (double v) { return juce::String (v, 6); }

uint32_t fourCC (const char* s)
{
    return (uint32_t) (uint8_t) s[0] << 24 | (uint32_t) (uint8_t) s[1] << 16 | (uint32_t) (uint8_t) s[2] << 8 | (uint32_t) (uint8_t) s[3];
}

/** A host parameter's VST3 ID, as the JUCE wrapper reports it to the DAW (the ID's hash, kept positive). */
int vstParamId (const juce::String& id) { return (int) ((uint32_t) id.hashCode() & 0x7fffffffu); }

/** The controller lanes a DAW plays as MIDI expressions (macros go to the plugin's parameters instead). */
bool isExpressionLane (int lane) { return lane == 0 || lane == 1 || lane == 11 || lane == 128; }

/** One DAW track: a Minibox track with one instrument (a track whose instrument differs between patterns
    becomes one DAW track per instrument). */
struct Group
{
    int track = 0;
    juce::String key, name, id, channelId;
    std::vector<int> patterns;
    juce::MemoryBlock state;
    bool mute = false, solo = false;
    std::array<float, 8> macros {};
    std::array<bool, 8> hasMacro {};
    std::map<int, std::vector<Note>> notes;          // per pattern
    std::map<int, std::vector<CCPoint>> cc;          // per pattern
};

/** Everything the file needs, copied under the engine's lock (the file is written after it). */
struct Snapshot
{
    juce::String name;
    double bpm = 120.0;
    int beats = 4, unit = 4;
    std::vector<int> used;                           // patterns on the timeline, in order
    std::map<int, double> lengthQ;                   // pattern → quarter notes
    std::vector<Group> groups;
};

Snapshot take (Engine& engine)
{
    Snapshot s;
    const Engine::ScopedEdit edit (engine);
    const auto& p = engine.project();
    s.name = p.name; s.bpm = p.bpm; s.beats = p.beats; s.unit = p.unit;
    for (int pt = 0; pt < kPatterns; ++pt)
    {
        bool any = false;
        for (int t = 0; t < kTracks && ! any; ++t) any = ! notesOf (engine, pt, t).empty();
        if (any) s.used.push_back (pt);
    }
    // nothing played anywhere: the patterns that hold instruments, so the tracks still arrive
    if (s.used.empty())
        for (int pt = 0; pt < kPatterns; ++pt)
            for (int t = 0; t < kTracks; ++t)
                if (engine.device (pt, t) != nullptr && (s.used.empty() || s.used.back() != pt)) s.used.push_back (pt);
    for (int pt : s.used) s.lengthQ[pt] = p.patternTicks (pt) / (double) kPPQ;

    for (int t = 0; t < kTracks; ++t)
        for (int pt : s.used)
        {
            auto* d = engine.device (pt, t);
            if (d == nullptr) continue;
            const auto& slot = p.patterns[(size_t) pt].slots[(size_t) t];
            juce::ValueTree dt ("Device");
            d->save (dt);
            const auto& m = slot.mixer;
            const auto key = dt.toXmlString() + juce::String (m.level) + "/" + juce::String (m.pan) + "/" + juce::String (m.sendA) + "/"
                           + juce::String (m.sendB) + "/" + juce::String ((int) m.mute) + juce::String ((int) m.solo);
            Group* g = nullptr;
            for (auto& x : s.groups) if (x.track == t && x.key == key) g = &x;
            if (g == nullptr)
            {
                s.groups.emplace_back();
                g = &s.groups.back();
                g->track = t; g->key = key;
                g->name = juce::String (t + 1) + " " + deviceTypeName (d->type()) + (d->presetName.isNotEmpty() ? " - " + d->presetName : juce::String());
                g->mute = m.mute; g->solo = m.solo;
                const auto& ms = p.master;
                std::vector<std::pair<juce::String, float>> host;
                for (int k = 0; k < 8; ++k)
                {
                    g->macros[(size_t) k] = d->getMacro (k);
                    host.push_back ({ "macro" + juce::String (k + 1), g->macros[(size_t) k] });
                }
                host.push_back ({ "level", m.level });
                host.push_back ({ "pan", (m.pan + 1.0f) * 0.5f });
                host.push_back ({ "mute", 0.0f });                       // the DAW channel mutes (and solos) instead
                host.push_back ({ "masterVolume", ms.volume / 1.2f });
                host.push_back ({ "delayReturn", ms.delayReturn });
                host.push_back ({ "reverbReturn", ms.reverbReturn });
                g->state = pluginstate::toBytes (pluginstate::make (d, host, m, ms));
            }
            g->patterns.push_back (pt);
            g->notes[pt] = notesOf (engine, pt, t);
            if (slot.track.pianoRoll) g->cc[pt] = slot.track.cc;
            for (auto& c : slot.track.cc) if (c.lane >= 100 && c.lane < 108 && slot.track.pianoRoll) g->hasMacro[(size_t) (c.lane - 100)] = true;
        }
    // two DAW tracks from one Minibox track: name them by their patterns
    for (auto& g : s.groups)
    {
        int same = 0;
        for (auto& x : s.groups) same += x.track == g.track;
        if (same < 2) continue;
        juce::String pats;
        for (int pt : g.patterns) pats << juce::String::charToString ("ABCDEFGH"[pt]);
        g.name << " (" << pats << ")";
    }
    return s;
}

juce::XmlElement* child (juce::XmlElement& parent, const juce::String& tag, std::initializer_list<std::pair<const char*, juce::String>> attrs = {})
{
    auto* e = parent.createNewChildElement (tag);
    for (auto& [k, v] : attrs) e->setAttribute (k, v);
    return e;
}
} // namespace

// ------------------------------------------------------------------------------------------- notes
std::vector<Note> notesOf (Engine& engine, int pattern, int track)
{
    std::vector<Note> out;
    auto* dev = engine.device (pattern, track);
    if (dev == nullptr) return out;
    const auto& p = engine.project();
    const auto& tr = p.patterns[(size_t) pattern].slots[(size_t) track].track;
    const int stT = p.stepTicks (pattern), steps = p.patternSteps (pattern);
    const double swing = (p.swing - 0.5) * 2.0 * stT;   // ticks the odd steps play late
    const bool acid = dev->type() == DeviceType::Acid, drums = dev->laneCount() > 0;
    // as the sequencer plays it: an accent is a full-velocity hit (drums and the acid line read it so); an acid
    // slide or tie holds the Slide keyswitch over the next note's start (docs/dawproject.md)
    auto add = [&] (double tick, double len, int key, int vel, int flags)
    {
        const bool accent = (flags & kAccentFlag) != 0 && (acid || drums);
        Note n;
        n.time = std::max (0.0, tick) / kPPQ;
        n.duration = std::max (1.0, len) / kPPQ;
        n.key = key;
        n.vel = accent ? 1.0f : std::clamp (vel, 1, 127) / 127.0f;
        out.push_back (n);
        if (acid && (flags & (kSlideFlag | kTieFlag)) != 0)
        {
            Note ks;
            ks.time = n.time + n.duration * 0.5;
            ks.duration = n.duration * 0.5;
            ks.key = AcidDevice::kKeyswitchLow + 1;   // D0: Slide
            ks.vel = 0.8f;
            out.push_back (ks);
        }
    };
    auto stepStart = [&] (int si) { return si * (double) stT + ((si & 1) ? swing : 0.0); };
    if (drums)
    {
        for (int l = 0; l < std::min (dev->laneCount(), kLanes); ++l)
            for (int si = 0; si < steps; ++si)
            {
                const auto& d = tr.lanes[(size_t) l][(size_t) si];
                if (! d.on) continue;
                const int r = std::clamp ((int) d.ratchet, 1, 4);
                for (int k = 0; k < r; ++k)
                    add (stepStart (si) + d.microTiming * stT / (double) kTicksPerStep + k * stT / (double) r, stT / (double) r * 0.8,
                         dev->laneNote (l), d.velocity, d.flags);
            }
    }
    else if (tr.pianoRoll)
    {
        const int total = p.patternTicks (pattern);
        for (auto& c : tr.clip)
        {
            if (c.muted || c.start >= total) continue;
            const bool onOddStep = c.start % stT == 0 && ((c.start / stT) & 1);
            add (c.start + (onOddStep ? swing : 0.0), c.length, c.note, c.vel, c.flags);
        }
    }
    else
    {
        for (int si = 0; si < steps; ++si)
        {
            const auto& st = tr.steps[(size_t) si];
            const int r = std::clamp ((int) st.ratchet, 1, 4);
            for (int k = 0; k < r; ++k)
                for (int i = 0; i < st.noteCount; ++i)
                {
                    if (st.isMuted (i)) continue;
                    double len = (st.flags & (kSlideFlag | kTieFlag)) ? stT + stT / 16.0 : st.len[i] * stT / 128.0;
                    if (r > 1) len = std::min (len, stT / (double) r * 0.9);
                    add (stepStart (si) + (st.microTiming + st.off[i]) * stT / (double) kTicksPerStep + k * stT / (double) r, len,
                         st.notes[i], st.vel[i], st.flags);
                }
        }
    }
    std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.time < b.time; });
    return out;
}

// ------------------------------------------------------------------------------------------- plugin files
juce::String pluginClassId()
{
    // the JUCE wrapper's component class: two fixed words, then the manufacturer and plugin codes
    juce::String hex;
    for (uint32_t w : { 0xABCDEF01u, 0x9182FAEBu, fourCC ("Pstl"), fourCC ("Mnbx") }) hex << juce::String::toHexString ((juce::int64) w).paddedLeft ('0', 8);
    return hex.toUpperCase();
}

juce::MemoryBlock vstPreset (const juce::MemoryBlock& comp)
{
    // VST3 preset file: header ("VST3", version 1, class ID, offset of the chunk list), the component state,
    // then the chunk list naming it ("Comp")
    constexpr juce::int64 header = 48;
    juce::MemoryOutputStream o;
    o.write ("VST3", 4);
    o.writeInt (1);
    o.write (pluginClassId().toRawUTF8(), 32);
    o.writeInt64 (header + (juce::int64) comp.getSize());
    o.write (comp.getData(), comp.getSize());
    o.write ("List", 4);
    o.writeInt (1);
    o.write ("Comp", 4);
    o.writeInt64 (header);
    o.writeInt64 ((juce::int64) comp.getSize());
    return o.getMemoryBlock();
}

// ------------------------------------------------------------------------------------------- the file
Result exportProject (Engine& engine, const juce::File& file)
{
    Result r;
    auto s = take (engine);
    if (s.groups.empty()) { r.error = "The project has no instruments to export."; return r; }

    int nextId = 0;
    auto newId = [&nextId] { return "id" + juce::String (nextId++); };
    juce::XmlElement project ("Project");
    project.setAttribute ("version", "1.0");
    child (project, "Application", { { "name", "Pastel Minibox" }, { "version", "1.0" } });
    auto* transport = child (project, "Transport");
    child (*transport, "Tempo", { { "unit", "bpm" }, { "value", num (s.bpm) }, { "min", "20.000000" }, { "max", "300.000000" },
                                  { "id", newId() }, { "name", "Tempo" } });
    child (*transport, "TimeSignature", { { "numerator", juce::String (s.beats) }, { "denominator", juce::String (s.unit) }, { "id", newId() } });

    auto* structure = child (project, "Structure");
    const auto masterTrackId = newId(), masterChannelId = newId();
    std::vector<std::array<juce::String, 8>> groupMacroIds (s.groups.size());
    juce::ZipFile::Builder zip;
    auto addFile = [&zip] (const juce::String& path, const juce::MemoryBlock& data)
    {
        zip.addEntry (new juce::MemoryInputStream (data, true), 9, path, juce::Time::getCurrentTime());
    };
    auto mixParams = [&] (juce::XmlElement& ch, bool mute)
    {
        child (ch, "Mute", { { "value", mute ? "true" : "false" }, { "id", newId() }, { "name", "Mute" } });
        child (ch, "Pan", { { "unit", "normalized" }, { "value", "0.500000" }, { "min", "0.000000" }, { "max", "1.000000" }, { "id", newId() }, { "name", "Pan" } });
        child (ch, "Volume", { { "unit", "linear" }, { "value", "1.000000" }, { "min", "0.000000" }, { "max", "2.000000" }, { "id", newId() }, { "name", "Volume" } });
    };
    for (size_t gi = 0; gi < s.groups.size(); ++gi)
    {
        auto& g = s.groups[gi];
        g.id = newId(); g.channelId = newId();
        auto* track = child (*structure, "Track", { { "contentType", "notes" }, { "loaded", "true" }, { "id", g.id }, { "name", g.name } });
        auto* ch = child (*track, "Channel", { { "audioChannels", "2" }, { "destination", masterChannelId }, { "role", "regular" },
                                              { "solo", g.solo ? "true" : "false" }, { "id", g.channelId } });
        auto* devices = child (*ch, "Devices");
        const auto statePath = "plugins/" + g.id + ".vstpreset";
        auto* plug = child (*devices, "Vst3Plugin", { { "deviceID", pluginClassId() }, { "deviceName", "Minibox" }, { "deviceRole", "instrument" },
                                                     { "deviceVendor", "Pastel" }, { "pluginVersion", "1.0.0" }, { "loaded", "true" },
                                                     { "id", newId() }, { "name", "Minibox" } });
        auto* params = child (*plug, "Parameters");
        for (int m = 0; m < 8; ++m)
        {
            groupMacroIds[gi][(size_t) m] = newId();
            child (*params, "RealParameter", { { "parameterID", juce::String (vstParamId ("macro" + juce::String (m + 1))) },
                                               { "unit", "normalized" }, { "value", num (g.macros[(size_t) m]) }, { "min", "0.000000" },
                                               { "max", "1.000000" }, { "id", groupMacroIds[gi][(size_t) m] }, { "name", "Macro " + juce::String (m + 1) } });
        }
        child (*plug, "Enabled", { { "value", "true" }, { "id", newId() }, { "name", "On/Off" } });
        child (*plug, "State", { { "path", statePath } });
        addFile (statePath, vstPreset (g.state));
        mixParams (*ch, g.mute);
    }
    {
        auto* master = child (*structure, "Track", { { "contentType", "audio notes" }, { "loaded", "true" }, { "id", masterTrackId }, { "name", "Master" } });
        auto* ch = child (*master, "Channel", { { "audioChannels", "2" }, { "role", "master" }, { "solo", "false" }, { "id", masterChannelId } });
        mixParams (*ch, false);
    }

    // a pattern's clip: its notes, and its bend / mod wheel / expression / pressure lanes as MIDI expressions
    auto clipContent = [&] (juce::XmlElement& clip, const Group& g, int pt)
    {
        std::map<int, std::vector<const CCPoint*>> lanes;
        if (auto it = g.cc.find (pt); it != g.cc.end())
            for (auto& c : it->second) if (isExpressionLane (c.lane)) lanes[c.lane].push_back (&c);
        auto* holder = lanes.empty() ? &clip : child (clip, "Lanes", { { "id", newId() } });
        auto* notes = child (*holder, "Notes", { { "id", newId() } });
        for (auto& n : g.notes.at (pt))
            child (*notes, "Note", { { "time", num (n.time) }, { "duration", num (n.duration) }, { "channel", "0" },
                                     { "key", juce::String (n.key) }, { "vel", num (n.vel) }, { "rel", num (n.vel) } });
        for (auto& [lane, pts] : lanes)
        {
            auto* points = child (*holder, "Points", { { "unit", "normalized" }, { "id", newId() } });
            auto* target = child (*points, "Target", { { "channel", "0" } });
            if (lane == 0) target->setAttribute ("expression", "pitchBend");
            else if (lane == 128) target->setAttribute ("expression", "channelPressure");
            else { target->setAttribute ("expression", "channelController"); target->setAttribute ("controller", lane); }
            for (auto* c : pts)
                child (*points, "RealPoint", { { "time", num (c->tick / (double) kPPQ) },
                                               { "value", num (lane == 0 ? (c->value + 1.0f) * 0.5f : c->value) }, { "interpolation", "hold" } });
        }
    };

    // the timeline: the patterns one after another
    std::map<int, double> offset;
    double at = 0.0;
    for (int pt : s.used) { offset[pt] = at; at += s.lengthQ[pt]; }
    auto* arrangement = child (project, "Arrangement", { { "id", newId() } });
    auto* lanes = child (*arrangement, "Lanes", { { "timeUnit", "beats" }, { "id", newId() } });
    for (size_t gi = 0; gi < s.groups.size(); ++gi)
    {
        auto& g = s.groups[gi];
        auto* lane = child (*lanes, "Lanes", { { "track", g.id }, { "id", newId() } });
        auto* clips = child (*lane, "Clips", { { "id", newId() } });
        for (int pt : g.patterns)
        {
            auto* clip = child (*clips, "Clip", { { "time", num (offset[pt]) }, { "duration", num (s.lengthQ[pt]) }, { "playStart", "0.000000" },
                                                  { "name", juce::String ("Pattern ") + "ABCDEFGH"[pt] } });
            clipContent (*clip, g, pt);
            r.notes += (int) g.notes[pt].size();
        }
        // macro lanes: the plugin's Macro parameters, each pattern starting from the instrument's own value
        for (int m = 0; m < 8; ++m)
        {
            if (! g.hasMacro[(size_t) m]) continue;
            auto* points = child (*lane, "Points", { { "unit", "normalized" }, { "id", newId() } });
            child (*points, "Target", { { "parameter", groupMacroIds[gi][(size_t) m] } });
            for (int pt : g.patterns)
            {
                const auto& cc = g.cc[pt];
                const bool atStart = std::any_of (cc.begin(), cc.end(), [m] (const CCPoint& c) { return c.lane == 100 + m && c.tick == 0; });
                if (! atStart)
                    child (*points, "RealPoint", { { "time", num (offset[pt]) }, { "value", num (g.macros[(size_t) m]) }, { "interpolation", "hold" } });
                for (auto& c : cc)
                    if (c.lane == 100 + m)
                        child (*points, "RealPoint", { { "time", num (offset[pt] + c.tick / (double) kPPQ) }, { "value", num (c.value) }, { "interpolation", "hold" } });
            }
        }
    }
    auto* markers = child (*arrangement, "Markers", { { "id", newId() } });
    for (int pt : s.used) child (*markers, "Marker", { { "time", num (offset[pt]) }, { "name", juce::String ("Pattern ") + "ABCDEFGH"[pt] } });

    // the clip launcher: a scene per pattern
    auto* scenes = child (project, "Scenes");
    for (int pt : s.used)
    {
        auto* scene = child (*scenes, "Scene", { { "id", newId() }, { "name", juce::String ("Pattern ") + "ABCDEFGH"[pt] } });
        auto* sl = child (*scene, "Lanes", { { "timeUnit", "beats" }, { "id", newId() } });
        for (auto& g : s.groups)
        {
            if (std::find (g.patterns.begin(), g.patterns.end(), pt) == g.patterns.end()) continue;
            auto* slot = child (*sl, "ClipSlot", { { "track", g.id }, { "hasStop", "true" }, { "id", newId() } });
            auto* clip = child (*slot, "Clip", { { "time", "0.000000" }, { "duration", num (s.lengthQ[pt]) }, { "playStart", "0.000000" },
                                                 { "loopStart", "0.000000" }, { "loopEnd", num (s.lengthQ[pt]) },
                                                 { "name", juce::String ("Pattern ") + "ABCDEFGH"[pt] } });
            clipContent (*clip, g, pt);
        }
    }

    juce::XmlElement meta ("MetaData");
    meta.createNewChildElement ("Title")->addTextElement (s.name);
    meta.createNewChildElement ("Comment")->addTextElement ("Exported from Pastel Minibox: every track plays the Minibox plugin.");
    const auto fmt = juce::XmlElement::TextFormat();   // UTF-8
    auto bytes = [] (const juce::String& text) { return juce::MemoryBlock (text.toRawUTF8(), text.getNumBytesAsUTF8()); };
    addFile ("project.xml", bytes (project.toString (fmt)));
    addFile ("metadata.xml", bytes (meta.toString (fmt)));

    const auto tmp = file.getSiblingFile (file.getFileName() + ".part");
    {
        tmp.deleteFile();
        juce::FileOutputStream out (tmp);
        if (! out.openedOk() || ! zip.writeToStream (out, nullptr)) { tmp.deleteFile(); r.error = "Can't write " + file.getFullPathName(); return r; }
    }
    if (! tmp.moveFileTo (file)) { tmp.deleteFile(); r.error = "Can't write " + file.getFullPathName(); return r; }
    r.ok = true;
    r.tracks = (int) s.groups.size();
    r.patterns = (int) s.used.size();
    return r;
}
} // namespace mb::dawproject
