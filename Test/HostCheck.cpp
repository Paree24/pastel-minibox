#include "Tests.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace
{
float renderNote (juce::AudioPluginInstance& p, int note, int blocks = 60)
{
    juce::AudioBuffer<float> buf (2, 512);
    double sq = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 10);
        if (b == blocks / 2) midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        buf.clear();
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 512; ++i) sq += buf.getSample (ch, i) * buf.getSample (ch, i);
    }
    return (float) std::sqrt (sq / (blocks * 1024.0));
}
} // namespace

MB_TEST (tool_host_check)
{
    auto bundle = juce::File (MINIBOX_SOURCE_DIR).getChildFile ("build/MiniboxPlugin_artefacts/Release/VST3/Minibox.vst3");
    ctx.check (bundle.exists(), "VST3 bundle built");
    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> descs;
    fm.getFormat (0)->findAllTypesForFile (descs, bundle.getFullPathName());
    ctx.check (descs.size() == 1, "one plugin in the bundle");
    if (descs.isEmpty()) return;
    ctx.check (descs[0]->isInstrument && descs[0]->name == "Minibox", "an instrument called Minibox");
    juce::String err;
    auto inst = fm.createPluginInstance (*descs[0], 48000.0, 512, err);
    ctx.check (inst != nullptr, "instance created " + err);
    if (! inst) return;
    inst->setPlayConfigDetails (0, 2, 48000.0, 512);
    inst->prepareToPlay (48000.0, 512);
    const float rms = renderNote (*inst, 60);
    ctx.check (rms > 1.0e-3f, "plays MIDI, rms " + juce::String (rms));
    ctx.check (inst->getParameters().size() >= 14, "14 host parameters (" + juce::String (inst->getParameters().size()) + ")");
    // automate macro 1 (cutoff) and hear the change
    inst->getParameters()[0]->setValue (0.0f);
    const float dark = renderNote (*inst, 60);
    inst->getParameters()[0]->setValue (1.0f);
    const float bright = renderNote (*inst, 60);
    ctx.check (std::abs (dark - bright) > 1.0e-4f, "macro automation changes the sound");
    juce::MemoryBlock state;
    inst->getStateInformation (state);
    auto second = fm.createPluginInstance (*descs[0], 48000.0, 512, err);
    second->setPlayConfigDetails (0, 2, 48000.0, 512);
    second->prepareToPlay (48000.0, 512);
    second->setStateInformation (state.getData(), (int) state.getSize());
    juce::MemoryBlock state2;
    second->getStateInformation (state2);
    ctx.check (state == state2, "state round-trips through the host");
    std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
    ctx.check (ed != nullptr, "editor opens");
    if (ed) { ed->setSize (1280, 800); juce::MessageManager::getInstance()->runDispatchLoopUntil (100); }
    ed.reset();
    inst->releaseResources();
}
