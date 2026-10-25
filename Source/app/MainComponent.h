#pragma once
#include "ui/TopBar.h"
#include "ui/RackView.h"
#include "ui/SeqView.h"
#include "ui/EditorView.h"
#include "ui/PlayDrawer.h"
#include "ui/LearnDrawer.h"
#include "ui/SettingsWindow.h"
#include "ui/LookAndFeel.h"

namespace mb
{
/** Standalone root: top bar plus Rack / Seq / device editor pages, laid out at 1600 × 1000 and scaled
    to the window. Owns the project file actions and the computer keyboard. */
class MainComponent : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent (Engine&);
    ~MainComponent() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    // project actions (also used by the window's close button)
    void newProject();
    void openProject();
    void openFile (const juce::File&);
    void saveProject (bool saveAs, std::function<void()> after = {});
    void exportWav();
    void exportMidi();
    void exportDawProject();
    void importMidi();
    void askToSaveThen (std::function<void()> action);
    juce::String windowTitle() const;
    std::function<void()> onTitleChanged;
    /** Page switching for tests and screenshots: 0 Rack, 1 Seq, 2 editor of `track`. */
    void showPage (int page, int track = 0);
    void refreshAll() { timerCallback(); }
    /** Standalone: autosave the project every minute while it has unsaved changes, and offer to recover the
        autosave after a crash (the previous session never closed). */
    void enableAutosave();
    static juce::File autosaveFile();
    static juce::File runningMarker();
    /** The Play drawer (top-bar keyboard button). */
    void showDrawer (bool on);
    bool drawerOpen() const { return drawer.isVisible(); }
    /** MIDI learn mode and its drawer (docs/midi-learn.md). */
    void showLearn (bool on);
    bool learnOpen() const { return learnDrawer.isVisible(); }
    MidiMap& midiMap() { return map; }
    /** Runs a mappable action (learnActions(): top-bar ids, track-prev, bar-next, undo, …). */
    void runAction (const juce::String& id);
    bool onSeqPage() const { return view == ui::TopBar::View::Seq; }
    ui::AppContext& context() { return ctx; }
    ui::SeqView& seqView() { return seq; }
    /** The track the device menu was last opened for (tests). */
    int lastPickerTrack = -1;
    std::function<void()> showAudioSettings;
    /** The audio / MIDI device panel (Settings ▸ Audio and MIDI devices), made by the window that owns the devices. */
    std::function<juce::Component*()> createAudioSettings;

private:
    void timerCallback() override;
    void setView (ui::TopBar::View);
    void showDevicePicker (int track, juce::Component* anchor);
    void showMenu();
    void markClean();
    void selectTrack (int dir);
    void runCommand (juce::CommandID);
    void showSettings();
    void addDevice();
    void moveBar (int dir);
    void midiControl (const juce::String& target, const MidiControl&, MidiMapping&);

    Engine& engine;
    ui::MiniboxLookAndFeel laf;
    ui::AppContext ctx;
    MidiMap map;
    int mapRevision = -1;
    juce::Component content;
    ui::TopBar topBar;
    ui::RackView rack;
    ui::SeqView seq;
    ui::EditorView editor;
    ui::PlayDrawer drawer;
    ui::LearnDrawer learnDrawer;
    ui::TopBar::View view = ui::TopBar::View::Rack;
    juce::File currentFile;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { nullptr, 600 };
    // computer keyboard: A S D F G H J K white keys, W E T Y U O P black keys, Z / X octave
    int keyboardOctave = 4;
    std::map<int, int> keysDown;   // key code → note
    int retireCounter = 0;
    bool autosaveOn = false;
    int autosaveCounter = 0;
};
} // namespace mb
