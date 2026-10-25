#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

namespace mb::ui
{
/** Every keyboard shortcut of the standalone app (docs/keyboard.md), rebindable in Settings ▸ Keyboard and saved
    to <user app data>/Pastel Minibox/shortcuts.xml. Focus keys (Tab, arrows, Enter, Esc) stay fixed. */
namespace cmd
{
enum : juce::CommandID
{
    // project
    newProject = 1, openProject, saveProject, saveProjectAs, exportWav, exportMidi, importMidi, settings,
    // transport and recording
    playStop, toStart, recordLive, recordStep, metronome, tempoUp, tempoDown, tempoUpFine, tempoDownFine, perform,
    // pages and drawers
    pageRack, pageSeq, pageEditor, playDrawer, midiLearn,
    // instruments
    trackPrev, trackNext, addDevice, presetBrowser, presetPrev, presetNext, barPrev, barNext, undo, redo, redoAlt,
    // patterns A–H
    pattern0, pattern7 = pattern0 + 7,
    // piano roll
    rollSelectAll, rollInvertSelection, rollCopy, rollCut, rollPaste, rollDuplicate, rollDelete, rollDeleteAlt, rollQuantize,
    rollMute, rollLock, rollTool, rollSnap, rollZoomIn, rollZoomOut, rollRowsIn, rollRowsOut, rollFit, rollFitNotes,
    rollTransposeUp, rollTransposeDown, rollOctaveUp, rollOctaveDown, rollShorter, rollLonger, rollVelocityDown, rollVelocityUp,
    rollPlayFromCursor, rollLoopSelection, rollLoopToggle,
    // page keys: the same keys do what fits the page in view
    rackTrackPrev, rackTrackNext, rackPresetPrev, rackPresetNext,
    seqTrackPrev, seqTrackNext, seqBarPrev, seqBarNext,
    editorTrackPrev, editorTrackNext, editorPresetPrev, editorPresetNext,
    // the computer keyboard as a piano: 16 keys from C up, and the octave
    note0, note15 = note0 + 15, octaveDown, octaveUp,
    // added later (at the end, so saved key maps keep their IDs)
    exportDawProject
};
} // namespace cmd

class Shortcuts
{
public:
    Shortcuts();
    juce::ApplicationCommandManager& manager() { return commands; }
    juce::KeyPressMappingSet& mappings() { return *commands.getKeyMappings(); }
    /** True when `k` is one of the keys assigned to `id`. */
    bool is (juce::CommandID id, const juce::KeyPress& k) const;
    /** The first command of these categories that `k` triggers (0: none). */
    juce::CommandID find (const juce::KeyPress& k, const juce::StringArray& categories) const;
    /** The key a command's first assignment uses (the note keys are held: keyStateChanged polls them). */
    int keyCode (juce::CommandID id) const;
    juce::String describe (juce::CommandID id) const;
    void save() const;
    void load();
    static juce::File file();
    static const juce::StringArray& globalCategories();
    /** The page's own keys first, then the global ones (page: 0 Rack, 1 Seq, 2 the editor). */
    static juce::StringArray categoriesFor (int page);

private:
    void add (juce::CommandID, const juce::String& name, const juce::String& category, std::initializer_list<juce::KeyPress> keys);
    juce::ApplicationCommandManager commands;
};

Shortcuts& shortcuts();
} // namespace mb::ui
