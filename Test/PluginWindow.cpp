#include "Tests.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** Opens the installed VST3 the way a host does and keeps its editor window on screen for a while,
    so the real window can be captured (tool, not a check). */
MB_TEST (tool_plugin_window)
{
    auto bundle = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".vst3/Minibox.vst3");
    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> descs;
    fm.getFormat (0)->findAllTypesForFile (descs, bundle.getFullPathName());
    if (descs.isEmpty()) { ctx.check (false, "plugin found"); return; }
    juce::String err;
    auto inst = fm.createPluginInstance (*descs[0], 48000.0, 512, err);
    inst->prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
    std::printf ("    editor size %d x %d\n", ed->getWidth(), ed->getHeight());
    juce::DocumentWindow w ("Minibox (VST3 host)", juce::Colours::black, juce::DocumentWindow::allButtons);
    w.setUsingNativeTitleBar (true);
    w.setContentNonOwned (ed.get(), true);
    w.setVisible (true);
    w.toFront (true);
    const int ms = juce::SystemStats::getEnvironmentVariable ("MB_HOLD_MS", "6000").getIntValue();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    w.clearContentComponent();
    ed.reset();
    ctx.check (true, "shown");
}
