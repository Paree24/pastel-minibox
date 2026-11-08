#include "Tests.h"
#include "app/MainComponent.h"
#include "app/presets/PresetBank.h"
#include "ui/PresetBrowser.h"
#include "devices/SampleSlot.h"
#include "plugin/PluginProcessor.h"
#include "model/Serializer.h"

using namespace mb;

namespace
{
juce::File shotDir() { auto d = juce::File (MINIBOX_SOURCE_DIR).getChildFile ("build/shots"); d.createDirectory(); return d; }

void snap (juce::Component& c, const juce::String& name)
{
    auto f = shotDir().getChildFile (name + ".png");
    f.deleteFile();   // never leave a stale screenshot behind
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
}

void loadPreset (DeviceProcessor& d, const juce::String& name)
{
    for (auto& p : presets::list (d.type())) if (p.name == name) { presets::apply (d, p, true); return; }
    presets::loadDefault (d, true);
}

void demo (Engine& e)
{
    struct S { int p, t; DeviceType type; const char* preset; };
    const S slots[] = { { 0, 0, DeviceType::Drums, "House" }, { 0, 1, DeviceType::Acid, "Resonant Line" }, { 0, 2, DeviceType::Poly, "Breathing Pad" },
                        { 0, 3, DeviceType::FM, "Mellow Bell" }, { 1, 0, DeviceType::DrumSampler, "Boom Bap" }, { 1, 1, DeviceType::Mono, "Warm Bass" },
                        { 1, 2, DeviceType::Poly, "Ring Keys" }, { 1, 3, DeviceType::Sampler, "Muted Aah Choir" } };
    forceSyncSampleLoads() = true;
    for (auto& s : slots)
    {
        auto d = createDevice (s.type);
        loadPreset (*d, s.preset);
        e.setDevice (s.p, s.t, std::move (d));
    }
    forceSyncSampleLoads() = false;
    auto& p = e.project();
    p.name = "Night Market";
    p.bpm = 124.0;
    for (int pat = 0; pat < 2; ++pat)
    {
        p.patterns[(size_t) pat].lengthBars = pat == 0 ? 4 : 8;
        auto& dr = p.patterns[(size_t) pat].slots[0].track;
        const int kick[] = { 0, 4, 7, 8, 10, 12, 15 }, snare[] = { 4, 12 }, hat[] = { 0, 2, 4, 6, 8, 10, 12, 14 };
        for (int s : kick) dr.lanes[0][(size_t) s] = { true, 110, 100, 1, 0, 0 };
        for (int s : snare) dr.lanes[1][(size_t) s] = { true, 100, 100, 1, 0, 0 };
        for (int s : hat) dr.lanes[4][(size_t) s] = { true, 80, 100, 1, 0, 0 };
        dr.lanes[3][14] = { true, 90, 100, 1, 0, 0 };
        dr.lanes[7][2] = { true, 70, 100, 1, 0, 0 };
    }
    auto& acid = p.patterns[0].slots[1].track;
    const int notes[16] = { 36, 48, 46, 0, 36, 39, 41, 0, 36, 36, 43, 0, 34, 0, 36, 39 };
    const int flags[16] = { 1, 2, 0, 0, 1, 2, 1, 0, 0, 2, 1, 0, 0, 0, 1, 0 };
    for (int i = 0; i < 16; ++i) if (notes[i]) { auto& s = acid.steps[(size_t) i]; s.addNote (notes[i]); s.flags = (uint8_t) flags[i]; s.setVelocity (90 + (i % 3) * 12); }
    auto& poly = p.patterns[0].slots[2].track;
    for (int i : { 0, 6, 8, 14 }) { auto& s = poly.steps[(size_t) i]; for (int n : { 48, 51, 55, 58 }) s.addNote (n); s.setLength (300); }
    auto& fm = p.patterns[0].slots[3].track;
    for (int i : { 2, 9, 12 }) fm.steps[(size_t) i].addNote (72 + i % 5);
    p.patterns[0].slots[3].mixer.mute = true;
}
} // namespace

MB_TEST (tool_ui_shots)
{
    {
        Engine e;
        e.prepare (48000.0, 512);
        MainComponent mc (e);
        mc.setSize (1778, 1000);   // 16:9, as the mockups
        mc.refreshAll();
        snap (mc, "00_new_project");
    }
    Engine e;
    e.prepare (48000.0, 512);
    demo (e);
    serializer::save (e, shotDir().getChildFile ("demo.minibox"));   // the same project for the phone's screenshots
    MainComponent mc (e);
    mc.setSize (1600, 1000);   // 16:10: the narrow layout (icon file buttons)
    mc.refreshAll();
    snap (mc, "01_rack_16x10");
    mc.setSize (1778, 1000);   // 16:9, as the mockups
    auto shot = [&] (int page, int track, const juce::String& name)
    {
        mc.showPage (page, track);
        mc.refreshAll();
        mc.refreshAll();
        snap (mc, name);
    };
    e.focusPattern = 0;
    shot (0, 0, "01_rack");
    {
        // the master effects drawer with a mastering chain, Stutter held (mockup 19_master_fx)
        auto& fx = mc.masterEffects();
        const char* chain[] = { "3-Band EQ", "Compressor", "Saturation", "Stereo Width", "Limiter" };
        for (int i = 0; i < 5; ++i) fx.setSlotEffect (false, i, chain[i]);
        e.project().masterFx[2].bypass = true;
        fx.setHeld (0, true);
        mc.showMasterFx (true, false, 1);
        shot (0, 0, "19_master_fx");
        mc.showMasterFx (true, true, 3);
        shot (0, 0, "19b_master_fx_pads");
        mc.showMasterFx (false);
        fx.setHeld (0, false);
        for (int i = 4; i >= 0; --i) fx.removeSlot (i);
    }
    shot (1, 1, "02_seq");
    shot (1, 0, "02h_seq_drums");    // the drum track's steps
    shot (1, 2, "02i_seq_chords");   // a step track of chords and long notes
    mc.seqView().setGridMode (2);
    shot (1, 1, "02j_seq_octave");   // the step grid's octave sequencer
    mc.seqView().setGridMode (0);
    {
        // a long note in the Steps view: its gate bar runs over the 4 steps it rings (as the piano roll shows it)
        auto& st = e.project().patterns[0].slots[1].track.steps[0];
        const auto saved = st;
        if (st.noteCount == 0) st.addNote (48, 100);
        st.setLength (4 * 128);
        shot (1, 1, "02g_seq_long_note");
        st = saved;
    }
    for (int mode = 0; mode < 2; ++mode)
    {
        // the recording popover's content (mockup 18_recording / 18b_recording_step)
        e.project().recordMode = mode;
        auto panel = mc.topBarView().createRecordingPanel();
        snap (*panel, mode == 0 ? "18_recording" : "18b_recording_step");
        e.project().recordMode = 0;
    }
    {
        // playing: bar 2 outlined and filling, the top bar's position, some swing
        e.project().swing = 0.58f;
        e.project().countIn = false;
        e.play();
        std::vector<float> l (512), r (512);
        while (e.currentStep() < 22) e.process (l.data(), r.data(), 512, nullptr);
        shot (1, 1, "02f_seq_playing");
        e.stop();
        e.process (l.data(), r.data(), 512, nullptr);
        e.project().swing = 0.5f;
        mc.context().viewBar = 0;   // the view followed the playhead: back to bar 1 for the shots below
    }
    {
        // the step sequence viewed as a piano roll; zoomed out to the whole pattern on a 1/8 triplet grid
        mc.seqView().setStepsView (false);
        shot (1, 1, "02b_seq_steps_as_roll");
        auto& rs = mc.context().rollSettings;
        rs.viewSteps = 64; rs.start = 0; rs.grid = 4; rs.triplet = true;
        shot (1, 1, "02d_seq_zoomed");
        rs = {};
        mc.seqView().setStepsView (true);
        // a piano-roll clip (the VA track converted, plus free notes)
        auto& tr = e.project().patterns[0].slots[2].track;
        const auto saved = tr;
        const int st = e.project().stepTicks (0);
        tr.convertToPianoRoll (st, e.project().patternSteps (0));
        for (auto [s0, len, n, v] : std::initializer_list<std::tuple<double, double, int, int>> { { 0, 6, 60, 110 }, { 0, 6, 63, 90 }, { 0, 6, 67, 90 }, { 6.5, 1.5, 70, 70 },
                                                                                                    { 8, 7.2, 58, 100 }, { 8, 7.2, 62, 90 }, { 11.3, 4, 65, 60 }, { 12, 3.6, 53, 80 } })
        {
            ClipNote c; c.start = (int) (s0 * st); c.length = (int) (len * st); c.note = (uint8_t) n; c.vel = (uint8_t) v;
            tr.addClipNote (c);
        }
        mc.seqView().refresh();
        shot (1, 2, "02c_seq_clip");
        // its pitch-bend lane
        tr.setPoint (DeviceProcessor::kBend, 0, 0.0f); tr.setPoint (DeviceProcessor::kBend, 4 * st, 0.6f); tr.setPoint (DeviceProcessor::kBend, 6 * st, 1.0f);
        tr.setPoint (DeviceProcessor::kBend, 9 * st, -0.4f); tr.setPoint (DeviceProcessor::kBend, 12 * st, 0.0f);
        mc.seqView().showLane (DeviceProcessor::kBend);
        shot (1, 2, "02e_seq_clip_bend");
        mc.seqView().showLane (-1);
        tr = saved;
    }
    {
        // the Play drawer in Step recording, as mockup 14_play
        auto& p = e.project();
        p.recordMode = 1; p.scale.lock = true; p.scale.scale = 6;
        auto& ps = p.patterns[0].slots[1].play;
        ps.chordOn = true; ps.chordType[4] = 8; ps.inversion = 1; ps.bassBelow = true; ps.strumMs = 10; ps.arpOn = true; ps.arpMode = 2; ps.arpOct = 1;
        e.setRecord (true);
        e.stepRecordCursor = 8;
        mc.showDrawer (true);
        shot (1, 1, "14_play");
        e.setRecord (false);
        p.recordMode = 0;
        shot (1, 1, "14b_play_live");   // Live recording's settings
        mc.showDrawer (false);
    }
    {
        // MIDI learn with the factory controller map, as mockup 15_midi_learn
        MidiMap::currentFileOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("minibox-shot.mbmidi");
        auto& map = mc.midiMap();
        if (! MidiMap::factoryMaps().isEmpty()) map.load (MidiMap::factoryMaps()[0]);
        mc.showLearn (true);
        map.arm ("macro:1:3");
        map.lastReceived = juce::String::fromUTF8 ("CC 77 = 64 \xc2\xb7 ch 1 \xc2\xb7 Arturia KeyLab Essential 49 MID");
        shot (0, 1, "15_midi_learn");
        mc.showLearn (false);
        MidiMap::currentFileOverride().deleteFile();
        MidiMap::currentFileOverride() = juce::File();
    }
    {
        // Settings: the keyboard shortcuts and the MIDI controller map
        MidiMap::currentFileOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("minibox-shot.mbmidi");
        if (! MidiMap::factoryMaps().isEmpty()) mc.midiMap().load (MidiMap::factoryMaps()[0]);
        ui::SettingsPanel panel (mc.context(), mc.midiMap(), [] { return new juce::Component(); });
        panel.setSize (900, 640);
        panel.setLookAndFeel (&mc.getLookAndFeel());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);   // the shortcut list fills in asynchronously
        snap (panel, "17_settings_keyboard");
        if (auto* tabs = dynamic_cast<juce::TabbedComponent*> (panel.getChildComponent (0))) tabs->setCurrentTabIndex (1);
        snap (panel, "17b_settings_midi");
        panel.setLookAndFeel (nullptr);
        MidiMap::currentFileOverride().deleteFile();
        MidiMap::currentFileOverride() = juce::File();
    }
    shot (2, 0, "03_drums");
    shot (2, 1, "04_acid");
    shot (2, 2, "05_poly");
    shot (2, 3, "06_fm");
    e.focusPattern = 1;
    shot (2, 0, "07_drumsampler");
    shot (2, 1, "08_mono");
    shot (2, 2, "09_poly_va");
    shot (2, 3, "10_sampler");
    {
        ui::AppContext bctx (e, true);
        e.focusPattern = 0;
        ui::PresetBrowser pb (bctx, *e.device (0, 1), ui::col::track (1));
        pb.setSize (480, 600);
        snap (pb, "12_preset_browser");
    }
    {
        MiniboxProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (1600, 1000);
        for (int i = 0; i < 3; ++i) { juce::MessageManager::getInstance()->runDispatchLoopUntil (40); }
        snap (*ed, "11_plugin");
    }
    ctx.check (shotDir().getChildFile ("11_plugin.png").existsAsFile(), "screenshots written");
}
