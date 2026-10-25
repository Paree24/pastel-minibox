#pragma once
#include "PluginProcessor.h"
#include "ui/EditorView.h"
#include "ui/LookAndFeel.h"

namespace mb
{
/** Plugin top bar (desktop.py plugin_header): brand and the instrument tabs. */
class PluginHeader : public juce::Component
{
public:
    explicit PluginHeader (MiniboxProcessor&);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();
    std::function<void()> onSwitched;
private:
    MiniboxProcessor& proc;
    int shownType = -1;
    std::vector<std::pair<juce::Rectangle<float>, DeviceType>> tabs;
    ui::Feedback fb { *this };
};

/** MIDI activity: a keyboard lighting the sounding notes, plus host tempo. Sits in the step strip's place. */
class MidiRow : public juce::Component
{
public:
    explicit MidiRow (MiniboxProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void refresh();
private:
    MiniboxProcessor& proc;
    juce::String sig;
};

/** The plugin: instrument tabs, then exactly the device editor layout of the mockups (03–10) in the
    device's own colour. */
class MiniboxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MiniboxEditor (MiniboxProcessor&);
    ~MiniboxEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    /** Keyboard (docs/keyboard.md): Tab, arrows, Enter and Esc walk and operate every control, as in the app. */
    bool keyPressed (const juce::KeyPress&) override;
private:
    void timerCallback() override;
    void attachDevice();
    void showInstrumentMenu (juce::Component* anchor);
    MiniboxProcessor& proc;
    ui::MiniboxLookAndFeel laf;
    ui::AppContext ctx;
    juce::Component content;
    PluginHeader topBar;
    ui::EditorHeader header;
    std::unique_ptr<ui::EditorBody> body;
    MidiRow midiRow;
    pastel::MidiLearnButton midiBtn;
    DeviceProcessor* shown = nullptr;
    std::array<float, 8> uiMacro {};
};
} // namespace mb
