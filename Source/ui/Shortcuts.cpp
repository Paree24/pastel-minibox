#include "Shortcuts.h"

namespace mb::ui
{
namespace
{
using K = juce::KeyPress;
using M = juce::ModifierKeys;
const int ctrl = M::commandModifier, shift = M::shiftModifier, alt = M::altModifier;
K key (int code, int mods = 0) { return K (code, M (mods), 0); }
} // namespace

Shortcuts& shortcuts() { static Shortcuts s; return s; }

const juce::StringArray& Shortcuts::globalCategories()
{
    static const juce::StringArray c { "Project", "Transport and recording", "Pages and drawers", "Instruments", "Patterns" };
    return c;
}

Shortcuts::Shortcuts()
{
    using namespace cmd;
    add (newProject, "New project", "Project", { key ('N', ctrl) });
    add (openProject, "Open a project", "Project", { key ('O', ctrl) });
    add (saveProject, "Save the project", "Project", { key ('S', ctrl) });
    add (saveProjectAs, "Save the project as", "Project", { key ('S', ctrl | shift) });
    add (exportWav, "Export the pattern as a WAV", "Project", { key ('E', ctrl) });
    add (exportMidi, "Export the pattern as a MIDI file", "Project", { key ('E', ctrl | shift) });
    add (importMidi, "Import a MIDI file into the selected track", "Project", { key ('I', ctrl) });
    add (settings, "Settings", "Project", { key (',', ctrl) });

    add (playStop, "Play / stop", "Transport and recording", { key (K::spaceKey) });
    add (toStart, "Back to the start", "Transport and recording", { key (K::homeKey) });
    add (recordLive, "Live record", "Transport and recording", { key ('R', ctrl) });
    add (recordStep, "Step record", "Transport and recording", { key ('R', ctrl | shift) });
    add (metronome, "Metronome", "Transport and recording", { key ('K', ctrl) });
    add (tempoUp, "Tempo +1", "Transport and recording", { key (K::upKey, ctrl) });
    add (tempoDown, "Tempo -1", "Transport and recording", { key (K::downKey, ctrl) });
    add (tempoUpFine, "Tempo +0.1", "Transport and recording", { key (K::upKey, ctrl | shift) });
    add (tempoDownFine, "Tempo -0.1", "Transport and recording", { key (K::downKey, ctrl | shift) });
    add (perform, "Performance mode", "Transport and recording", { key ('P', ctrl | shift) });

    add (pageRack, "Rack page", "Pages and drawers", { key ('1', ctrl) });
    add (pageSeq, "Seq page", "Pages and drawers", { key ('2', ctrl) });
    add (pageEditor, "Editor of the selected instrument", "Pages and drawers", { key ('3', ctrl) });
    add (playDrawer, "Play drawer", "Pages and drawers", { key ('P', ctrl) });
    add (midiLearn, "MIDI learn", "Pages and drawers", { key ('M', ctrl) });

    add (trackPrev, "Previous instrument", "Instruments", { key (K::upKey) });
    add (trackNext, "Next instrument", "Instruments", { key (K::downKey) });
    add (addDevice, "Add an instrument", "Instruments", { key ('='), key ('+', shift) });
    add (presetBrowser, "Preset browser", "Instruments", { key ('B', ctrl) });
    add (presetPrev, "Previous preset", "Instruments", { key ('[', ctrl) });
    add (presetNext, "Next preset", "Instruments", { key (']', ctrl) });
    add (barPrev, "Previous bar", "Instruments", { key (K::leftKey) });
    add (barNext, "Next bar", "Instruments", { key (K::rightKey) });
    add (undo, "Undo", "Instruments", { key ('Z', ctrl) });
    add (redo, "Redo", "Instruments", { key ('Z', ctrl | shift) });
    add (redoAlt, "Redo (second key)", "Instruments", { key ('Y', ctrl) });

    for (int p = 0; p < 8; ++p)
        add (pattern0 + p, "Pattern " + juce::String::charToString ("ABCDEFGH"[p]), "Patterns", { key ('1' + p, alt) });

    add (rollSelectAll, "Select all", "Piano roll", { key ('A', ctrl) });
    add (rollInvertSelection, "Invert the selection", "Piano roll", { key ('A', ctrl | shift) });
    add (rollCopy, "Copy", "Piano roll", { key ('C', ctrl) });
    add (rollCut, "Cut", "Piano roll", { key ('X', ctrl) });
    add (rollPaste, "Paste", "Piano roll", { key ('V', ctrl) });
    add (rollDuplicate, "Duplicate", "Piano roll", { key ('D', ctrl) });
    add (rollDelete, "Delete", "Piano roll", { key (K::deleteKey) });
    add (rollDeleteAlt, "Delete (second key)", "Piano roll", { key (K::backspaceKey) });
    add (rollQuantize, "Quantize", "Piano roll", { key ('Q') });
    add (rollMute, "Mute / unmute", "Piano roll", { key ('0') });
    add (rollLock, "Lock / unlock", "Piano roll", { key ('L', ctrl) });
    add (rollTool, "Select / Draw tool", "Piano roll", { key ('B') });
    add (rollSnap, "Snap to grid", "Piano roll", { key ('4', ctrl) });
    add (rollZoomIn, "Zoom in", "Piano roll", { key ('=', ctrl) });
    add (rollZoomOut, "Zoom out", "Piano roll", { key ('-', ctrl) });
    add (rollRowsIn, "Taller rows", "Piano roll", { key ('=', ctrl | shift), key ('+', ctrl | shift) });
    add (rollRowsOut, "More rows", "Piano roll", { key ('-', ctrl | shift), key ('_', ctrl | shift) });
    add (rollFit, "The whole pattern", "Piano roll", { key ('0', ctrl) });
    add (rollFitNotes, "Zoom to the notes / the selection", "Piano roll", { key ('0', ctrl | shift), key (')', ctrl | shift) });
    add (rollTransposeUp, "Transpose +1 (a scale step under scale lock)", "Piano roll", { key (K::pageUpKey) });
    add (rollTransposeDown, "Transpose -1", "Piano roll", { key (K::pageDownKey) });
    add (rollOctaveUp, "Transpose +12", "Piano roll", { key (K::pageUpKey, shift) });
    add (rollOctaveDown, "Transpose -12", "Piano roll", { key (K::pageDownKey, shift) });

    const char* notes[16] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C (up)", "C# (up)", "D (up)", "D# (up)" };
    const char keys[16] = { 'A', 'W', 'S', 'E', 'D', 'F', 'T', 'G', 'Y', 'H', 'U', 'J', 'K', 'O', 'L', 'P' };
    for (int i = 0; i < 16; ++i) add (note0 + i, juce::String ("Play ") + notes[i], "Computer keyboard notes", { key (keys[i]) });
    add (octaveDown, "Octave down", "Computer keyboard notes", { key ('Z') });
    add (octaveUp, "Octave up", "Computer keyboard notes", { key ('X') });
    load();
}

void Shortcuts::add (juce::CommandID id, const juce::String& name, const juce::String& category, std::initializer_list<juce::KeyPress> keys)
{
    juce::ApplicationCommandInfo info (id);
    info.shortName = name;
    info.description = name;
    info.categoryName = category;
    for (auto& k : keys) info.addDefaultKeypress (k.getKeyCode(), k.getModifiers());
    commands.registerCommand (info);
    for (auto& k : keys) commands.getKeyMappings()->addKeyPress (id, k);
}

bool Shortcuts::is (juce::CommandID id, const juce::KeyPress& k) const
{
    for (auto& kp : commands.getKeyMappings()->getKeyPressesAssignedToCommand (id))
    {
        if (kp.getModifiers().withoutMouseButtons() != k.getModifiers().withoutMouseButtons()) continue;
        const int a = kp.getKeyCode(), b = k.getKeyCode();
        if (a == b || (a >= 'A' && a <= 'Z' && b == a + 32) || (b >= 'A' && b <= 'Z' && a == b + 32)) return true;
        if (k.getTextCharacter() != 0 && a == (int) k.getTextCharacter()) return true;
    }
    return false;
}

juce::CommandID Shortcuts::find (const juce::KeyPress& k, const juce::StringArray& categories) const
{
    for (auto& cat : categories)
        for (auto id : commands.getCommandsInCategory (cat))
            if (is (id, k)) return id;
    return 0;
}

int Shortcuts::keyCode (juce::CommandID id) const
{
    const auto keys = commands.getKeyMappings()->getKeyPressesAssignedToCommand (id);
    return keys.isEmpty() ? 0 : keys[0].getKeyCode();
}

juce::String Shortcuts::describe (juce::CommandID id) const
{
    const auto keys = commands.getKeyMappings()->getKeyPressesAssignedToCommand (id);
    return keys.isEmpty() ? juce::String() : keys[0].getTextDescriptionWithIcons();
}

juce::File Shortcuts::file()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Pastel Minibox").getChildFile ("shortcuts.xml");
}

void Shortcuts::save() const
{
    if (auto xml = commands.getKeyMappings()->createXml (true))
    {
        file().getParentDirectory().createDirectory();
        xml->writeTo (file());
    }
}

void Shortcuts::load()
{
    if (auto xml = juce::parseXML (file())) commands.getKeyMappings()->restoreFromXml (*xml);
}
} // namespace mb::ui
