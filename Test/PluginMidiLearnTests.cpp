#include "Tests.h"
#include "plugin/PluginProcessor.h"
#include "app/presets/PresetBank.h"

using namespace mb;

/** Plugin MIDI learn (pastel::MidiLearn): global CC map of the host parameters + the preset knob. */
MB_TEST (plugin_midi_learn)
{
    using Enc = pastel::MidiLearn::Enc;
    auto det = [] (std::vector<int> v) { return pastel::MidiLearn::detectEncoding (v); };
    ctx.check (det ({ 65, 65, 65, 65 }) == Enc::offset64 && det ({ 63, 62, 63 }) == Enc::offset64, "Arturia-style 64+n encoder detected");
    ctx.check (det ({ 1, 1, 2, 1 }) == Enc::twos && det ({ 127, 127, 126 }) == Enc::twos, "two's complement encoder detected");
    ctx.check (det ({ 60, 61, 62, 63 }) == Enc::absolute && det ({ 0, 1, 2, 3 }) == Enc::absolute
                   && det ({ 127, 126, 125, 124 }) == Enc::absolute, "turned absolute knob stays absolute");

    auto cc = [] (int num, int val) { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, num, val), 0); return m; };
    auto a = std::make_unique<MiniboxProcessor>();
    auto& ml = a->midiLearn();
    ml.clearAll();
    juce::AudioProcessorParameter* macro = a->macroParams[0];

    // a UI move of the macro is a gesture, which arms it
    ml.setLearning (true);
    a->macroChangedByUi (0, 0.3f);
    ctx.check (ml.getArmedName() == macro->getName (40), "moving a macro in the UI arms it");
    const float before = macro->getValue();
    for (int i = 0; i < 4; ++i) ml.process (cc (74, 65));
    ctx.check (std::abs (macro->getValue() - before) < 1e-6f, "learning CCs never move the armed control");
    ml.tick();
    auto maps = ml.getMappings();
    ctx.check (maps.size() == 1 && maps[0].cc == 74 && maps[0].paramId == "macro1" && maps[0].enc == Enc::offset64,
               "CC 74 learned as a relative knob on Macro 1");
    ml.setLearning (false);
    {
        // the learned CC reaches the instrument through processBlock
        a->prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buf (2, 512);
        auto m = cc (74, 70);
        a->processBlock (buf, m);
        ctx.check (macro->getValue() > before && std::abs (a->device()->getMacro (0) - a->macroParams[0]->get()) < 1e-4f,
                   "a learned CC in processBlock moves the instrument's macro");
    }
    const float up = macro->getValue();
    ml.process (cc (74, 60));
    ctx.check (macro->getValue() < up, "endless knob turns back down");

    // host/CC writes are not gestures: no arming while learning
    ml.setLearning (true);
    a->macroChangedByUi (0, a->macroParams[0]->get());
    ctx.check (ml.getArmedName().isEmpty(), "a host-side macro change does not arm learn");
    ml.setLearning (false);

    // global: a new instance has the mapping
    auto b = std::make_unique<MiniboxProcessor>();
    const float bBefore = static_cast<juce::AudioProcessorParameter*> (b->macroParams[0])->getValue();
    b->midiLearn().process (cc (74, 70));
    ctx.check (b->midiLearn().getMappings().size() == 1 && static_cast<juce::AudioProcessorParameter*> (b->macroParams[0])->getValue() > bBefore, "mapping is global: a new instance has it");

    // preset knob: one nudge learns it, detents step the instrument's presets
    int stepped = 0;
    ml.onPresetStep = [&] (int d) { stepped += d; };
    ml.armPresetKnob();
    ml.process (cc (114, 1));
    for (int i = 0; i < 10; ++i) ml.tick();
    ctx.check (ml.getPresetKnobCC() == 114, "preset knob learned from one nudge");
    ml.process (cc (114, 1)); ml.process (cc (114, 3)); ml.tick();
    ml.process (cc (114, 127)); ml.tick();
    ctx.check (stepped == 1, "preset knob: two steps forward, one back");

    auto c = std::make_unique<MiniboxProcessor>();
    auto list = presets::list (c->device()->type());
    const auto name0 = c->device()->presetName;
    c->midiLearn().process (cc (114, 1));
    c->midiLearn().tick();
    ctx.check (list.size() > 1 && c->device()->presetName != name0, "preset knob loads the next preset in a new instance");
    c->midiLearn().process (cc (114, 127));
    c->midiLearn().tick();
    ctx.check (c->device()->presetName == name0, "and turns back to the previous one");
    ml.clearAll();
    ctx.check (ml.getMappings().empty() && ml.getPresetKnobCC() < 0, "clear all");
}
