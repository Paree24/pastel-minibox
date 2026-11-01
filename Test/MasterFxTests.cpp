#include "Tests.h"
#include "efx/MasterFx.h"
#include "model/Serializer.h"
#include "app/presets/PresetBank.h"
#include "app/MainComponent.h"

using namespace mb;

namespace
{
/** A held VA chord into the master bus; the RMS of the next `blocks` blocks. */
struct FxRig
{
    Engine e;
    MasterFx fx { e };
    std::vector<float> l = std::vector<float> (512), r = std::vector<float> (512);
    FxRig()
    {
        e.prepare (48000.0, 512);
        auto d = createDevice (DeviceType::Poly);
        presets::loadDefault (*d, true);
        e.setDevice (0, 0, std::move (d));
        e.focusTrack = 0;
        e.project().master.limitDb = 0.0f;
        e.project().master.glue = 0.0f;   // the bus compressor would still be recovering after a pad: test the pads alone
        e.setMasterInsert (&fx);
        fx.sync();
        juce::MidiBuffer m;
        for (int n : { 48, 55, 60 }) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        e.process (l.data(), r.data(), 512, &m);
        rms (40);   // past the attack
    }
    float rms (int blocks, std::vector<float>* keep = nullptr)
    {
        double sq = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            e.process (l.data(), r.data(), 512, nullptr);
            for (int i = 0; i < 512; ++i) { sq += l[(size_t) i] * l[(size_t) i]; if (keep) keep->push_back (l[(size_t) i]); }
        }
        return (float) std::sqrt (sq / (blocks * 512.0));
    }
};
} // namespace

/** Master effects: a slot changes the bus (a Gain slot at −12 dB: a quarter of the level), bypass restores it;
    empty slots cost nothing. */
MB_TEST (master_fx_slot_chain)
{
    // the same chord through two engines in lockstep: one with the chain, one dry (the pad still swells)
    FxRig rig, dry;
    auto both = [&] (int blocks) { const float a = rig.rms (blocks); return std::pair<float, float> { a, dry.rms (blocks) }; };
    auto& slot = rig.e.project().masterFx[2];
    MasterFx::setEffect (slot, "Gain");
    const auto* d = efx::defForId (efx::idForName ("Gain"));
    slot.norm[0] = efx::mapParam (d->p[0], -12.0f);
    rig.fx.sync();
    ctx.check (rig.fx.instance (false, 2) != nullptr, "slot 3 runs the Gain effect");
    both (10);
    const auto [quiet, ref] = both (20);
    ctx.check (quiet < ref * 0.3f && quiet > ref * 0.2f, "−12 dB in the chain (" + juce::String (ref, 4) + " -> " + juce::String (quiet, 4) + ")");
    slot.bypass = true;
    both (10);
    const auto [back, ref2] = both (20);
    ctx.check (std::abs (back - ref2) < ref2 * 0.01f, "bypassed: the dry level (" + juce::String (back, 4) + " / " + juce::String (ref2, 4) + ")");
    MasterFx::setEffect (slot, "");
    rig.fx.sync();
    ctx.check (rig.fx.instance (false, 2) == nullptr, "removed: the slot is empty");
    ctx.check (MasterFx::choosableEffects().size() > 50, "a long list of effects to choose from (" + juce::String ((int) MasterFx::choosableEffects().size()) + ")");
}

/** Performance pads: the effect works only while its pad is held, and every pad's effect is there. */
MB_TEST (master_fx_perform_pads_hold)
{
    FxRig rig;
    for (int i = 0; i < kPerformFx; ++i)
        ctx.check (rig.fx.instance (true, i) != nullptr && rig.e.project().performFx[(size_t) i].effect.isNotEmpty(),
                   juce::String ("pad ") + MasterFx::padName (i) + ": " + rig.e.project().performFx[(size_t) i].effect);
    for (int pad = 0; pad < kPerformFx; ++pad)
    {
        // the same chord through two engines in lockstep: one with the pad held, one dry
        FxRig held, dry;
        auto diff = [&] (int blocks)
        {
            std::vector<float> a, b;
            held.rms (blocks, &a); dry.rms (blocks, &b);
            double d = 0.0, ref = 0.0;
            for (size_t i = 0; i < a.size(); ++i) { d += std::abs (a[i] - b[i]); ref += std::abs (b[i]); }
            return d / std::max (1.0e-9, ref);
        };
        const auto name = juce::String (MasterFx::padName (pad));
        ctx.check (diff (8) < 1.0e-4, name + ": not held, nothing changes");
        held.fx.setHeld (pad, true);
        const double on = diff (40);
        ctx.check (on > 0.05, name + ": held, the sound changes (" + juce::String (on, 3) + ")");
        held.fx.setHeld (pad, false);
        diff (12);   // the fade (a 10 ms time constant: gone after ~70 ms)
        const double off = diff (8);
        ctx.check (off < 1.0e-3, name + ": released, back to the dry sound (" + juce::String (off, 5) + ")");
    }
}

/** The chain and the pads' settings are saved with the project. */
MB_TEST (master_fx_saved_with_project)
{
    Engine e;
    e.prepare (48000.0, 512);
    e.setDevice (0, 0, createDevice (DeviceType::Poly));
    MasterFx::setEffect (e.project().masterFx[0], "Compressor");
    e.project().masterFx[0].norm[1] = 0.7f;
    e.project().masterFx[0].bypass = true;
    MasterFx::defaultPad (e.project().performFx[3], 3);
    e.project().performFx[3].mix = 0.6f;
    const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("minibox_fx_test.minibox");
    ctx.check (serializer::save (e, f).ok, "saved");
    Engine e2;
    e2.prepare (48000.0, 512);
    ctx.check (serializer::load (e2, f).ok, "loaded");
    const auto& s = e2.project().masterFx[0];
    ctx.check (s.effect == "Compressor" && std::abs (s.norm[1] - 0.7f) < 1e-6f && s.bypass, "slot 1: the compressor, its ratio, bypassed");
    ctx.check (e2.project().performFx[3].effect == "Slicer" && std::abs (e2.project().performFx[3].mix - 0.6f) < 1e-6f, "pad 4: the slicer at 60% mix");
    f.deleteFile();
}

/** FX keys: off, every note plays; on, 8 notes from any controller (any channel) hold the effects and the rest play.
    The first note is learnt from the next note. The KeyLab pad mappings a short-lived map added are removed. */
MB_TEST (master_fx_keys)
{
    // a saved map from the short-lived KeyLab version: pads on channel 10 → the effects
    const auto old = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("minibox-old-keylab.mbmidi");
    {
        MidiMap m;
        m.name = "KeyLab Essential 49 mk2";
        for (int i = 0; i < 8; ++i)
        {
            MidiMapping mp;
            mp.kind = MidiMapping::Kind::Note; mp.channel = 10; mp.number = 36 + i; mp.port = "KeyLab Essential 49 MID";
            mp.target = "perform:" + juce::String (i); mp.mode = MidiMapping::Mode::Momentary; mp.label = "Pad " + juce::String (i + 1);
            m.mappings.push_back (mp);
        }
        MidiMapping knob; knob.kind = MidiMapping::Kind::CC; knob.number = 74; knob.target = "macro:sel:0"; knob.label = "Knob 1";
        m.mappings.push_back (knob);
        m.save (old);
    }
    MidiMap::currentFileOverride() = old;
    Engine e;
    e.prepare (48000.0, 512);
    {
        MainComponent mc (e);
        auto& map = mc.midiMap();
        int pads = 0;
        for (auto& m : map.mappings) pads += m.target.startsWith ("perform:");
        ctx.check (pads == 0 && map.mappings.size() == 1, "the old channel-10 pad mappings are gone, the knob stays");
        const juce::String port = "KeyLab Essential 49 MID";
        ctx.check (! map.filter (port, juce::MidiMessage::noteOn (1, 37, (juce::uint8) 100)), "FX keys off: the pad's note plays");
        map.filter (port, juce::MidiMessage::noteOff (1, 37));
        mc.context().setFxKeys (true);
        ctx.check (map.filter (port, juce::MidiMessage::noteOn (1, 37, (juce::uint8) 100)) && mc.masterEffects().isHeld (1), "FX keys on: note 37 holds Glitch (channel 1)");
        ctx.check (map.filter (port, juce::MidiMessage::noteOff (1, 37)) && ! mc.masterEffects().isHeld (1), "released: Glitch off");
        ctx.check (map.filter ("Any keyboard", juce::MidiMessage::noteOn (5, 43, (juce::uint8) 90)) && mc.masterEffects().isHeld (7), "any input, any channel: note 43 holds Reverse");
        map.filter ("Any keyboard", juce::MidiMessage::noteOff (5, 43));
        ctx.check (! map.filter (port, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)), "notes outside the 8 still play");
        // learn the first note from the next note
        map.fxKeysLearning = true;
        ctx.check (map.filter (port, juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100)) && map.fxKeysBase.load() == 48, "the next note sets the first FX key (48)");
        ctx.check (map.filter (port, juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100)) && mc.masterEffects().isHeld (2), "now 50 holds Shuffle");
        map.filter (port, juce::MidiMessage::noteOff (1, 50));
        mc.context().setFxKeys (false);
        ctx.check (! map.filter (port, juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100)), "FX keys off: it plays again");
        map.dispatch();
    }
    MidiMap reread;
    ctx.check (reread.load (old) && reread.fxKeysBase.load() == 48 && ! reread.fxKeysOn.load(), "the first note and the mode are saved with the map");
    MidiMap::currentFileOverride() = juce::File();
    old.deleteFile();
}

/** Track effects (3 slots per device, the phone's Sound → FX): they change that track's sound and nothing else,
    survive a project save and load, and an empty or bypassed slot leaves the track as it was. */
MB_TEST (track_fx_slots)
{
    FxRig rig;
    std::vector<float> dry, wet, bypassed;
    rig.rms (8, &dry);
    rig.fx.setTrackEffect (0, 0, 1, "Bitcrusher");
    auto& slot = rig.e.project().patterns[0].slots[0].fx[1];
    ctx.check (slot.effect == "Bitcrusher", "slot 2 holds the effect at its defaults");
    slot.mix = 1.0f;
    ctx.check (rig.fx.trackActive (0, 0) && ! rig.fx.trackActive (0, 1), "only that track runs effects");
    rig.rms (8, &wet);
    double diff = 0.0;
    for (size_t i = 0; i < std::min (dry.size(), wet.size()); ++i) diff += std::abs (dry[i] - wet[i]);
    ctx.check (diff > 1.0, "the crushed chord differs from the dry one (" + juce::String (diff, 2) + ")");
    slot.bypass = true;
    ctx.check (! rig.fx.trackActive (0, 0), "bypassed: nothing to run");

    // the slots travel with the project
    const auto json = serializer::projectToJson (rig.e.project());
    Project back;
    serializer::projectFromJson (back, json);
    ctx.check (back.patterns[0].slots[0].fx[1].effect == "Bitcrusher" && back.patterns[0].slots[0].fx[1].bypass
               && back.patterns[0].slots[0].fx[0].effect.isEmpty(), "saved and loaded with the project");
    rig.fx.setTrackEffect (0, 0, 1, "");
    ctx.check (rig.e.project().patterns[0].slots[0].fx[1].effect.isEmpty() && ! rig.fx.trackActive (0, 0), "emptied");
}

/** Tool (MB_PADS=1): the timed performance pads' default settings, by name. */
MB_TEST (tool_pad_defaults)
{
    if (juce::SystemStats::getEnvironmentVariable ("MB_PADS", {}).isEmpty()) return;
    for (int pad = 0; pad < 4; ++pad)
    {
        FxSlot fx;
        MasterFx::defaultPad (fx, pad);
        const auto* d = efx::defForId (efx::idForName (fx.effect));
        juce::String line = juce::String (MasterFx::padName (pad)) + " (" + fx.effect + ", mode " + juce::String (fx.mode) + "):";
        for (int k = 0; k < d->numParams; ++k) line << " " << d->p[k].name << "=" << efx::formatParam (d->p[k], fx.norm[(size_t) k]);
        std::cout << "    " << line << std::endl;
    }
}

/** Tool (MB_PADS=1): each timed pad held over a playing drum pattern (from bar 2 for 2 bars) against the same render
    without it: how much it changes the sound, and the high-frequency share (a filter would lower it). */
MB_TEST (tool_pad_effect_renders)
{
    if (juce::SystemStats::getEnvironmentVariable ("MB_PADS", {}).isEmpty()) return;
    auto render = [] (int pad, std::vector<float>& out)
    {
        Engine e;
        e.prepare (48000.0, 512);
        MasterFx fx (e);
        e.setMasterInsert (&fx);
        auto d = createDevice (DeviceType::Drums);
        presets::loadDefault (*d, true);
        e.setDevice (0, 0, std::move (d));
        fx.sync();
        auto& tr = e.project().patterns[0].slots[0].track;
        for (int st = 0; st < 16; ++st) { tr.lanes[0][(size_t) st].on = st % 4 == 0; tr.lanes[4][(size_t) st].on = st % 2 == 0; tr.lanes[1][(size_t) st].on = st % 8 == 4; }
        e.play();
        std::vector<float> l (512), r (512);
        const int bar = 96000;
        for (int pos = 0; pos < bar * 4; pos += 512)
        {
            if (pad >= 0 && pos >= bar && pos < bar + 512) fx.setHeld (pad, true);
            if (pad >= 0 && pos >= bar * 3 && pos < bar * 3 + 512) fx.setHeld (pad, false);
            e.process (l.data(), r.data(), 512, nullptr);
            out.insert (out.end(), l.begin(), l.end());
        }
        e.setMasterInsert (nullptr);
    };
    std::vector<float> dry;
    render (-1, dry);
    auto hf = [] (const std::vector<float>& x, int a, int b)
    {
        double all = 0, high = 0, prev = 0;
        for (int i = a; i < b; ++i) { const double d = x[(size_t) i] - prev; prev = x[(size_t) i]; high += d * d; all += (double) x[(size_t) i] * x[(size_t) i]; }
        return high / (all + 1e-12);
    };
    for (int pad = 0; pad < 4; ++pad)
    {
        std::vector<float> wet;
        render (pad, wet);
        double diff = 0, ref = 0;
        for (int i = 96000; i < 96000 * 3; ++i) { const double dd = wet[(size_t) i] - dry[(size_t) i]; diff += dd * dd; ref += (double) dry[(size_t) i] * dry[(size_t) i]; }
        for (int b = 1; b <= 2; ++b)
        {
            double d2 = 0, r2 = 0;
            for (int i = 96000 * b; i < 96000 * (b + 1); ++i) { const double dd = wet[(size_t) i] - dry[(size_t) i]; d2 += dd * dd; r2 += (double) dry[(size_t) i] * dry[(size_t) i]; }
            std::cout << "    " << MasterFx::padName (pad) << " bar " << b + 1 << " of the hold: change " << juce::String (10 * std::log10 ((d2 + 1e-12) / (r2 + 1e-12)), 1) << " dB" << std::endl;
        }
        std::cout << "    " << MasterFx::padName (pad) << ": change " << juce::String (10 * std::log10 ((diff + 1e-12) / (ref + 1e-12)), 1)
                  << " dB, hf share held " << juce::String (hf (wet, 96000, 96000 * 3), 4) << " vs dry " << juce::String (hf (dry, 96000, 96000 * 3), 4) << std::endl;
    }
}

/** Tool (MB_PADS=1): the Slicer pad over a sustained chord: how long each slice stays above half level (ms). */
MB_TEST (tool_slicer_gate)
{
    if (juce::SystemStats::getEnvironmentVariable ("MB_PADS", {}).isEmpty()) return;
    Engine e;
    e.prepare (48000.0, 512);
    MasterFx fx (e);
    e.setMasterInsert (&fx);
    auto d = createDevice (DeviceType::Poly);
    presets::loadDefault (*d, true);
    e.setDevice (0, 0, std::move (d));
    fx.sync();
    auto& tr = e.project().patterns[0].slots[0].track;
    tr.pianoRoll = true;
    for (int n : { 48, 55, 60 }) { ClipNote c; c.start = 0; c.length = e.project().patternTicks (0); c.note = (uint8_t) n; tr.addClipNote (c); }
    e.play();
    std::vector<float> l (512), r (512), out;
    for (int pos = 0; pos < 96000 * 3; pos += 512)
    {
        if (pos >= 96000 && pos < 96000 + 512) fx.setHeld (3, true);
        e.process (l.data(), r.data(), 512, nullptr);
        if (pos >= 96000 * 2) out.insert (out.end(), l.begin(), l.end());
    }
    e.setMasterInsert (nullptr);
    std::vector<double> env;
    for (size_t i = 0; i + 480 < out.size(); i += 480) { double a = 0; for (size_t k = 0; k < 480; ++k) a = std::max (a, (double) std::abs (out[i + k])); env.push_back (a); }
    const double top = *std::max_element (env.begin(), env.end());
    std::vector<double> bursts; int run = 0;
    for (double v : env) { if (v > top * 0.5) ++run; else if (run > 0) { bursts.push_back (run * 10.0); run = 0; } }
    double avg = 0; for (double b : bursts) avg += b;
    std::cout << "    slicer: " << bursts.size() << " slices a bar, each above half level for " << juce::String (bursts.empty() ? 0.0 : avg / bursts.size(), 0) << " ms on average" << std::endl;
}

/** Tool (MB_PADS=1): the Slicer pad's gate itself (the effect on a constant 1.0 at 120 BPM, playing): per bar, the
    slices and how long each stays above half (ms). MB_SLICER_DECAY overrides the pad's Decay (ms). */
MB_TEST (tool_slicer_gate_direct)
{
    if (juce::SystemStats::getEnvironmentVariable ("MB_PADS", {}).isEmpty()) return;
    FxSlot fx;
    MasterFx::defaultPad (fx, 3);
    const auto* d = efx::defForId (efx::idForName (fx.effect));
    if (auto ms = juce::SystemStats::getEnvironmentVariable ("MB_SLICER_DECAY", {}); ms.isNotEmpty())
        for (int k = 0; k < d->numParams; ++k) if (juce::String (d->p[k].name) == "Decay") fx.norm[(size_t) k] = efx::mapParam (d->p[k], ms.getFloatValue());
    efx::GridEffect g (efx::idForName (fx.effect));
    g.prepare (48000.0, 512);
    efx::SlotInput in; in.norm = fx.norm.data(); in.mix = 1.0f; in.mode = fx.mode; in.bpm = 120.0; in.playing = true;
    std::vector<float> l (512), r (512), gate;
    for (int pos = 0; pos < 96000 * 2; pos += 512)
    {
        std::fill (l.begin(), l.end(), 1.0f); std::fill (r.begin(), r.end(), 1.0f);
        in.ppq = pos / 24000.0;
        g.process (l.data(), r.data(), 512, in);
        if (pos >= 96000) gate.insert (gate.end(), l.begin(), l.end());
    }
    std::vector<double> runs; int run = 0;
    for (float v : gate) { if (v > 0.5f) ++run; else if (run > 0) { runs.push_back (run / 48.0); run = 0; } }
    double avg = 0; for (double x : runs) avg += x;
    std::cout << "    slicer gate: " << runs.size() << " slices a bar, above half for " << juce::String (runs.empty() ? 0.0 : avg / runs.size(), 1) << " ms on average" << std::endl;
}
