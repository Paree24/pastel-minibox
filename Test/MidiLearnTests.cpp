#include "Tests.h"
#include "app/MainComponent.h"

using namespace mb;
using namespace mb::ui;

namespace
{
const juce::String kMid = "Arturia KeyLab Essential 49 MID", kDaw = "Arturia KeyLab Essential 49 DAW";
juce::MidiMessage cc (int n, int v, int ch = 1) { return juce::MidiMessage::controllerEvent (ch, n, v); }
} // namespace

/** MIDI learn (docs/midi-learn.md): arming, learning, modes, routing, ports, files, the factory map. */
MB_TEST (midi_learn)
{
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("minibox-learn-test");
    tmp.deleteRecursively();
    tmp.createDirectory();
    MidiMap::currentFileOverride() = tmp.getChildFile ("current.mbmidi");
    Engine e;
    e.prepare (48000.0, 512);
    e.setDevice (0, 0, createDevice (DeviceType::Acid));
    e.setDevice (0, 1, createDevice (DeviceType::Poly));
    e.focusTrack = 1;
    {
        MainComponent mc (e);
        mc.setSize (1600, 1000);
        auto& map = mc.midiMap();
        map.clear();
        auto& ac = mc.context();

        // learn mode: a click arms (selected-instrument form), a moved control maps
        mc.keyPressed (juce::KeyPress ('m', juce::ModifierKeys::commandModifier, 0));
        ctx.check (mc.learnOpen() && learnHooks().active && map.learning(), "Ctrl+M turns learn mode on");
        learnHooks().arm ("macro:1:3");
        ctx.check (map.armedTarget() == "macro:sel:3", "a macro arms for the selected instrument");
        ctx.check (map.filter (kMid, cc (77, 40)), "an armed control takes the message");
        map.dispatch();
        ctx.check (map.mappings.size() == 1 && map.mappings[0].number == 77 && map.mappings[0].mode == MidiMapping::Mode::Absolute,
                   "the moved CC is mapped as absolute");
        ctx.check (learnHooks().label ("macro:1:3") == "CC 77" && learnHooks().label ("macro:0:3").isEmpty(), "the selected strip shows the message");
        map.arm ({});
        ctx.check (! map.filter (kMid, juce::MidiMessage::noteOn (1, 60, 0.8f)), "unmapped notes still play");

        // the mapping follows the selected instrument
        const bool used = map.filter (kMid, cc (77, 127));
        map.dispatch();
        ctx.check (used && std::abs (e.device (0, 1)->getMacro (3) - 1.0f) < 1.0e-3f, "the CC sets the selected instrument's macro");
        e.focusTrack = 0;
        map.filter (kMid, cc (77, 0)); map.dispatch();
        ctx.check (e.device (0, 0)->getMacro (3) < 1.0e-3f && e.device (0, 1)->getMacro (3) > 0.99f, "selecting another instrument moves the knobs to it");

        // relative encoder detection and deltas
        learnHooks().arm ("level:0");
        for (int i = 0; i < 4; ++i) map.filter (kDaw, cc (16, 1)), map.dispatch();
        auto it = std::find_if (map.mappings.begin(), map.mappings.end(), [] (const MidiMapping& m) { return m.number == 16; });
        ctx.check (it != map.mappings.end() && it->mode == MidiMapping::Mode::RelativeTwos && it->port == kDaw, "a relative encoder is detected, with its port");
        map.arm ({});
        auto& level = e.project().patterns[0].slots[(size_t) 0].mixer.level;
        level = 0.5f;
        map.filter (kDaw, cc (16, 127)); map.dispatch();
        ctx.check (level < 0.5f && level > 0.48f, "relative: 127 steps down");
        ctx.check (! map.filter (kMid, cc (16, 1)), "the same CC on another port is not mapped");

        // a note button mapped to an action (toggle play), moved from one action to another
        mc.runAction ("stop");
        learnHooks().arm (TopBar::learnTarget ("play"));
        map.filter (kDaw, juce::MidiMessage::noteOn (1, 94, 1.0f)); map.filter (kDaw, juce::MidiMessage::noteOff (1, 94)); map.dispatch();
        map.arm ({});
        ctx.check (map.sourceFor ("action:play") == "Note 94", "a note learns an action");
        map.filter (kDaw, juce::MidiMessage::noteOn (1, 94, 1.0f)); map.filter (kDaw, juce::MidiMessage::noteOff (1, 94)); map.dispatch();
        std::vector<float> l (512), r (512);
        juce::MidiBuffer none;
        e.process (l.data(), r.data(), 512, &none);
        ctx.check (e.isPlaying(), "the button plays");
        e.stop();
        map.arm ("action:track-next");
        map.filter (kDaw, juce::MidiMessage::noteOn (1, 94, 1.0f)); map.dispatch();
        map.arm ({});
        ctx.check (map.sourceFor ("action:play").isEmpty() && map.sourceFor ("action:track-next") == "Note 94", "learning a mapped message moves it");
        e.focusTrack = 0;
        map.filter (kDaw, juce::MidiMessage::noteOff (1, 94)); map.filter (kDaw, juce::MidiMessage::noteOn (1, 94, 1.0f)); map.dispatch();
        ctx.check (e.focusTrack.load() == 1, "an action selects the next instrument");

        // toggle / momentary / pickup on continuous targets
        MidiMapping mp; mp.target = "x"; mp.mode = MidiMapping::Mode::Absolute; mp.pickup = true;
        FloatBinding b (&level, 0, 1, 0.8f, [] (float) { return juce::String(); });
        level = 0.5f;
        applyLearnControl (b, { MidiControl::Type::Set, 0.1f }, mp);
        ctx.check (std::abs (level - 0.5f) < 1.0e-6f, "pickup: far below waits");
        applyLearnControl (b, { MidiControl::Type::Set, 0.7f }, mp);
        ctx.check (std::abs (level - 0.7f) < 1.0e-6f, "pickup: passing the value takes over");
        mp.mode = MidiMapping::Mode::Toggle; mp.pickup = false;
        applyLearnControl (b, { MidiControl::Type::Press, 0 }, mp);
        ctx.check (level < 1.0e-6f, "toggle flips to the other end");
        mp.mode = MidiMapping::Mode::Momentary;
        applyLearnControl (b, { MidiControl::Type::Press, 0 }, mp);
        const float held = level;
        applyLearnControl (b, { MidiControl::Type::Release, 0 }, mp);
        ctx.check (held > 0.99f && level < 1.0e-6f, "momentary holds while pressed");

        // remove and the drawer paints the list
        mc.createComponentSnapshot (mc.getLocalBounds());
        learnHooks().remove ("level:0");
        ctx.check (map.sourceFor ("level:0").isEmpty(), "right-click removes a mapping");

        // files: the current map persists; save / load
        ctx.check (MidiMap::currentFile().existsAsFile(), "the current map is saved automatically");
        const auto f = tmp.getChildFile ("mine.mbmidi");
        const int n = (int) map.mappings.size();
        ctx.check (map.save (f), "save a map file");
        map.clear();
        ctx.check (map.load (f) && (int) map.mappings.size() == n, "load it back");
        mc.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
        ctx.check (! mc.learnOpen() && ! learnHooks().active, "Esc leaves learn mode");
        juce::ignoreUnused (ac);
    }
    // the factory map
    {
        MidiMap map;
        const auto files = MidiMap::factoryMaps();
        ctx.check (files.size() >= 1 && map.load (files[0]) && map.mappings.size() > 30, "the factory controller map loads");
        ctx.check (map.filter (kMid, cc (74, 10)) && ! map.filter (kMid, juce::MidiMessage::noteOn (10, 36, 1.0f)), "its knobs are mapped, the pads play");
        ctx.check (map.filter (kDaw, juce::MidiMessage::pitchWheel (1, 8192)), "its DAW faders are mapped");
    }
    MidiMap::currentFileOverride() = juce::File();
    tmp.deleteRecursively();
}
