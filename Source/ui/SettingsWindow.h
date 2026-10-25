#pragma once
#include "LearnDrawer.h"
#include "Shortcuts.h"

namespace mb::ui
{
/** Settings ▸ MIDI controller: the controller map as a table (control, message, target, mode), each row's target
    and mode changeable, its message re-learnt, rows added and removed; map files. Keys: Enter target, M mode,
    L learn, Delete remove, N add. */
class MidiMapEditor : public juce::Component, private juce::TableListBoxModel, private juce::Timer
{
public:
    MidiMapEditor (AppContext&, MidiMap&);
    ~MidiMapEditor() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int w, int h, bool selected) override;
    void cellClicked (int row, int column, const juce::MouseEvent&) override;
    void deleteKeyPressed (int) override;
    void returnKeyPressed (int) override;
    void timerCallback() override;
    void chooseTarget (int row);
    void learnRow (int row);
    void addMapping();
    void rename (int row);
    void showMaps();

    AppContext& ctx;
    MidiMap& map;
    juce::TableListBox table;
    juce::TextButton add { "Add a mapping" }, learn { "Learn the selected row" }, maps { "Map" }, load { "Load map..." },
                     save { "Save map..." }, clear { "Clear all" };
    juce::Label status;
    juce::String learningTarget, learningBefore;
    int shownRevision = -1;
    bool wasLearning = false;
    std::vector<juce::String> menuIds;
    std::unique_ptr<juce::FileChooser> chooser;
};

/** The Settings window (⋯ ▸ Settings, Ctrl+,): Keyboard (every shortcut, rebindable), MIDI controller (the map)
    and Audio and MIDI (devices). */
class SettingsPanel : public juce::Component, private juce::ChangeListener
{
public:
    SettingsPanel (AppContext&, MidiMap&, std::function<juce::Component*()> audioSettings);
    ~SettingsPanel() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    static void show (AppContext&, MidiMap&, std::function<juce::Component*()> audioSettings, int tab = 0);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { shortcuts().save(); }
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
};
} // namespace mb::ui
