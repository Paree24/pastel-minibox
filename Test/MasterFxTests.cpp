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

/** The controller's pads hold the performance effects (KeyLab: pads on channel 10, notes 36–43); the keys play the
    same notes on channel 1 and stay notes. A map saved before the pads existed gets them. */
MB_TEST (master_fx_controller_pads)
{
    // an old copy of the shipped KeyLab map: without the pads
    const auto factory = MidiMap::factoryMaps();
    juce::File keylab;
    for (auto& f : factory) if (f.getFileName().contains ("KeyLab")) keylab = f;
    ctx.check (keylab.existsAsFile(), "the shipped KeyLab map");
    auto v = juce::JSON::parse (keylab);
    auto* list = v["mappings"].getArray();
    for (int i = list->size(); --i >= 0;) if ((*list)[i]["target"].toString().startsWith ("perform:")) list->remove (i);
    const auto old = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("minibox-old-keylab.mbmidi");
    old.replaceWithText (juce::JSON::toString (v));
    MidiMap::currentFileOverride() = old;
    Engine e;
    e.prepare (48000.0, 512);
    {
        MainComponent mc (e);
        auto& map = mc.midiMap();
        int pads = 0;
        for (auto& m : map.mappings) pads += m.target.startsWith ("perform:") && m.channel == 10 && m.mode == MidiMapping::Mode::Momentary;
        ctx.check (pads == 8, "the saved map got the 8 pads (" + juce::String (pads) + ")");
        const juce::String port = "KeyLab Essential 49 MID";
        ctx.check (map.filter (port, juce::MidiMessage::noteOn (10, 37, (juce::uint8) 100)), "pad 2 (channel 10) is taken by the map");
        map.dispatch();
        ctx.check (mc.masterEffects().isHeld (1), "pad 2 holds Glitch");
        map.filter (port, juce::MidiMessage::noteOff (10, 37));
        map.dispatch();
        ctx.check (! mc.masterEffects().isHeld (1), "released: Glitch off");
        ctx.check (! map.filter (port, juce::MidiMessage::noteOn (1, 37, (juce::uint8) 100)), "the same note from the keys (channel 1) plays as a note");
    }
    MidiMap::currentFileOverride() = juce::File();
    old.deleteFile();
}
