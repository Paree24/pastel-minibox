#include "Tests.h"
#include "model/DawProject.h"
#include "plugin/PluginProcessor.h"
#include "devices/acid/AcidDevice.h"
#include "app/presets/PresetBank.h"

using namespace mb;

namespace
{
/** Three tracks across two patterns: an acid line (accent, slide), a VA chord clip with bend and a macro lane,
    drums; pattern B swaps the acid line's preset (a second DAW track). */
struct DawRig
{
    Engine e;
    juce::File file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("minibox_test.dawproject");
    DawRig()
    {
        e.prepare (48000.0, 512);
        auto& p = e.project();
        p.name = "Export test"; p.bpm = 128.0; p.swing = 0.5f;
        for (int pt = 0; pt < 2; ++pt)
        {
            auto acid = createDevice (DeviceType::Acid);
            presets::loadDefault (*acid, true);
            if (pt == 1) acid->presetName = "Other";
            e.setDevice (pt, 0, std::move (acid));
            e.setDevice (pt, 2, createDevice (DeviceType::Drums));
        }
        auto poly = createDevice (DeviceType::Poly);
        presets::loadDefault (*poly, true);
        e.setDevice (0, 1, std::move (poly));
        auto& acidTr = p.patterns[0].slots[0].track;
        acidTr.steps[0].addNote (36, 100);
        acidTr.steps[0].flags = kAccentFlag | kSlideFlag;
        acidTr.steps[1].addNote (43, 90);
        p.patterns[1].slots[0].track.steps[4].addNote (48, 100);
        auto& roll = p.patterns[0].slots[1].track;
        roll.pianoRoll = true;
        for (int n : { 60, 64, 67 }) { ClipNote c; c.start = 0; c.length = kPPQ * 2; c.note = (uint8_t) n; c.vel = 100; roll.addClipNote (c); }
        ClipNote muted; muted.start = kPPQ; muted.note = 72; muted.muted = true; roll.addClipNote (muted);
        roll.setPoint (0, 0, 0.5f);                 // bend
        roll.setPoint (100, kPPQ * 2, 0.25f);       // macro 1
        p.patterns[0].slots[1].mixer.level = 0.6f;
        p.patterns[0].slots[1].mixer.mute = true;
        auto& drums = p.patterns[0].slots[2].track.lanes[0];
        drums[0].on = true; drums[0].velocity = 90;
        drums[2].on = true; drums[2].ratchet = 2;
    }
    ~DawRig() { file.deleteFile(); }
};

juce::String entry (juce::ZipFile& zip, const juce::String& name)
{
    auto* e = zip.getEntry (name);
    if (e == nullptr) return {};
    std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (*e));
    return in ? in->readEntireStreamAsString() : juce::String();
}

juce::MemoryBlock entryBytes (juce::ZipFile& zip, const juce::String& name)
{
    juce::MemoryBlock mb;
    if (auto* e = zip.getEntry (name))
    {
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (*e));
        if (in) in->readIntoMemoryBlock (mb);
    }
    return mb;
}

/** The component state inside a .vstpreset (its "Comp" chunk), or empty when the file is malformed. */
juce::MemoryBlock compChunk (const juce::MemoryBlock& preset)
{
    juce::MemoryInputStream in (preset, false);
    char tag[5] {};
    in.read (tag, 4);
    if (juce::String (tag) != "VST3" || in.readInt() != 1) return {};
    in.skipNextBytes (32);
    in.setPosition (in.readInt64());
    in.read (tag, 4);
    if (juce::String (tag) != "List") return {};
    for (int i = in.readInt(); --i >= 0;)
    {
        in.read (tag, 4);
        const auto off = in.readInt64(), size = in.readInt64();
        if (juce::String (tag) == "Comp") return juce::MemoryBlock (static_cast<const char*> (preset.getData()) + off, (size_t) size);
    }
    return {};
}
} // namespace

/** DAWproject export: a track per instrument holding the Minibox plugin with its state, the patterns on the
    timeline and in scenes, notes as they play. */
MB_TEST (dawproject_export)
{
    DawRig rig;
    const auto r = dawproject::exportProject (rig.e, rig.file);
    ctx.check (r.ok, "exported " + r.error);
    ctx.check (r.tracks == 4 && r.patterns == 2, "4 DAW tracks (the acid line twice), 2 patterns (" + juce::String (r.tracks) + ", " + juce::String (r.patterns) + ")");
    juce::ZipFile zip (rig.file);
    auto xml = juce::parseXML (entry (zip, "project.xml"));
    ctx.check (xml != nullptr && xml->hasTagName ("Project"), "project.xml");
    ctx.check (juce::parseXML (entry (zip, "metadata.xml")) != nullptr, "metadata.xml");
    if (xml == nullptr) return;
    ctx.check (xml->getChildByName ("Transport")->getChildByName ("Tempo")->getDoubleAttribute ("value") == 128.0, "tempo");

    std::map<juce::String, juce::XmlElement*> tracks;
    for (auto* t : xml->getChildByName ("Structure")->getChildIterator()) tracks[t->getStringAttribute ("name")] = t;
    ctx.check (tracks.count ("Master") == 1, "a master track");
    int plugins = 0;
    for (auto& [name, t] : tracks)
    {
        if (name == "Master") continue;
        auto* plug = t->getChildByName ("Channel")->getChildByName ("Devices")->getChildByName ("Vst3Plugin");
        if (plug == nullptr) continue;
        ++plugins;
        ctx.check (plug->getStringAttribute ("deviceID") == "ABCDEF019182FAEB5073746C4D6E6278", name + ": the Minibox VST3 class");
        const auto preset = entryBytes (zip, plug->getChildByName ("State")->getStringAttribute ("path"));
        const auto comp = compChunk (preset);
        ctx.check (comp.getSize() > 0, name + ": its .vstpreset");
        // the plugin opens it as a DAW would hand it over
        MiniboxProcessor proc;
        proc.setStateInformation (comp.getData(), (int) comp.getSize());
        auto* d = proc.device();
        const bool acid = name.contains ("Acid") || name.contains (deviceTypeName (DeviceType::Acid));
        if (name.startsWith ("1 "))
            ctx.check (d != nullptr && d->type() == DeviceType::Acid && (name.contains ("(A)") ? d->presetName != "Other" : d->presetName == "Other"),
                       name + ": the acid line, its own preset in each pattern");
        if (name.startsWith ("2 "))
        {
            ctx.check (d != nullptr && d->type() == DeviceType::Poly, name + ": the VA");
            ctx.check (std::abs (proc.levelParam->get() - 0.6f) < 1e-3f, name + ": its level");
            ctx.check (t->getChildByName ("Channel")->getChildByName ("Mute")->getBoolAttribute ("value"), name + ": muted on the DAW channel");
            ctx.check (! proc.muteParam->get(), name + ": not muted inside the plugin");
        }
        if (name.startsWith ("3 ")) ctx.check (d != nullptr && d->type() == DeviceType::Drums, name + ": the drums");
        juce::ignoreUnused (acid);
    }
    ctx.check (plugins == 4, "a Minibox plugin on every track (" + juce::String (plugins) + ")");

    // the timeline: A then B, markers; notes as they play
    auto* lanes = xml->getChildByName ("Arrangement")->getChildByName ("Lanes");
    std::map<juce::String, juce::String> nameOf;
    for (auto& [name, t] : tracks) nameOf[t->getStringAttribute ("id")] = name;
    int markers = 0;
    for (auto* m : xml->getChildByName ("Arrangement")->getChildByName ("Markers")->getChildIterator()) markers += m->getStringAttribute ("name").startsWith ("Pattern ");
    ctx.check (markers == 2, "a marker per pattern");
    for (auto* lane : lanes->getChildIterator())
    {
        const auto name = nameOf[lane->getStringAttribute ("track")];
        auto* clips = lane->getChildByName ("Clips");
        if (clips == nullptr) continue;
        for (auto* clip : clips->getChildIterator())
        {
            auto* notes = clip->getChildByName ("Notes");
            auto* inner = clip->getChildByName ("Lanes");
            if (notes == nullptr && inner != nullptr) notes = inner->getChildByName ("Notes");
            if (notes == nullptr) continue;
            std::vector<juce::XmlElement*> ns;
            for (auto* n : notes->getChildIterator()) ns.push_back (n);
            if (name.startsWith ("1 ") && name.contains ("(A)"))
            {
                bool accent = false, keyswitch = false;
                for (auto* n : ns)
                {
                    accent |= n->getIntAttribute ("key") == 36 && n->getDoubleAttribute ("vel") > 0.99;
                    keyswitch |= n->getIntAttribute ("key") == AcidDevice::kKeyswitchLow + 1 && n->getDoubleAttribute ("time") > 0.0
                              && n->getDoubleAttribute ("time") < 0.25 + 1e-6;
                }
                ctx.check (accent, "the accented step plays at full velocity");
                ctx.check (keyswitch, "the slide holds the Slide keyswitch over the next note's start");
            }
            if (name.startsWith ("1 ") && name.contains ("(B)"))
                ctx.check (std::abs (clip->getDoubleAttribute ("time") - 4.0) < 1e-6 && ns.size() == 1 && std::abs (ns[0]->getDoubleAttribute ("time") - 1.0) < 1e-6,
                           "pattern B's clip starts after pattern A, its note on beat 2");
            if (name.startsWith ("2 "))
            {
                ctx.check (ns.size() == 3, "the chord, not the muted note (" + juce::String ((int) ns.size()) + ")");
                ctx.check (inner != nullptr && inner->getChildByName ("Points") != nullptr
                               && inner->getChildByName ("Points")->getChildByName ("Target")->getStringAttribute ("expression") == "pitchBend",
                           "the bend lane rides in the clip");
            }
            if (name.startsWith ("3 ") && clip->getDoubleAttribute ("time") == 0.0)
                ctx.check (ns.size() == 3 && std::abs (ns[2]->getDoubleAttribute ("time") - 0.625) < 1e-6, "drums: a hit and a ratchet pair");
        }
        if (name.startsWith ("2 "))
        {
            auto* pts = lane->getChildByName ("Points");
            ctx.check (pts != nullptr && pts->getChildByName ("Target")->getStringAttribute ("parameter").isNotEmpty(), "the macro lane automates the plugin's Macro 1");
        }
    }
    int scenes = 0;
    for (auto* s : xml->getChildByName ("Scenes")->getChildIterator()) scenes += s->getChildByName ("Lanes")->getNumChildElements() > 0;
    ctx.check (scenes == 2, "a scene per pattern");

    // the parameter IDs the export names are the ones the plugin reports
    MiniboxProcessor proc;
    for (auto* p : proc.getParameters())
        if (auto* pw = dynamic_cast<juce::AudioProcessorParameterWithID*> (p); pw != nullptr && pw->paramID == "macro1")
        {
            auto* va = tracks.begin()->second;
            for (auto& [name, t] : tracks) if (name.startsWith ("2 ")) va = t;
            auto* param = va->getChildByName ("Channel")->getChildByName ("Devices")->getChildByName ("Vst3Plugin")->getChildByName ("Parameters")->getChildElement (0);
            ctx.check (param->getIntAttribute ("parameterID") == (int) ((uint32_t) juce::String ("macro1").hashCode() & 0x7fffffffu), "Macro 1's VST3 ID");
        }
}

/** The exported presets in the built VST3, through the host's preset loader (class ID checked by it). Tool:
    needs build/MiniboxPlugin_artefacts. */
MB_TEST (tool_dawproject_host)
{
    DawRig rig;
    ctx.check (dawproject::exportProject (rig.e, rig.file).ok, "exported");
    // a copy to check against the format's schema (xmllint --schema Project.xsd)
    if (auto keep = juce::SystemStats::getEnvironmentVariable ("MINIBOX_KEEP_DAWPROJECT", {}); keep.isNotEmpty()) rig.file.copyFileTo (keep);
    auto bundle = juce::File (MINIBOX_SOURCE_DIR).getChildFile ("build/MiniboxPlugin_artefacts/Release/VST3/Minibox.vst3");
    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> descs;
    fm.getFormat (0)->findAllTypesForFile (descs, bundle.getFullPathName());
    if (descs.isEmpty()) { ctx.check (false, "the VST3 is built"); return; }
    juce::ZipFile zip (rig.file);
    auto xml = juce::parseXML (entry (zip, "project.xml"));
    int loaded = 0;
    for (auto* t : xml->getChildByName ("Structure")->getChildIterator())
    {
        auto* devs = t->getChildByName ("Channel")->getChildByName ("Devices");
        if (devs == nullptr) continue;
        auto* plug = devs->getChildByName ("Vst3Plugin");
        juce::String err;
        auto inst = fm.createPluginInstance (*descs[0], 48000.0, 512, err);
        const auto preset = entryBytes (zip, plug->getChildByName ("State")->getStringAttribute ("path"));
        struct Visitor : juce::ExtensionsVisitor
        {
            const juce::MemoryBlock& data; bool ok = false;
            explicit Visitor (const juce::MemoryBlock& d) : data (d) {}
            void visitVST3Client (const VST3Client& c) override { ok = c.setPreset (data); }
        } visitor (preset);
        inst->getExtensions (visitor);
        const bool ok = visitor.ok;
        // the host wraps the plugin's own state: <VST3PluginState><IComponent>base64</IComponent>…
        juce::MemoryBlock state, comp;
        inst->getStateInformation (state);
        if (auto host = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize()))
            if (auto* c = host->getChildByName ("IComponent")) comp.fromBase64Encoding (c->getAllSubText());
        const auto tree = juce::ValueTree::readFromData (comp.getData(), comp.getSize());
        ctx.check (ok, t->getStringAttribute ("name") + ": the host accepts the preset (its class ID)");
        const auto name = t->getStringAttribute ("name");
        const auto want = name.startsWith ("1 ") ? "acid" : name.startsWith ("2 ") ? "poly" : "drums";
        ctx.check (ok && tree.getProperty ("instrument").toString() == want, name + ": the plugin loads it as " + want + " (" + tree.getProperty ("instrument").toString() + ")");
        // the host's parameter IDs match the ones the automation targets
        auto* macro1 = plug->getChildByName ("Parameters")->getChildElement (0);
        bool idMatches = false;
        for (auto* p : inst->getParameters())
            if (auto* hp = dynamic_cast<juce::HostedAudioProcessorParameter*> (p))
                idMatches |= p->getName (64) == "Macro 1" && hp->getParameterID() == macro1->getStringAttribute ("parameterID");
        ctx.check (idMatches, name + ": Macro 1's ID is the plugin's");
        loaded += ok;
    }
    ctx.check (loaded == 4, "every track's preset loads");
}
