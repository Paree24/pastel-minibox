#include "SettingsWindow.h"

namespace mb::ui
{
namespace
{
enum Columns { cControl = 1, cMessage, cTarget, cMode, cLearn, cRemove };
juce::String lastWord (const juce::String& s) { return s.trim().fromLastOccurrenceOf (" ", false, false); }
const juce::Colour kPanel (0xff1B1D22);
} // namespace

// ------------------------------------------------------------------------------------------- MIDI controller
MidiMapEditor::MidiMapEditor (AppContext& c, MidiMap& m) : ctx (c), map (m)
{
    setWantsKeyboardFocus (true);
    auto& h = table.getHeader();
    h.addColumn ("Control", cControl, 150);
    h.addColumn ("Message", cMessage, 190);
    h.addColumn ("Target", cTarget, 250);
    h.addColumn ("Mode", cMode, 120);
    h.addColumn ("", cLearn, 70);
    h.addColumn ("", cRemove, 40);
    table.setModel (this);
    h.setColour (juce::TableHeaderComponent::backgroundColourId, col::control);
    h.setColour (juce::TableHeaderComponent::textColourId, col::quiet);
    h.setColour (juce::TableHeaderComponent::outlineColourId, col::divider);
    h.setColour (juce::TableHeaderComponent::highlightColourId, col::divider);
    table.setRowHeight (28);
    table.setColour (juce::ListBox::backgroundColourId, kPanel);
    table.setWantsKeyboardFocus (true);
    addAndMakeVisible (table);
    for (auto* b : { &add, &learn, &maps, &load, &save, &clear }) addAndMakeVisible (*b);
    add.onClick = [this] { addMapping(); };
    learn.onClick = [this] { learnRow (table.getSelectedRow()); };
    maps.onClick = [this] { showMaps(); };
    load.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load MIDI map", juce::File(), "*.mbmidi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) map.load (fc.getResult()); table.updateContent(); repaint(); });
    };
    save.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Save MIDI map", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                          .getChildFile (juce::File::createLegalFileName (map.name) + ".mbmidi"), "*.mbmidi");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == juce::File()) return;
                                  f = f.withFileExtension (".mbmidi");
                                  map.name = f.getFileNameWithoutExtension();
                                  map.save (f);
                                  map.changed();
                              });
    };
    clear.onClick = [this] { map.clear(); table.updateContent(); repaint(); };
    status.setColour (juce::Label::textColourId, col::quiet);
    status.setFont (font (12, Weight::Bold));
    addAndMakeVisible (status);
    startTimerHz (15);
}

MidiMapEditor::~MidiMapEditor()
{
    if (learningTarget.isNotEmpty()) { map.arm ({}); map.setLearning (wasLearning); }
}

void MidiMapEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (30);
    for (auto* b : { &maps, &load, &save, &clear }) { b->setBounds (top.removeFromLeft (110)); top.removeFromLeft (6); }
    auto bottom = r.removeFromBottom (32);
    add.setBounds (bottom.removeFromLeft (140)); bottom.removeFromLeft (6);
    learn.setBounds (bottom.removeFromLeft (180)); bottom.removeFromLeft (12);
    status.setBounds (bottom);
    r.removeFromTop (8); r.removeFromBottom (8);
    table.setBounds (r);
}

void MidiMapEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::surface);
    paint::text (g, "Map: " + map.name, (float) maps.getRight() + 400, 32, 12, Weight::Black, col::text);
}

int MidiMapEditor::getNumRows() { return (int) map.mappings.size(); }

void MidiMapEditor::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    g.fillAll (selected ? col::pattern.withAlpha (0.25f) : (row % 2 == 0 ? col::control.withAlpha (0.5f) : kPanel));
}

void MidiMapEditor::paintCell (juce::Graphics& g, int row, int column, int w, int h, bool)
{
    if (row >= (int) map.mappings.size()) return;
    const auto& mp = map.mappings[(size_t) row];
    const bool learningThis = learningTarget.isNotEmpty() && mp.target == learningTarget;
    juce::String text;
    auto c = col::text;
    switch (column)
    {
        case cControl: text = mp.label.isNotEmpty() ? mp.label : juce::String ("(unnamed)"); c = mp.label.isNotEmpty() ? col::text : col::faint; break;
        case cMessage: text = learningThis ? juce::String ("move a control...") : mp.messageName() + (mp.port.isNotEmpty() ? juce::String::fromUTF8 ("  \xc2\xb7  ") + lastWord (mp.port) : juce::String()); if (learningThis) c = col::pattern; break;
        case cTarget: text = learnTargetName (ctx, mp.target); break;
        case cMode: text = MidiMapping::modeName (mp.mode) + (mp.pickup ? ", pickup" : ""); c = col::quiet; break;
        case cLearn: text = learningThis ? "Listening" : "Learn"; c = learningThis ? col::pattern : col::quiet; break;
        case cRemove: text = juce::String::fromUTF8 ("\xc3\x97"); c = col::quiet; break;
        default: break;
    }
    g.setColour (c);
    g.setFont (font (12, column == cControl || column == cTarget ? Weight::Black : Weight::Bold));
    g.drawText (text, 8, 0, w - 12, h, column == cRemove || column == cLearn ? juce::Justification::centred : juce::Justification::centredLeft, true);
}

void MidiMapEditor::cellClicked (int row, int column, const juce::MouseEvent&)
{
    switch (column)
    {
        case cControl: rename (row); break;
        case cTarget: chooseTarget (row); break;
        case cMode: showMappingModeMenu (map, row, [this] { table.repaint(); }, &table, table.getCellPosition (cMode, row, true).toFloat()); break;
        case cLearn: learnRow (row); break;
        case cRemove: map.remove (row); table.updateContent(); break;
        default: break;
    }
}

void MidiMapEditor::deleteKeyPressed (int row) { map.remove (row); table.updateContent(); }
void MidiMapEditor::returnKeyPressed (int row) { chooseTarget (row); }

bool MidiMapEditor::keyPressed (const juce::KeyPress& k)
{
    // the table's rows by keyboard: Enter target, M mode, L learn, R rename, N add, Delete remove
    const int row = table.getSelectedRow();
    const auto ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (ch == 'm' && row >= 0) { showMappingModeMenu (map, row, [this] { table.repaint(); }, &table, table.getCellPosition (cMode, row, true).toFloat()); return true; }
    if (ch == 'l' && row >= 0) { learnRow (row); return true; }
    if (ch == 'r' && row >= 0) { rename (row); return true; }
    if (ch == 'n') { addMapping(); return true; }
    return false;
}

void MidiMapEditor::rename (int row)
{
    if (row < 0 || row >= (int) map.mappings.size()) return;
    askForText ("Control name", map.mappings[(size_t) row].label, [this, row] (juce::String s)
    {
        if (row < (int) map.mappings.size()) { map.mappings[(size_t) row].label = s; map.changed(); table.repaint(); }
    });
}

void MidiMapEditor::chooseTarget (int row)
{
    if (row < 0 || row >= (int) map.mappings.size()) return;
    auto m = learnTargetMenu (ctx, menuIds);
    m.showMenuAsync (menuAt (table, table.getCellPosition (cTarget, row, true).toFloat()), [this, row] (int r)
    {
        if (r <= 0 || r > (int) menuIds.size() || row >= (int) map.mappings.size()) return;
        // the same control drives the new target (another control on that target loses it)
        auto mp = map.mappings[(size_t) row];
        map.mappings.erase (map.mappings.begin() + row);
        mp.target = menuIds[(size_t) r - 1];
        if (mp.target.startsWith ("action:")) mp.mode = MidiMapping::Mode::Trigger;
        else if (mp.mode == MidiMapping::Mode::Trigger && mp.kind == MidiMapping::Kind::CC) mp.mode = MidiMapping::Mode::Absolute;
        map.add (mp);
        table.updateContent();
        repaint();
    });
}

void MidiMapEditor::learnRow (int row)
{
    if (row < 0 || row >= (int) map.mappings.size()) return;
    // the next message from the controller becomes this row's (its name and target stay)
    if (learningTarget.isEmpty()) wasLearning = map.learning();
    learningTarget = map.mappings[(size_t) row].target;
    learningBefore = map.mappings[(size_t) row].messageName() + map.mappings[(size_t) row].port;
    map.setLearning (true);
    map.arm (learningTarget);
    status.setText ("Move or press a control on your MIDI controller...", juce::dontSendNotification);
    table.repaint();
}

void MidiMapEditor::addMapping()
{
    // pick what it drives, then move a control
    auto m = learnTargetMenu (ctx, menuIds);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&add), [this] (int r)
    {
        if (r <= 0 || r > (int) menuIds.size()) return;
        if (learningTarget.isEmpty()) wasLearning = map.learning();
        learningTarget = menuIds[(size_t) r - 1];
        learningBefore = map.sourceFor (learningTarget);
        map.setLearning (true);
        map.arm (learningTarget);
        status.setText ("Move or press a control for " + learnTargetName (ctx, learningTarget) + "...", juce::dontSendNotification);
    });
}

void MidiMapEditor::showMaps()
{
    juce::PopupMenu m;
    const auto files = MidiMap::factoryMaps();
    m.addSectionHeader ("Factory maps");
    for (int i = 0; i < files.size(); ++i) m.addItem (i + 1, files[i].getFileNameWithoutExtension(), true, files[i].getFileNameWithoutExtension() == map.name);
    m.addSeparator();
    m.addItem (500, "New empty map");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&maps), [this, files] (int r)
    {
        if (r >= 1 && r <= files.size()) map.load (files[r - 1]);
        if (r == 500) map.clear();
        table.updateContent();
        repaint();
    });
}

void MidiMapEditor::timerCallback()
{
    // a learnt row: the message arrived, stop listening
    if (learningTarget.isNotEmpty())
    {
        juce::String now;
        for (auto& mp : map.mappings) if (mp.target == learningTarget) now = mp.messageName() + mp.port;
        if (now.isNotEmpty() && now != learningBefore)
        {
            map.arm ({});
            map.setLearning (wasLearning);
            status.setText ("Learnt: " + learnTargetName (ctx, learningTarget) + " is now " + map.sourceFor (learningTarget), juce::dontSendNotification);
            learningTarget = {};
            table.updateContent();
        }
    }
    else if (map.lastReceived.isNotEmpty() && map.learning()) status.setText ("Last received: " + map.lastReceived, juce::dontSendNotification);
    if (map.revision != shownRevision) { shownRevision = map.revision; table.updateContent(); repaint(); }
}

// ------------------------------------------------------------------------------------------- the window
SettingsPanel::SettingsPanel (AppContext& ctx, MidiMap& map, std::function<juce::Component*()> audioSettings)
{
    auto* keys = new juce::KeyMappingEditorComponent (shortcuts().mappings(), true);
    keys->setColours (col::control, col::text);
    shortcuts().mappings().addChangeListener (this);
    tabs.addTab ("Keyboard", col::surface, keys, true);
    tabs.addTab ("MIDI controller", col::surface, new MidiMapEditor (ctx, map), true);
    if (audioSettings) tabs.addTab ("Audio and MIDI devices", col::surface, audioSettings(), true);
    tabs.setTabBarDepth (34);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);
    setSize (900, 640);
}

SettingsPanel::~SettingsPanel() { shortcuts().mappings().removeChangeListener (this); }

void SettingsPanel::resized() { tabs.setBounds (getLocalBounds()); }
void SettingsPanel::paint (juce::Graphics& g) { g.fillAll (col::surface); }

void SettingsPanel::show (AppContext& ctx, MidiMap& map, std::function<juce::Component*()> audioSettings, int tab)
{
    auto* panel = new SettingsPanel (ctx, map, std::move (audioSettings));
    panel->tabs.setCurrentTabIndex (tab);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panel);
    o.dialogTitle = "Settings";
    o.dialogBackgroundColour = col::surface;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.escapeKeyTriggersCloseButton = true;
    o.launchAsync();
}
} // namespace mb::ui
