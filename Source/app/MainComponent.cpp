#include "MainComponent.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include "ui/PresetBrowser.h"
#include "ui/Shortcuts.h"
#include "ui/FxParamBinding.h"
#include "model/Serializer.h"
#include "model/MidiFiles.h"
#include "model/DawProject.h"
#include "devices/SampleSlot.h"

namespace mb
{
using namespace ui;

MainComponent::MainComponent (Engine& e)
    : engine (e), ctx (e, true), topBar (ctx), rack (ctx), seq (ctx), editor (ctx), drawer (ctx), learnDrawer (ctx, map), masterFxDrawer (ctx)
{
    // the groovebox's master and performance effects (the plugin has none)
    ctx.masterFx = &masterFx;
    ctx.midiMap = &map;
    // FX keys: 8 notes from any controller hold the performance effects while the mode is on (MIDI thread)
    map.onFxKey = [this] (int i, bool down) { masterFx.setHeld (i, down); };
    ctx.setFxKeys = [this] (bool on)
    {
        if (! on) for (int i = 0; i < kPerformFx; ++i) masterFx.setHeld (i, false);
        map.fxKeysLearning = false;
        map.setFxKeys (on);
    };
    masterFx.sync();
    engine.setMasterInsert (&masterFx);
    ctx.onMasterFx = [this] (bool pads, int index)
    {
        const bool same = masterFxOpen() && masterFxDrawer.showingPads() == pads && (! pads || masterFxDrawer.selected() == index);
        showMasterFx (! same, pads, index);
    };
    ctx.onTrackFx = [this] (int track)
    {
        const bool same = masterFxOpen() && masterFxDrawer.shownTrack() == track;
        if (same) { showMasterFx (false); return; }
        showMasterFx (true);
        masterFxDrawer.showTrack (track);
    };
    juce::LookAndFeel::setDefaultLookAndFeel (&laf);
    setLookAndFeel (&laf);
    setOpaque (true);
    setWantsKeyboardFocus (true);
    addAndMakeVisible (content);
    content.addAndMakeVisible (topBar);
    content.addChildComponent (rack);
    content.addChildComponent (seq);
    content.addChildComponent (editor);
    content.addChildComponent (drawer);
    drawer.onClose = [this] { showDrawer (false); };
    content.addChildComponent (learnDrawer);
    learnDrawer.onClose = [this] { showLearn (false); };
    content.addChildComponent (masterFxDrawer);
    masterFxDrawer.onClose = [this] { showMasterFx (false); };
    // MIDI learn: the controls arm through these hooks; mapped messages reach their targets here
    auto& hooks = learnHooks();
    hooks.arm = [this] (const juce::String& t) { map.arm (learnDrawer.armTarget (t)); };
    hooks.remove = [this] (const juce::String& t)
    {
        map.removeTarget (t);
        const auto sel = learnDrawer.armTarget (t);
        if (sel != t && t.fromFirstOccurrenceOf (":", false, false).getIntValue() == ctx.focusTrack()) map.removeTarget (sel);
    };
    hooks.label = [this] (const juce::String& t) { return learnDrawer.labelFor (t); };
    hooks.isArmed = [this] (const juce::String& t) { return map.armedTarget().isNotEmpty() && (map.armedTarget() == t || map.armedTarget() == learnDrawer.armTarget (t)); };
    map.apply = [this] (const juce::String& t, const MidiControl& c, MidiMapping& mp) { midiControl (t, c, mp); };
    map.onChanged = [this]
    {
        if (map.revision != mapRevision) { mapRevision = map.revision; content.repaint(); }
        else learnDrawer.repaint();
    };
    // the map in use; on the first run, the shipped map of a connected controller
    if (! map.load (MidiMap::currentFile()))
        for (auto& f : MidiMap::factoryMaps())
        {
            const auto detect = juce::JSON::parse (f)["detect"].toString();
            bool found = false;
            for (auto& in : juce::MidiInput::getAvailableDevices()) found = found || (detect.isNotEmpty() && in.name.containsIgnoreCase (detect));
            if (found) { map.load (f); map.save (MidiMap::currentFile()); break; }
        }
    upgradeControllerMap (map);
    layoutContent();

    ctx.openEditor = [this] (int t)
    {
        if (engine.device (ctx.focusPattern(), t) == nullptr) return;
        engine.focusTrack = t;
        ctx.lockStep = -1;
        editor.showTrack (t);
        setView (TopBar::View::Editor);
    };
    ctx.closeEditor = [this] { ctx.lockStep = -1; setView (TopBar::View::Rack); };
    ctx.showDevicePicker = [this] (int t, juce::Component* anchor) { showDevicePicker (t, anchor); };
    ctx.showPresetBrowser = [this] (DeviceProcessor* d, juce::Component* anchor) { PresetBrowser::show (ctx, *d, anchor); };
    ctx.onDirtyChanged = [this] { if (onTitleChanged) onTitleChanged(); };
    ctx.onDevicesChanged = [this] { if (view == TopBar::View::Editor && ctx.focusDevice() == nullptr) setView (TopBar::View::Rack); };

    topBar.onView = [this] (TopBar::View v) { setView (v); };
    topBar.onNew = [this] { newProject(); };
    topBar.onOpen = [this] { openProject(); };
    topBar.onSave = [this] { saveProject (false); };
    topBar.onExport = [this] { exportWav(); };
    topBar.onMenu = [this] { showMenu(); };
    topBar.onPlayDrawer = [this] { showDrawer (! drawer.isVisible()); };
    topBar.onRename = [this]
    {
        askForText ("Project name", engine.project().name, [this] (juce::String s)
        {
            if (s.isEmpty()) return;
            engine.project().name = s;
            ctx.markDirty();
            if (onTitleChanged) onTitleChanged();
        });
    };
    setView (TopBar::View::Rack);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    if (autosaveOn) { runningMarker().deleteFile(); autosaveFile().deleteFile(); }   // a clean exit: nothing to recover
    learnHooks() = {};
    map.apply = nullptr; map.onChanged = nullptr;
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void MainComponent::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void MainComponent::layoutContent()
{
    using namespace geo;
    content.setSize (W, H);
    topBar.setBounds (0, 0, W, TOP);
    for (auto* c : std::initializer_list<juce::Component*> { &rack, &seq, &editor }) c->setBounds (0, TOP, W, H - TOP);
    drawer.setBounds (M, H - M - 300, W - 2 * M, 300);
    learnDrawer.setBounds (drawer.getBounds());
    // above the master strip, so the pads stay in reach (mockup 19_master_fx)
    masterFxDrawer.setBounds (M, H - M - 124 - GAP - 300, W - 2 * M, 300);
    content.repaint();
}

void MainComponent::resized()
{
    // the layout is 1000 high and as wide as the window's shape asks (16:10 → 1600, 16:9 → 1778, wider screens up
    // to kMaxW); narrower windows keep 1600 and letterbox
    if (getWidth() > 0 && getHeight() > 0)
    {
        const int w = juce::jlimit (geo::kMinW, geo::kMaxW, juce::roundToInt (geo::H * getWidth() / (double) getHeight()));
        if (w != geo::W) { geo::W = w; layoutContent(); }
    }
    const float s = std::min (getWidth() / (float) geo::W, getHeight() / (float) geo::H);
    const float ox = (getWidth() - geo::W * s) / 2.0f, oy = (getHeight() - geo::H * s) / 2.0f;
    content.setTransform (juce::AffineTransform::scale (s).translated (ox, oy));
}

void MainComponent::showPage (int page, int track)
{
    if (page == 2) { if (ctx.openEditor) ctx.openEditor (track); }
    else { engine.focusTrack = track; setView (page == 1 ? TopBar::View::Seq : TopBar::View::Rack); }
}

void MainComponent::upgradeControllerMap (MidiMap& m)
{
    // a short-lived KeyLab map sent the pads' notes 36–43 on channel 10 to the performance effects, but the KeyLab's
    // pads send the keys' channel: those mappings go (FX keys does the job for any controller)
    for (int i = (int) m.mappings.size(); --i >= 0;)
    {
        const auto& mp = m.mappings[(size_t) i];
        if (mp.target.startsWith ("perform:") && mp.kind == MidiMapping::Kind::Note && mp.channel == 10
            && mp.number == 36 + mp.target.getTrailingIntValue() && mp.label == "Pad " + juce::String (mp.target.getTrailingIntValue() + 1))
            m.remove (i);
    }
}

void MainComponent::showMasterFx (bool on, bool pads, int index)
{
    if (on && learnOpen()) showLearn (false);
    if (on && drawer.isVisible()) showDrawer (false);
    if (on) { masterFxDrawer.show (pads, index); masterFxDrawer.toFront (false); }
    masterFxDrawer.setVisible (on);
    content.repaint();
}

void MainComponent::showDrawer (bool on)
{
    if (on && learnOpen()) showLearn (false);
    if (on && masterFxOpen()) showMasterFx (false);
    drawer.setVisible (on);
    if (on) drawer.toFront (false);
    topBar.playOpen = on;
    topBar.repaint();
}

void MainComponent::showLearn (bool on)
{
    if (on && drawer.isVisible()) showDrawer (false);
    if (on && masterFxOpen()) showMasterFx (false);
    learnDrawer.setVisible (on);
    if (on) learnDrawer.toFront (false);
    learnHooks().active = on;
    map.setLearning (on);
    content.repaint();
}

void MainComponent::midiControl (const juce::String& target, const MidiControl& c, MidiMapping& mp)
{
    if (target.startsWith ("action:"))
    {
        if (c.type == MidiControl::Type::Press) runAction (target.fromFirstOccurrenceOf (":", false, false));
        return;
    }
    // while a performance effect is held, the knobs mapped to macros play its controls instead (in the order the
    // master effects drawer shows them, then its mix); letting go hands them back to the macros
    const bool knob = c.type == MidiControl::Type::Set || c.type == MidiControl::Type::Delta;
    if (knob && target.startsWith ("macro:"))
        if (const int pad = masterFx.heldPad(); pad >= 0)
        {
            const int k = target.fromLastOccurrenceOf (":", false, false).getIntValue();
            const auto* d = efx::defForId (efx::idForName (engine.project().performFx[(size_t) pad].effect));
            const int n = d != nullptr ? d->numParams : 0;
            if (k > n) return;
            FxParamBinding b (ctx, true, pad, k < n ? k : -1);
            auto direct = mp;
            direct.pickup = false;   // a held effect is played, not set up: the knob takes over at once
            applyLearnControl (b, c, direct);
            return;
        }
    if (auto b = learnBinding (ctx, target)) applyLearnControl (*b, c, mp);
}

void MainComponent::runAction (const juce::String& id)
{
    if (id == "tostart") engine.toStart();
    else if (id == "track-prev" || id == "track-next") selectTrack (id.endsWith ("prev") ? -1 : 1);
    else if (id == "bar-prev" || id == "bar-next") moveBar (id.endsWith ("prev") ? -1 : 1);
    else if (id == "preset-prev" || id == "preset-next") { if (auto* d = ctx.focusDevice()) presetActions::step (ctx, *d, id.endsWith ("prev") ? -1 : 1); }
    else if (id == "undo" || id == "redo") { if (id == "undo" ? ctx.undo() : ctx.redo()) { seq.refresh(); rack.refresh(); } }
    else if (id == "editor") { if (ctx.openEditor) ctx.openEditor (ctx.focusTrack()); }
    else if (id == "step-rest") engine.stepRest();
    else if (id == "step-back") engine.stepBack();
    else if (id == "playdrawer") showDrawer (! drawer.isVisible());
    else if (id == "save") saveProject (false);
    else topBar.trigger (id);
    topBar.repaint();
}

void MainComponent::addDevice()
{
    // + / =: an instrument for the first empty track (the picker opens over its rack strip)
    for (int t = 0; t < kTracks; ++t)
        if (engine.device (ctx.focusPattern(), t) == nullptr)
        {
            if (view != TopBar::View::Rack) setView (TopBar::View::Rack);
            showDevicePicker (t, rack.strip (t));
            return;
        }
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "No empty track",
                                            "All four tracks of this pattern have an instrument. Change one with its name menu.");
}

void MainComponent::showSettings()
{
    SettingsPanel::show (ctx, map, createAudioSettings);
}

void MainComponent::selectTrack (int dir)
{
    // the previous / next instrument (what the keyboard and MIDI play)
    for (int t = ctx.focusTrack() + dir; t >= 0 && t < kTracks; t += dir)
        if (engine.device (ctx.focusPattern(), t) != nullptr)
        {
            if (view == TopBar::View::Editor && ctx.openEditor) ctx.openEditor (t);
            else engine.focusTrack = t;
            break;
        }
}

void MainComponent::moveBar (int dir)
{
    if (engine.isRecording() && engine.project().recordMode == 1) { engine.stepMove (dir); return; }   // the step-record cursor
    // otherwise: the previous / next bar of the sequencer view (Seq and the editors' step strip)
    const int bars = engine.project().patterns[(size_t) ctx.focusPattern()].lengthBars;
    ctx.viewBar = juce::jlimit (0, std::max (0, bars - 1), ctx.viewBar + dir);
    ctx.followPlayhead = false;
    seq.refresh(); editor.refresh();
}

void MainComponent::setView (TopBar::View v)
{
    view = v;
    topBar.view = v;
    rack.setVisible (v == TopBar::View::Rack);
    seq.setVisible (v == TopBar::View::Seq);
    editor.setVisible (v == TopBar::View::Editor);
    topBar.repaint();
}

void MainComponent::timerCallback()
{
    topBar.projectName = engine.project().name;
    topBar.refresh();
    if (rack.isVisible()) rack.refresh();
    if (seq.isVisible()) seq.refresh();
    if (editor.isVisible()) editor.refresh();
    if (drawer.isVisible()) drawer.refresh();
    masterFx.sync();   // a loaded or new project: its effects (only what changed is rebuilt)
    if (masterFxDrawer.isVisible()) masterFxDrawer.refresh();
    map.dispatch();
    if (engine.applyRecorded()) ctx.markDirty();   // live / step recording into piano-roll clips
    engine.recordTouched();                        // knobs still being turned write the steps the playhead reaches
    // keys reach the app after a dialog closes (New's "Save changes?", file choosers): nothing focused → take it back
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if ((focused == nullptr || focused == getTopLevelComponent()) && isShowing() && getPeer() != nullptr && getPeer()->isFocused()
        && juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0)
        grabKeyboardFocus();
    if (++retireCounter >= 30) { retireCounter = 0; collectRetiredSamples(); }
    if (autosaveOn && ++autosaveCounter >= 30 * 60)
    {
        autosaveCounter = 0;
        if (ctx.dirty) serializer::save (engine, autosaveFile());
    }
}

juce::File MainComponent::autosaveFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Pastel Minibox").getChildFile ("autosave.minibox");
}

juce::File MainComponent::runningMarker()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Pastel Minibox").getChildFile ("session.running");
}

void MainComponent::enableAutosave()
{
    autosaveOn = true;
    const auto marker = runningMarker(), autosave = autosaveFile();
    marker.getParentDirectory().createDirectory();
    const bool crashed = marker.existsAsFile() && autosave.existsAsFile();
    marker.replaceWithText (juce::Time::getCurrentTime().toISO8601 (true));
    if (! crashed) { autosave.deleteFile(); return; }
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Recover unsaved work?",
                                        "Minibox did not close properly last time. An autosave from "
                                            + autosave.getLastModificationTime().toString (true, true) + " can be opened.",
                                        "Recover", "Discard", this,
                                        juce::ModalCallbackFunction::create ([this] (int r)
                                        {
                                            const auto f = autosaveFile();
                                            if (r == 1 && serializer::load (engine, f).ok)
                                            {
                                                currentFile = juce::File();   // Save asks where (the autosave is not the project file)
                                                for (int p = 0; p < kPatterns; ++p) for (int t = 0; t < kTracks; ++t) if (auto* d = engine.device (p, t)) ctx.setReference (*d);
                                                setView (TopBar::View::Rack);
                                                ctx.dirty = false;
                                                ctx.markDirty();
                                            }
                                            else f.deleteFile();
                                        }));
}

void MainComponent::markClean()
{
    if (autosaveOn) autosaveFile().deleteFile();
    ctx.dirty = false;
    if (onTitleChanged) onTitleChanged();
}

juce::String MainComponent::windowTitle() const
{
    return "Pastel Minibox - " + engine.project().name + (ctx.dirty ? " *" : "");
}

// ------------------------------------------------------------------------------------------- devices
void MainComponent::showDevicePicker (int track, juce::Component* anchor)
{
    lastPickerTrack = track;
    juce::PopupMenu m;
    auto* cur = engine.device (ctx.focusPattern(), track);
    const DeviceType order[] = { DeviceType::Mono, DeviceType::Poly, DeviceType::Acid, DeviceType::FM, DeviceType::Sampler, DeviceType::Drums, DeviceType::DrumSampler };
    for (auto t : order) m.addItem (1 + (int) t, deviceTypeName (t), true, cur != nullptr && cur->type() == t);
    m.addSeparator();
    m.addItem (100, "Empty", true, cur == nullptr);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor), [this, track] (int id)
    {
        if (id <= 0) return;
        std::unique_ptr<DeviceProcessor> d;
        if (id != 100)
        {
            d = createDevice ((DeviceType) (id - 1));
            presets::loadDefault (*d);
            ctx.setReference (*d);
        }
        auto* old = engine.device (ctx.focusPattern(), track);
        if (old != nullptr) { ctx.reference.erase (old); ctx.comparing.erase (old); }
        auto freed = engine.replaceDevice (ctx.focusPattern(), track, std::move (d));
        freed.reset();   // released here, outside the audio lock
        if (engine.device (ctx.focusPattern(), track) != nullptr) engine.focusTrack = track;   // the new instrument is selected
        ctx.markDirty();
        if (view == TopBar::View::Editor && engine.device (ctx.focusPattern(), track) == nullptr) setView (TopBar::View::Rack);
    });
}

void MainComponent::showMenu()
{
    auto& p = engine.project();
    juce::PopupMenu m, sw, rec, click;
    m.addItem (3, "Settings (keyboard, MIDI controller, devices)...\tCtrl+,");
    m.addItem (1, "Audio and MIDI devices...");
    m.addItem (2, "MIDI learn (Ctrl+M)", true, learnOpen());
    m.addSeparator();
    sw.addItem (10, "Bar", true, p.switchQuantize == 0);
    sw.addItem (11, "Pattern end", true, p.switchQuantize == 1);
    sw.addItem (12, "Immediate", true, p.switchQuantize == 2);
    m.addSubMenu (p.perform ? "Pattern switch (Performance mode: at the pattern's end)" : "Pattern switch", sw);
    m.addItem (13, "Performance mode\tCtrl+Shift+P", true, p.perform);
    rec.addItem (20, "Overdub (loop and layer)", true, p.overdub);
    rec.addItem (21, "Extend (the pattern grows while recording)", true, ! p.overdub);
    rec.addItem (26, "Live (real time)", true, p.recordMode == 0);
    rec.addItem (27, "Step (notes and chords at the cursor)", true, p.recordMode == 1);
    rec.addSeparator();
    for (int i = 0; i < 4; ++i) rec.addItem (22 + i, "Quantize " + juce::String (100 - i * 25) + "%", true, std::abs (p.recordQuantize - (1.0f - i * 0.25f)) < 0.01f);
    m.addSubMenu ("Recording", rec);
    for (int i = 0; i < 4; ++i) click.addItem (30 + i, juce::String (25 * (i + 1)) + "%", true, std::abs (p.clickLevel - 0.25f * (i + 1)) < 0.01f);
    m.addSubMenu ("Click level", click);
    m.addItem (50, "Limiter look-ahead", true, p.master.lookAhead);
    m.addItem (51, "MIDI channels 1-8 play tracks 1-8 (otherwise every channel plays the selected track)", true, p.channelPerTrack);
    m.addSeparator();
    m.addItem (60, "Save rack preset...");
    m.addItem (61, "Load rack preset into pattern " + juce::String::charToString ("ABCDEFGH"[ctx.focusPattern()]) + "...");
    m.addSeparator();
    m.addItem (70, "Save project as...");
    m.addSeparator();
    m.addItem (80, "Import MIDI file into the selected track...", ctx.focusDevice() != nullptr);
    m.addItem (83, "Export the song: WAV, FLAC or OGG (patterns A, B, C … in order)...");
    m.addItem (81, "Export pattern " + juce::String::charToString ("ABCDEFGH"[ctx.focusPattern()]) + " as a MIDI file...");
    m.addItem (82, "Export the project as DAWproject...");
    m.showMenuAsync (menuAt (topBar, topBar.zoneRect ("menu")), [this] (int id)
    {
        auto& pr = engine.project();
        if (id == 1 && showAudioSettings) showAudioSettings();
        if (id == 3) showSettings();
        if (id == 2) showLearn (! learnOpen());
        if (id >= 10 && id <= 12) pr.switchQuantize = id - 10;
        if (id == 13) pr.perform = ! pr.perform;
        if (id == 20 || id == 21) pr.overdub = id == 20;
        if (id >= 22 && id <= 25) pr.recordQuantize = 1.0f - (id - 22) * 0.25f;
        if (id == 26 || id == 27) pr.recordMode = id - 26;
        if (id == 51) pr.channelPerTrack = ! pr.channelPerTrack;
        if (id >= 30 && id <= 33) pr.clickLevel = 0.25f * (id - 29);
        if (id == 50) engine.setLookAhead (! pr.master.lookAhead);   // rebuilds only the master limiter
        if (id == 60 || id == 61)
        {
            chooser = std::make_unique<juce::FileChooser> (id == 60 ? "Save rack preset" : "Load rack preset", juce::File(), "*.mbrack");
            const bool save = id == 60;
            const int flags = save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                                   : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
            chooser->launchAsync (flags | juce::FileBrowserComponent::canSelectFiles, [this, save] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                auto r = save ? serializer::saveRack (engine, ctx.focusPattern(), f.withFileExtension (".mbrack"))
                              : serializer::loadRack (engine, ctx.focusPattern(), f);
                if (! r.ok) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Rack preset", r.error);
                else if (! save) ctx.markDirty();
            });
        }
        if (id == 70) saveProject (true);
        if (id == 80) importMidi();
        if (id == 81) exportMidi();
        if (id == 83) exportSong();
        if (id == 82) exportDawProject();
        if (id >= 10 && id < 60) ctx.markDirty();
    });
}

// ------------------------------------------------------------------------------------------- files
void MainComponent::askToSaveThen (std::function<void()> action)
{
    if (! ctx.dirty) { action(); return; }
    juce::AlertWindow::showYesNoCancelBox (juce::MessageBoxIconType::QuestionIcon, "Save changes?",
                                           "\"" + engine.project().name + "\" has unsaved changes.", "Save", "Don't Save", "Cancel", nullptr,
                                           juce::ModalCallbackFunction::create ([this, action] (int r)
                                           {
                                               if (r == 1) saveProject (false, action);
                                               else if (r == 2) action();
                                           }));
}

void MainComponent::newProject()
{
    askToSaveThen ([this]
    {
        engine.newProject();
        currentFile = juce::File();
        ctx.reference.clear(); ctx.comparing.clear();
        ctx.viewBar = 0; ctx.lockStep = -1;
        setView (TopBar::View::Rack);
        markClean();
        grabKeyboardFocus();
    });
}

void MainComponent::openProject()
{
    askToSaveThen ([this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open project", currentFile.getParentDirectory(), "*.minibox");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult() != juce::File()) openFile (fc.getResult());
        });
    });
}

void MainComponent::openFile (const juce::File& f)
{
    auto r = serializer::load (engine, f);
    if (! r.ok) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Can't open project", r.error); return; }
    currentFile = f;
    ctx.reference.clear(); ctx.comparing.clear();
    ctx.viewBar = 0; ctx.lockStep = -1;
    for (int p = 0; p < kPatterns; ++p) for (int t = 0; t < kTracks; ++t) if (auto* d = engine.device (p, t)) ctx.setReference (*d);
    setView (TopBar::View::Rack);
    markClean();
    grabKeyboardFocus();
    if (! r.missingSamples.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Missing samples",
                                                "These samples could not be found:\n" + r.missingSamples.joinIntoString ("\n"));
}

void MainComponent::saveProject (bool saveAs, std::function<void()> after)
{
    auto write = [this, after] (juce::File f)
    {
        f = f.withFileExtension (".minibox");
        auto r = serializer::save (engine, f);
        if (! r.ok) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Can't save project", r.error); return; }
        currentFile = f;
        markClean();
        if (after) after();
    };
    if (! saveAs && currentFile.existsAsFile()) { write (currentFile); return; }
    auto start = currentFile.existsAsFile() ? currentFile
                                            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (juce::File::createLegalFileName (engine.project().name) + ".minibox");
    chooser = std::make_unique<juce::FileChooser> ("Save project", start, "*.minibox");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [write] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) write (fc.getResult()); });
}

void MainComponent::exportMidi()
{
    const int pat = ctx.focusPattern();
    const auto name = juce::File::createLegalFileName (engine.project().name + " - Pattern " + juce::String::charToString ("ABCDEFGH"[pat])) + ".mid";
    chooser = std::make_unique<juce::FileChooser> ("Export pattern as MIDI", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name), "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, pat] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto r = midifile::exportPattern (engine, pat, f.withFileExtension (".mid"));
        if (! r.ok) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI export", r.error);
    });
}

void MainComponent::exportDawProject()
{
    const auto name = juce::File::createLegalFileName (engine.project().name) + ".dawproject";
    auto start = currentFile.existsAsFile() ? currentFile.withFileExtension (".dawproject")
                                            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name);
    chooser = std::make_unique<juce::FileChooser> ("Export the project as DAWproject", start, "*.dawproject");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto r = dawproject::exportProject (engine, f.withFileExtension (".dawproject"));
        if (! r.ok) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "DAWproject export", r.error); return; }
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "DAWproject export",
                                                juce::String (r.tracks) + " tracks with the Minibox plugin, " + juce::String (r.patterns)
                                                    + " patterns on the timeline and as scenes:\n" + f.withFileExtension (".dawproject").getFullPathName());
    });
}

void MainComponent::importMidi()
{
    chooser = std::make_unique<juce::FileChooser> ("Import a MIDI file into the selected track", juce::File(), "*.mid;*.midi;*.smf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        const int pat = ctx.focusPattern(), trk = ctx.focusTrack();
        auto run = [this, f, pat, trk] (int fileTrack)
        {
            ctx.snapshot (pat);
            const auto r = midifile::importTrack (engine, pat, trk, f, fileTrack);
            if (! r.ok) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI import", r.error); return; }
            ctx.markDirty();
            setView (TopBar::View::Seq);
            if (r.dropped > 0)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "MIDI import",
                                                        juce::String (r.notes) + " notes imported; " + juce::String (r.dropped) + " were beyond the pattern's longest length or had no drum part.");
        };
        const auto tracks = midifile::tracksWithNotes (f);
        if (tracks.size() <= 1) { run (tracks.empty() ? -1 : tracks.front().index); return; }
        // several tracks: pick one (or all of them merged)
        juce::PopupMenu m;
        m.addSectionHeader ("Import which track?");
        for (auto& t : tracks) m.addItem (t.index + 1, (t.name.isNotEmpty() ? t.name : "Track " + juce::String (t.index + 1)) + "  (" + juce::String (t.notes) + " notes)");
        m.addSeparator();
        m.addItem (1000, "All tracks, merged");
        m.showMenuAsync (menuAt (topBar, topBar.zoneRect ("menu")), [run] (int r) { if (r == 1000) run (-1); else if (r > 0) run (r - 1); });
    });
}

void MainComponent::exportSong()
{
    auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                     .getChildFile (juce::File::createLegalFileName (engine.project().name + " - Song") + ".wav");
    chooser = std::make_unique<juce::FileChooser> ("Export the song (WAV, FLAC or OGG)", start, "*.wav;*.flac;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (! f.hasFileExtension ("wav;flac;ogg")) f = f.withFileExtension (".wav");   // WAV, FLAC or OGG, as named
        auto r = serializer::exportSongWav (engine, f);
        if (r.ok) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Exported", f.getFullPathName());
        else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export failed", r.error);
    });
}

void MainComponent::exportWav()
{
    const auto pat = juce::String::charToString ("ABCDEFGH"[engine.activePattern()]);
    auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                     .getChildFile (juce::File::createLegalFileName (engine.project().name + " - Pattern " + pat) + ".wav");
    chooser = std::make_unique<juce::FileChooser> ("Export pattern " + pat + " (WAV, FLAC or OGG)", start, "*.wav;*.flac;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (! f.hasFileExtension ("wav;flac;ogg")) f = f.withFileExtension (".wav");   // WAV, FLAC or OGG, as named
        auto r = serializer::exportWav (engine, f, 2);
        if (r.ok) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Exported", f.getFullPathName());
        else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export failed", r.error);
    });
}

// ------------------------------------------------------------------------------------------- keyboard
bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    // docs/keyboard.md: focus (Tab, arrows, Enter, Esc), then the shortcuts (Settings ▸ Keyboard), the piano roll's
    // and the note keys. Undo / redo come first so they work whatever has focus.
    auto& sc = shortcuts();
    const int code = k.getKeyCode();
    if (sc.is (cmd::undo, k) || sc.is (cmd::redo, k) || sc.is (cmd::redoAlt, k))
    {
        if (sc.is (cmd::undo, k) ? ctx.undo() : ctx.redo()) { seq.refresh(); rack.refresh(); }
        return true;
    }
    if (Feedback::handleKey (k, content)) return true;
    // the piano roll's editing keys (Delete, Ctrl+A/C/X/V/D, arrows nudge, Q, 0, B, zoom …)
    if (view == TopBar::View::Seq && seq.keyCommand (k)) return true;
    const int page = view == TopBar::View::Rack ? 0 : (view == TopBar::View::Seq ? 1 : 2);
    if (const auto id = sc.find (k, Shortcuts::categoriesFor (page)); id != 0) { runCommand (id); return true; }
    if (code == juce::KeyPress::returnKey && view == TopBar::View::Rack) { if (ctx.openEditor) ctx.openEditor (ctx.focusTrack()); return true; }
    if (code == juce::KeyPress::escapeKey)
    {
        if (learnOpen()) showLearn (false);
        else if (drawer.isVisible()) showDrawer (false);
        else if (masterFxOpen()) showMasterFx (false);
        else if (ctx.lockStep >= 0) ctx.lockStep = -1;
        else if (view == TopBar::View::Editor) setView (TopBar::View::Rack);
        return true;
    }
    if (sc.is (cmd::octaveDown, k)) { keyboardOctave = std::max (0, keyboardOctave - 1); return true; }
    if (sc.is (cmd::octaveUp, k)) { keyboardOctave = std::min (8, keyboardOctave + 1); return true; }
    for (int i = 0; i < 16; ++i) if (sc.is (cmd::note0 + i, k)) return true;   // played in keyStateChanged
    return false;
}

void MainComponent::runCommand (juce::CommandID id)
{
    auto& proj = engine.project();
    auto tempo = [&] (double inc) { proj.bpm = juce::jlimit (30.0, 300.0, std::round ((proj.bpm + inc) * 10.0) / 10.0); ctx.markDirty(); topBar.repaint(); };
    if (id >= cmd::pattern0 && id <= cmd::pattern7) { topBar.trigger ("pat" + juce::String ((int) (id - cmd::pattern0))); return; }
    switch (id)
    {
        case cmd::newProject: this->newProject(); break;
        case cmd::openProject: this->openProject(); break;
        case cmd::saveProject: this->saveProject (false); break;
        case cmd::saveProjectAs: this->saveProject (true); break;
        case cmd::exportWav: this->exportWav(); break;
        case cmd::exportMidi: this->exportMidi(); break;
        case cmd::exportSong: this->exportSong(); break;
        case cmd::exportDawProject: this->exportDawProject(); break;
        case cmd::recordSettings: topBar.showRecording(); break;
        case cmd::masterEffects: showMasterFx (! masterFxOpen()); break;
        case cmd::fxKeys: ctx.setFxKeys (! map.fxKeysOn.load()); break;
        case cmd::noteRepeat: engine.noteRepeat = ! engine.noteRepeat.load(); rack.refresh(); break;
        case cmd::importMidi: this->importMidi(); break;
        case cmd::settings: showSettings(); break;
        case cmd::playStop: if (engine.isPlaying()) engine.stop(); else engine.play(); break;
        case cmd::toStart: engine.toStart(); break;
        case cmd::recordLive: topBar.toggleRecord (0); break;
        case cmd::recordStep: topBar.toggleRecord (1); break;
        case cmd::metronome: proj.metronome = ! proj.metronome; ctx.markDirty(); topBar.repaint(); break;
        case cmd::tempoUp: tempo (1.0); break;
        case cmd::tempoDown: tempo (-1.0); break;
        case cmd::tempoUpFine: tempo (0.1); break;
        case cmd::tempoDownFine: tempo (-0.1); break;
        case cmd::perform: topBar.trigger ("perform"); break;
        case cmd::pageRack: setView (TopBar::View::Rack); break;
        case cmd::pageSeq: setView (TopBar::View::Seq); break;
        case cmd::pageEditor: if (ctx.openEditor) ctx.openEditor (ctx.focusTrack()); break;
        case cmd::playDrawer: showDrawer (! drawer.isVisible()); break;
        case cmd::midiLearn: showLearn (! learnOpen()); break;
        case cmd::trackPrev: case cmd::rackTrackPrev: case cmd::seqTrackPrev: case cmd::editorTrackPrev: selectTrack (-1); break;
        case cmd::trackNext: case cmd::rackTrackNext: case cmd::seqTrackNext: case cmd::editorTrackNext: selectTrack (1); break;
        case cmd::rackPresetPrev: case cmd::editorPresetPrev: if (auto* d = ctx.focusDevice()) { presetActions::step (ctx, *d, -1); rack.refresh(); editor.refresh(); } break;
        case cmd::rackPresetNext: case cmd::editorPresetNext: if (auto* d = ctx.focusDevice()) { presetActions::step (ctx, *d, 1); rack.refresh(); editor.refresh(); } break;
        case cmd::addDevice: this->addDevice(); break;
        case cmd::presetBrowser: if (auto* d = ctx.focusDevice()) if (ctx.showPresetBrowser) ctx.showPresetBrowser (d, &topBar); break;
        case cmd::presetPrev: if (auto* d = ctx.focusDevice()) presetActions::step (ctx, *d, -1); break;
        case cmd::presetNext: if (auto* d = ctx.focusDevice()) presetActions::step (ctx, *d, 1); break;
        case cmd::barPrev: case cmd::seqBarPrev: moveBar (-1); break;
        case cmd::barNext: case cmd::seqBarNext: moveBar (1); break;
        default: break;
    }
}

bool MainComponent::keyStateChanged (bool)
{
    // the note keys (Settings ▸ Keyboard ▸ Computer keyboard notes), C upwards; drums: the white keys play the 8 parts
    std::pair<int, int> keys[16];
    for (int i = 0; i < 16; ++i)
    {
        int c = shortcuts().keyCode (cmd::note0 + i);
        if (c >= 'a' && c <= 'z') c -= 32;
        keys[i] = { c, i };
    }
    if (juce::ModifierKeys::currentModifiers.isCommandDown()) return false;
    bool used = false;
    // performance effects: on while their keys (1–8) are held; while note repeat is on, 1–6 pick its rate instead
    for (int i = 0; i < kPerformFx; ++i)
    {
        const int key = shortcuts().keyCode (cmd::perform0 + i);
        if (key == 0) continue;
        const bool down = juce::KeyPress::isKeyCurrentlyDown (key) && ! juce::ModifierKeys::currentModifiers.isAltDown();
        if (down != padKeysDown[(size_t) i])
        {
            padKeysDown[(size_t) i] = down;
            if (down) padKeyIsRate[(size_t) i] = engine.noteRepeat.load() && i < Engine::kRepeatRates;
            if (padKeyIsRate[(size_t) i]) { if (down) { engine.repeatBeats = Engine::repeatRate (i); rack.refresh(); } }
            else masterFx.setHeld (i, down);
            used = true;
        }
    }
    auto* dev = ctx.focusDevice();
    const int track = ctx.focusTrack();
    for (auto [key, semi] : keys)
    {
        if (key == 0) continue;
        const bool down = juce::KeyPress::isKeyCurrentlyDown (key) || (key >= 'A' && key <= 'Z' && juce::KeyPress::isKeyCurrentlyDown (key + 32));
        auto it = keysDown.find (key);
        if (down && it == keysDown.end())
        {
            int note = (keyboardOctave + 1) * 12 + semi;
            if (dev != nullptr && dev->laneCount() > 0)
            {
                // drums: the white-note keys play the eight parts
                static const int whiteSemis[8] = { 0, 2, 4, 5, 7, 9, 11, 12 };
                const auto* pos = std::find (whiteSemis, whiteSemis + 8, semi);
                if (pos == whiteSemis + 8) continue;
                note = dev->laneNote ((int) (pos - whiteSemis));
            }
            keysDown[key] = note;
            engine.injectNote (track, note, 0.8f, true);
            ctx.lastNote[(size_t) track] = note;
            used = true;
        }
        else if (! down && it != keysDown.end())
        {
            engine.injectNote (track, it->second, 0.0f, false);
            keysDown.erase (it);
            used = true;
        }
    }
    return used;
}
} // namespace mb
