#include "MidiMap.h"
#include "devices/SampleSlot.h"
#include <set>

namespace mb
{
namespace
{
struct Src { MidiMapping::Kind kind; int channel, number; };

bool sourceOf (const juce::MidiMessage& m, Src& s)
{
    if (m.isController()) { s = { MidiMapping::Kind::CC, m.getChannel(), m.getControllerNumber() }; return true; }
    if (m.isNoteOnOrOff()) { s = { MidiMapping::Kind::Note, m.getChannel(), m.getNoteNumber() }; return true; }
    if (m.isPitchWheel()) { s = { MidiMapping::Kind::PitchBend, m.getChannel(), 0 }; return true; }
    return false;
}

bool portMatches (const juce::String& pattern, const juce::String& port)
{
    return pattern.isEmpty() || port.containsIgnoreCase (pattern);
}

const char* kKinds[] = { "cc", "note", "pitchbend" };
const char* kModes[] = { "absolute", "relative", "relative-offset", "relative-sign", "trigger", "toggle", "momentary" };
} // namespace

// ------------------------------------------------------------------------------------------- mapping
bool MidiMapping::matches (const juce::String& portName, const juce::MidiMessage& m) const
{
    Src s;
    if (! sourceOf (m, s) || s.kind != kind) return false;
    if (channel != 0 && s.channel != channel) return false;
    if (kind != Kind::PitchBend && s.number != number) return false;
    return portMatches (port, portName);
}

juce::String MidiMapping::messageName() const
{
    juce::String s = kind == Kind::CC ? "CC " + juce::String (number)
                   : kind == Kind::Note ? "Note " + juce::String (number) : juce::String ("Pitch bend");
    if (channel != 0 && (kind == Kind::PitchBend || channel != 1)) s << " ch " << channel;
    return s;
}

juce::String MidiMapping::modeName (Mode m)
{
    static const char* names[] = { "Absolute", "Relative", "Relative (64)", "Relative (sign)", "Trigger", "Toggle", "Momentary" };
    return names[(int) m];
}

// ------------------------------------------------------------------------------------------- map
MidiMap::MidiMap() : queue ((size_t) kQueue) {}
MidiMap::~MidiMap() { cancelPendingUpdate(); }

bool MidiMap::filter (const juce::String& portName, const juce::MidiMessage& m)
{
    Src s;
    if (! sourceOf (m, s)) return false;
    bool mapped = false;
    {
        const juce::SpinLock::ScopedLockType lock (keyLock);
        for (auto& k : keys)
            if (k.kind == s.kind && (k.channel == 0 || k.channel == s.channel) && (k.kind == MidiMapping::Kind::PitchBend || k.number == s.number)
                && portMatches (k.port, portName)) { mapped = true; break; }
    }
    const bool armedNow = armedFlag.load();
    if (mapped || armedNow || learnOn.load())
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)
        {
            auto& q = queue[(size_t) scope.startIndex1];
            q.msg = m;
            portName.copyToUTF8 (q.port, sizeof (q.port));
        }
        triggerAsyncUpdate();
    }
    return mapped || armedNow;
}

void MidiMap::dispatch()
{
    const auto scope = fifo.read (fifo.getNumReady());
    auto run = [this] (int start, int n) { for (int i = 0; i < n; ++i) { auto& q = queue[(size_t) (start + i)]; handle (juce::String::fromUTF8 (q.port), q.msg); } };
    run (scope.startIndex1, scope.blockSize1);
    run (scope.startIndex2, scope.blockSize2);
}

void MidiMap::handle (const juce::String& port, const juce::MidiMessage& m)
{
    Src s;
    if (! sourceOf (m, s)) return;
    if (learning())
    {
        juce::String txt = s.kind == MidiMapping::Kind::CC ? "CC " + juce::String (s.number) + " = " + juce::String (m.getControllerValue())
                         : s.kind == MidiMapping::Kind::Note ? "Note " + juce::String (s.number) + (m.isNoteOn() ? " on" : " off")
                                                             : "Pitch bend " + juce::String (m.getPitchWheelValue());
        lastReceived = txt + juce::String::fromUTF8 (" \xc2\xb7 ch ") + juce::String (s.channel)
                     + (port.isNotEmpty() ? juce::String::fromUTF8 (" \xc2\xb7 ") + port : juce::String());
        if (armed.isNotEmpty()) { learn (port, m); return; }
        if (onChanged) onChanged();
    }
    for (auto& mp : mappings)
    {
        if (! mp.matches (port, m)) continue;
        MidiControl c;
        if (control (mp, m, c) && apply) apply (mp.target, c, mp);
    }
}

bool MidiMap::control (MidiMapping& mp, const juce::MidiMessage& m, MidiControl& c)
{
    using T = MidiControl::Type;
    if (mp.kind == MidiMapping::Kind::PitchBend)
    {
        const float v = m.getPitchWheelValue() / 16383.0f;
        if (mp.isButtonMode()) { const bool on = v > 0.5f; if (on == mp.held) return false; mp.held = on; c.type = on ? T::Press : T::Release; return true; }
        c = { T::Set, mp.lo + (mp.hi - mp.lo) * v };
        return true;
    }
    if (mp.kind == MidiMapping::Kind::Note)
    {
        const bool on = m.isNoteOn();
        if (mp.mode == MidiMapping::Mode::Absolute)
        {
            if (! on) return false;
            c = { T::Set, mp.lo + (mp.hi - mp.lo) * m.getVelocity() / 127.0f };
            return true;
        }
        if (on == mp.held) return false;
        mp.held = on;
        c.type = on ? T::Press : T::Release;
        return true;
    }
    const int v = m.getControllerValue();
    switch (mp.mode)
    {
        case MidiMapping::Mode::Absolute: c = { T::Set, mp.lo + (mp.hi - mp.lo) * v / 127.0f }; return true;
        case MidiMapping::Mode::RelativeTwos: c = { T::Delta, (v < 64 ? v : v - 128) * (mp.hi - mp.lo) / 100.0f }; return true;
        case MidiMapping::Mode::RelativeOffset: c = { T::Delta, (v - 64) * (mp.hi - mp.lo) / 100.0f }; return true;
        case MidiMapping::Mode::RelativeSign: c = { T::Delta, ((v & 64) ? -(v & 63) : (v & 63)) * (mp.hi - mp.lo) / 100.0f }; return true;
        default:
        {
            const bool on = v >= 64;
            if (on == mp.held) return false;
            mp.held = on;
            c.type = on ? T::Press : T::Release;
            return true;
        }
    }
}

void MidiMap::learn (const juce::String& port, const juce::MidiMessage& m)
{
    Src s;
    if (! sourceOf (m, s)) return;
    if (s.kind == MidiMapping::Kind::Note && ! m.isNoteOn()) return;   // the press maps a button; its release is used up
    const int key = (int) s.kind * 100000 + s.channel * 1000 + s.number;
    if (key != learnKey || port != learnPort) { learnValues.clear(); learnKey = key; learnPort = port; }
    MidiMapping mp;
    mp.kind = s.kind; mp.channel = s.channel; mp.number = s.number; mp.port = port; mp.target = armed;
    const bool action = armed.startsWith ("action:");
    if (s.kind == MidiMapping::Kind::CC)
    {
        learnValues.push_back (m.getControllerValue());
        // relative encoders send small steps around 0 / 64 instead of positions: tell them apart while the
        // control turns (an absolute control would pass through the values in between)
        auto all = [this] (auto pred) { for (int v : learnValues) if (! pred (v)) return false; return true; };
        std::set<int> distinct (learnValues.begin(), learnValues.end());
        const bool many = learnValues.size() >= 3 && distinct.size() <= 3;
        if (action) mp.mode = MidiMapping::Mode::Trigger;
        else if (many && all ([] (int v) { return v >= 57 && v <= 71 && v != 64; })) mp.mode = MidiMapping::Mode::RelativeOffset;
        else if (many && all ([] (int v) { return (v >= 1 && v <= 15) || (v >= 113 && v <= 127); })) mp.mode = MidiMapping::Mode::RelativeTwos;
        else if (many && all ([] (int v) { return (v >= 1 && v <= 15) || (v >= 65 && v <= 79); })) mp.mode = MidiMapping::Mode::RelativeSign;
        else mp.mode = MidiMapping::Mode::Absolute;
    }
    else if (s.kind == MidiMapping::Kind::Note) mp.mode = action ? MidiMapping::Mode::Trigger : MidiMapping::Mode::Toggle;
    else mp.mode = action ? MidiMapping::Mode::Trigger : MidiMapping::Mode::Absolute;
    add (mp);
}

void MidiMap::setLearning (bool on)
{
    learnOn = on;
    ++revision;
    if (! on) arm ({});
    if (onChanged) onChanged();
}

void MidiMap::arm (const juce::String& target)
{
    armed = target;
    ++revision;
    armedFlag = target.isNotEmpty();
    learnValues.clear(); learnKey = -1;
    if (onChanged) onChanged();
}

void MidiMap::add (MidiMapping mp)
{
    // a message moves to its new target; a control learns one message (the last one moved)
    // a re-learnt control keeps its name
    for (auto& o : mappings) if (mp.label.isEmpty() && (o.target == mp.target || o.sameSource (mp))) mp.label = o.label;
    mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [&] (const MidiMapping& o) { return o.sameSource (mp) || o.target == mp.target; }),
                    mappings.end());
    mappings.push_back (std::move (mp));
    changed();
}

void MidiMap::remove (int i)
{
    if (i < 0 || i >= (int) mappings.size()) return;
    mappings.erase (mappings.begin() + i);
    changed();
}

void MidiMap::removeTarget (const juce::String& target)
{
    mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [&] (const MidiMapping& o) { return o.target == target; }), mappings.end());
    changed();
}

void MidiMap::clear()
{
    mappings.clear();
    name = "Custom";
    changed();
}

juce::String MidiMap::sourceFor (const juce::String& target) const
{
    for (auto& m : mappings) if (m.target == target) return m.messageName();
    return {};
}

void MidiMap::publish()
{
    std::vector<Key> k;
    for (auto& m : mappings) k.push_back ({ m.kind, m.channel, m.number, m.port });
    const juce::SpinLock::ScopedLockType lock (keyLock);
    keys.swap (k);
}

void MidiMap::changed (bool persist)
{
    ++revision;
    publish();
    if (persist) save (currentFile());
    if (onChanged) onChanged();
}

// ------------------------------------------------------------------------------------------- files
juce::var MidiMap::toVar() const
{
    juce::Array<juce::var> list;
    for (auto& m : mappings)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("type", kKinds[(int) m.kind]);
        o->setProperty ("channel", m.channel);
        if (m.kind != MidiMapping::Kind::PitchBend) o->setProperty ("number", m.number);
        if (m.port.isNotEmpty()) o->setProperty ("port", m.port);
        o->setProperty ("target", m.target);
        if (m.label.isNotEmpty()) o->setProperty ("label", m.label);
        o->setProperty ("mode", kModes[(int) m.mode]);
        if (m.lo != 0.0f) o->setProperty ("min", m.lo);
        if (m.hi != 1.0f) o->setProperty ("max", m.hi);
        if (m.pickup) o->setProperty ("pickup", true);
        list.add (juce::var (o));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "minibox-midimap");
    root->setProperty ("version", 1);
    root->setProperty ("name", name);
    root->setProperty ("mappings", list);
    return juce::var (root);
}

bool MidiMap::fromVar (const juce::var& v)
{
    auto* list = v["mappings"].getArray();
    if (list == nullptr) return false;
    std::vector<MidiMapping> out;
    auto index = [] (const juce::String& s, const char* const* names, int n) { for (int i = 0; i < n; ++i) if (s == names[i]) return i; return -1; };
    for (auto& e : *list)
    {
        MidiMapping m;
        const int k = index (e["type"].toString(), kKinds, 3);
        const int md = index (e["mode"].toString(), kModes, 7);
        if (k < 0 || e["target"].toString().isEmpty()) continue;
        m.kind = (MidiMapping::Kind) k;
        m.mode = md < 0 ? MidiMapping::Mode::Absolute : (MidiMapping::Mode) md;
        m.channel = juce::jlimit (0, 16, (int) e.getProperty ("channel", 0));
        m.number = juce::jlimit (0, 127, (int) e.getProperty ("number", 0));
        m.port = e["port"].toString();
        m.target = e["target"].toString();
        m.label = e["label"].toString();
        m.lo = (float) e.getProperty ("min", 0.0);
        m.hi = (float) e.getProperty ("max", 1.0);
        m.pickup = (bool) e.getProperty ("pickup", false);
        out.push_back (std::move (m));
    }
    mappings = std::move (out);
    name = v.getProperty ("name", "Custom").toString();
    return true;
}

bool MidiMap::save (const juce::File& f) const
{
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (juce::JSON::toString (toVar()));
}

bool MidiMap::load (const juce::File& f)
{
    if (! f.existsAsFile()) return false;
    if (! fromVar (juce::JSON::parse (f))) return false;
    changed (f != currentFile());
    return true;
}

juce::File& MidiMap::currentFileOverride() { static juce::File f; return f; }

juce::File MidiMap::currentFile()
{
    if (currentFileOverride() != juce::File()) return currentFileOverride();
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Pastel Minibox").getChildFile ("current.mbmidi");
}

juce::Array<juce::File> MidiMap::factoryMaps()
{
    auto files = factoryDir().getChildFile ("midimaps").findChildFiles (juce::File::findFiles, false, "*.mbmidi");
    files.sort();
    return files;
}
} // namespace mb
