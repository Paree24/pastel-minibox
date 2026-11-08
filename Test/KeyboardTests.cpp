#include "Tests.h"
#include "app/MainComponent.h"
#include "seq/ScaleTranspose.h"

using namespace mb;
using namespace mb::ui;

namespace
{
/** Presses Tab until the focused control satisfies `want` (at most n presses). */
template <class Pred> bool tabTo (MainComponent& mc, Pred want, int n = 400)
{
    for (int i = 0; i < n; ++i)
    {
        mc.keyPressed (juce::KeyPress (juce::KeyPress::tabKey));
        mc.refreshAll();
        mc.createComponentSnapshot (mc.getLocalBounds());   // paint, as the window would
        if (Feedback::navFocus().fb != nullptr && want (Feedback::navFocus())) return true;
    }
    return false;
}
} // namespace

/** Everything reachable from the keyboard (docs/keyboard.md): Tab focus, Enter, knob keys, pages, bars, the
    piano roll's cursor. */
MB_TEST (keyboard_navigation)
{
    ui::shortcuts().mappings().resetToDefaultMappings();   // the defaults, whatever this machine's Settings say
    Engine e;
    e.prepare (48000.0, 512);
    e.setDevice (0, 1, createDevice (DeviceType::Acid));
    e.project().patterns[0].lengthBars = 2;
    e.focusTrack = 1;
    MainComponent mc (e);
    mc.setSize (1600, 1000);
    mc.refreshAll();
    mc.createComponentSnapshot (mc.getLocalBounds());
    auto key = [&] (int code, juce::ModifierKeys m = {})
    {
        const bool r = mc.keyPressed (juce::KeyPress (code, m, 0));
        mc.refreshAll();
        mc.createComponentSnapshot (mc.getLocalBounds());
        return r;
    };
    const juce::ModifierKeys cmd (juce::ModifierKeys::commandModifier);

    ctx.check (key (juce::KeyPress::tabKey) && Feedback::hasFocus(), "Tab focuses the first control");
    int count = 1;
    const auto first = Feedback::navFocus();
    for (; count < 600; ++count)
    {
        key (juce::KeyPress::tabKey);
        if (Feedback::navFocus().fb == first.fb && Feedback::navFocus().r == first.r) break;
    }
    ctx.check (count > 30, "Tab walks through every control of the page (" + juce::String (count) + ")");

    // Enter on the top bar's Play button opens the drawer
    const auto playR = mc.topBarView().zoneRect ("playdrawer").getCentre();
    const bool found = tabTo (mc, [playR] (const Feedback::Focus& f) { return f.r.contains (playR) && f.r.getWidth() < 40; });
    ctx.check (found && key (juce::KeyPress::returnKey) && mc.drawerOpen(), "Enter on the Play button opens the drawer");
    key (juce::KeyPress::escapeKey);
    key (juce::KeyPress::escapeKey);
    ctx.check (! Feedback::hasFocus() && ! mc.drawerOpen(), "Esc clears focus, then closes the drawer");

    // a knob: ↑ raises it, Delete resets it
    auto* dev = e.device (0, 1);
    const bool knob = tabTo (mc, [] (const Feedback::Focus& f) { return dynamic_cast<KnobView*> (&f.fb->getView()) != nullptr; });
    ctx.check (knob, "Tab reaches the knobs");
    if (knob)
    {
        auto& kv = static_cast<KnobView&> (Feedback::navFocus().fb->getView());
        const float before = kv.binding().getNorm();
        key (juce::KeyPress::upKey);
        ctx.check (kv.binding().getNorm() > before, "↑ turns the focused knob up");
        key (juce::KeyPress::deleteKey);
        ctx.check (std::abs (kv.binding().getNorm() - kv.binding().defaultNorm()) < 1e-4f, "Delete resets it");
    }
    juce::ignoreUnused (dev);
    key (juce::KeyPress::escapeKey);

    // pages and bars
    ctx.check (key ('2', cmd) && mc.onSeqPage(), "Ctrl+2 opens the Seq page");
    mc.context().viewBar = 0;
    key (juce::KeyPress::rightKey);
    ctx.check (mc.context().viewBar == 1, "→ goes to the next bar");
    key (juce::KeyPress::leftKey);
    ctx.check (mc.context().viewBar == 0, "← back to the previous bar");

    // the Steps view by keyboard: a cell of the step grid, Enter switches that note on
    const bool cell = tabTo (mc, [] (const Feedback::Focus& f) { return dynamic_cast<SeqView*> (&f.fb->getView()) != nullptr && f.r.contains (175.0f, 534.0f); });
    ctx.check (cell && key (juce::KeyPress::returnKey) && e.project().patterns[0].slots[1].track.steps[0].noteCount == 1, "Tab reaches a step's note cell, Enter switches it on");
    if (cell) { key (juce::KeyPress::returnKey); }
    ctx.check (e.project().patterns[0].slots[1].track.steps[0].noteCount == 0, "Enter again switches it off");
    // the Piano roll view: its switch, then the roll
    const bool sw = tabTo (mc, [] (const Feedback::Focus& f) { return dynamic_cast<SeqView*> (&f.fb->getView()) != nullptr && f.r.contains (280.0f, 379.0f); });
    ctx.check (sw && key (juce::KeyPress::returnKey), "Tab reaches the Piano roll view switch");
    // the piano roll by keyboard: focus it, move the cursor, Enter adds a note
    const bool roll = tabTo (mc, [] (const Feedback::Focus& f) { return dynamic_cast<PianoRoll*> (&f.fb->getView()) != nullptr; });
    ctx.check (roll, "Tab reaches the piano roll");
    if (roll)
    {
        key (juce::KeyPress::rightKey); key (juce::KeyPress::rightKey);
        key (juce::KeyPress::returnKey);
        int notes = 0;
        for (auto& st : e.project().patterns[0].slots[1].track.steps) notes += st.noteCount;
        ctx.check (notes == 1 && e.project().patterns[0].slots[1].track.steps[2].noteCount == 1, "arrows move the cursor, Enter adds a note there");
        key (juce::KeyPress::returnKey);
        ctx.check (e.project().patterns[0].slots[1].track.steps[2].noteCount == 0, "Enter again removes it");
    }
    key (juce::KeyPress::escapeKey);
    Feedback::clearFocus();
}

/** + / = on a new project (no instruments) opens the device menu for track 1 (the first empty one). */
MB_TEST (keyboard_add_device_new_project)
{
    Engine e;
    e.prepare (48000.0, 512);
    MainComponent mc (e);
    mc.setSize (1600, 1000);
    mc.refreshAll();
    ctx.check (mc.keyPressed (juce::KeyPress ('=', {}, '=')) && mc.lastPickerTrack == 0, "= opens the device menu for track 1");
    juce::PopupMenu::dismissAllActiveMenus();
    e.setDevice (0, 0, createDevice (DeviceType::Acid));
    mc.lastPickerTrack = -1;
    ctx.check (mc.keyPressed (juce::KeyPress ('+', juce::ModifierKeys::shiftModifier, '+')) && mc.lastPickerTrack == 1, "+ opens it for the next empty track");
    juce::PopupMenu::dismissAllActiveMenus();
}

/** Live recording through the app itself: the LIVE button, keys, the app's timer, Stop. Two one-bar chords
    make a 2-bar pattern; a take with nothing played drops every bar it added. */
MB_TEST (app_live_record_two_chords)
{
    Engine e;
    e.prepare (48000.0, 512);
    e.setDevice (0, 1, createDevice (DeviceType::Poly));
    e.setDevice (0, 0, createDevice (DeviceType::Drums));
    e.project().patterns[0].slots[0].track.lanes[0][0].on = true;
    e.focusTrack = 1;
    MainComponent mc (e);
    mc.setSize (1600, 1000);
    std::vector<float> l (512), r (512);
    double samples = 0;
    const double stepLen = 48000.0 * 60.0 / 120.0 / 4.0;
    double offset = 0;
    auto runTo = [&] (double steps)
    {
        while (samples < offset + steps * stepLen)
        {
            e.process (l.data(), r.data(), 512, nullptr);
            samples += 512;
            if ((int) (samples / 512) % 3 == 0) mc.refreshAll();   // the app's timer
        }
    };
    auto keys = [&] (std::initializer_list<int> notes, bool on) { for (int n : notes) e.injectNote (1, n, 0.8f, on); };
    auto& topBar = *dynamic_cast<ui::TopBar*> (mc.getChildComponent (0)->getChildComponent (0));
    topBar.trigger ("rec");                    // LIVE
    ctx.check (e.isRecording() && e.project().recordMode == 0, "LIVE arms live recording");
    topBar.trigger ("play");
    runTo (0.1);
    offset = e.project().countIn ? samples + 16 * stepLen - 0.1 * stepLen : 0.0;   // count-in bar
    runTo (0.2);  keys ({ 60, 64, 67 }, true);
    runTo (15.6); keys ({ 60, 64, 67 }, false);
    runTo (16.1); keys ({ 62, 65, 69 }, true);
    runTo (31.5); keys ({ 62, 65, 69 }, false);
    runTo (16 * 6.5);
    ctx.check (e.project().patterns[0].lengthBars >= 6, "recording on: the pattern kept growing (" + juce::String (e.project().patterns[0].lengthBars) + ")");
    topBar.trigger ("stop");
    runTo (16 * 6.6);
    mc.refreshAll();
    const auto& tr = e.project().patterns[0].slots[1].track;
    const int st = e.project().stepTicks (0);
    int bar1 = 0, bar2 = 0;
    for (auto& n : tr.clip) { bar1 += n.start < 16 * st; bar2 += n.start >= 16 * st && n.start < 32 * st; }
    ctx.check (bar1 == 3 && bar2 == 3, "both chords recorded (" + juce::String (bar1) + " + " + juce::String (bar2) + ")");
    ctx.check (e.project().patterns[0].lengthBars == 2, "Stop: the pattern is 2 bars (" + juce::String (e.project().patterns[0].lengthBars) + ")");
    // a second take where nothing is played: every bar it added goes again
    topBar.trigger ("play");
    offset = samples + (e.project().countIn ? 16 * stepLen : 0.0);
    runTo (16 * 5.0);
    topBar.trigger ("rec");                    // LIVE again: record off
    ctx.check (e.project().patterns[0].lengthBars == 2, "a take with nothing new: back to 2 bars (" + juce::String (e.project().patterns[0].lengthBars) + ")");
    topBar.trigger ("stop");
}

/** Settings ▸ Keyboard: every shortcut comes from the shortcut table; rebinding one moves it. */
MB_TEST (keyboard_shortcuts_rebind)
{
    auto& sc = ui::shortcuts();
    sc.mappings().resetToDefaultMappings();
    Engine e;
    e.prepare (48000.0, 512);
    MainComponent mc (e);
    mc.setSize (1600, 1000);
    ctx.check (! e.project().metronome && mc.keyPressed (juce::KeyPress ('K', juce::ModifierKeys::commandModifier, 0)) && e.project().metronome,
               "Ctrl+K toggles the metronome (default)");
    sc.mappings().clearAllKeyPresses (ui::cmd::metronome);
    sc.mappings().addKeyPress (ui::cmd::metronome, juce::KeyPress ('9', juce::ModifierKeys::altModifier, 0));
    ctx.check (mc.keyPressed (juce::KeyPress ('9', juce::ModifierKeys::altModifier, 0)) && ! e.project().metronome, "rebound to Alt+9");
    ctx.check (! mc.keyPressed (juce::KeyPress ('K', juce::ModifierKeys::commandModifier, 0)) || ! e.project().metronome, "Ctrl+K no longer does it");
    // the computer-keyboard notes follow the table too
    ctx.check (sc.keyCode (ui::cmd::note0) == 'A' && sc.keyCode (ui::cmd::octaveUp) == 'X', "note keys: A plays C, X is octave up");
    sc.mappings().resetToDefaultMappings();
    // the Settings window builds (all three tabs)
    ui::SettingsPanel panel (mc.context(), mc.midiMap(), [] { return new juce::Component(); });
    panel.setSize (900, 640);
    ctx.check (panel.getNumChildComponents() > 0, "the Settings window builds");
}

/** The mouse by keys (docs/keyboard.md): lane values and note cells take ↑ / ↓, Delete right-clicks (clears),
    Shift+Enter is a Shift-click, and the piano roll's length / velocity / playhead keys. */
MB_TEST (keyboard_mouse_equivalents)
{
    ui::shortcuts().mappings().resetToDefaultMappings();
    Engine e;
    e.prepare (48000.0, 512);
    e.setDevice (0, 1, createDevice (DeviceType::Poly));
    e.focusTrack = 1;
    auto& tr = e.project().patterns[0].slots[1].track;
    tr.steps[0].addNote (60, 100, 96);
    MainComponent mc (e);
    mc.setSize (1600, 1000);
    mc.showPage (1, 1);
    mc.refreshAll();
    mc.createComponentSnapshot (mc.getLocalBounds());
    auto key = [&] (int code, juce::ModifierKeys m = {}) { const bool r = mc.keyPressed (juce::KeyPress (code, m, 0)); mc.refreshAll(); mc.createComponentSnapshot (mc.getLocalBounds()); return r; };
    auto seqAt = [] (float x, float y) { return [x, y] (const Feedback::Focus& f) { return dynamic_cast<SeqView*> (&f.fb->getView()) != nullptr && f.r.contains (x, y); }; };
    // the Steps view: step 1 in the step grid (x ≈ 190; a row is a note of the scale) and its velocity lane cell
    ctx.check (tabTo (mc, seqAt (190.0f, 534.0f)) && key (juce::KeyPress::returnKey) && tr.steps[0].noteCount == 2, "Enter on another note's cell adds it (a chord on VA)");
    const int velY = 444 + 180 + 16 + 18;
    ctx.check (tabTo (mc, seqAt (190.0f, (float) velY)) && key (juce::KeyPress::upKey) && tr.steps[0].vel[0] == 108, "↑ on a velocity cell raises it (" + juce::String (tr.steps[0].vel[0]) + ")");
    ctx.check (tabTo (mc, seqAt (190.0f, 500.0f)) && key (juce::KeyPress::deleteKey) && tr.steps[0].noteCount == 0, "Delete on a note cell clears it (a right-click)");
    Feedback::clearFocus();
    // the top bar: Shift+Enter on pad B copies the current pattern there (a Shift-click)
    const auto padB = mc.topBarView().zoneRect ("pat1").getCentre();
    const bool pad = tabTo (mc, [padB] (const Feedback::Focus& f) { return dynamic_cast<ui::TopBar*> (&f.fb->getView()) != nullptr && f.r.contains (padB); });
    ctx.check (pad && key (juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier) && e.device (1, 1) != nullptr, "Shift+Enter on pad B copies the pattern there");
    Feedback::clearFocus();
    // the piano roll's keys on a selection
    tr.clear();
    tr.pianoRoll = true;
    const int st = e.project().stepTicks (0);
    ClipNote n; n.start = 0; n.length = st; n.note = 60; n.vel = 100; tr.addClipNote (n);
    mc.seqView().setStepsView (false);
    mc.refreshAll();
    ctx.check (key ('A', juce::ModifierKeys::commandModifier), "Ctrl+A selects");
    key ('.');
    ctx.check (tr.clip.size() == 1 && tr.clip[0].length == 2 * st, "'.' makes it a grid unit longer");
    key (']');
    ctx.check (tr.clip[0].vel == 108, "']' raises its velocity");
    key ('L', juce::ModifierKeys (juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier));
    ctx.check (e.project().patterns[0].loopOn && e.project().patterns[0].loopStart == 0 && e.project().patterns[0].loopEnd == 2, "Ctrl+Shift+L loops the selection");
}

/** The rack scrolls with eight tracks, but ↑ / ↓ still select the instrument after a click in it. */
MB_TEST (keyboard_rack_arrows_select)
{
    ui::shortcuts().mappings().resetToDefaultMappings();
    Engine e;
    e.prepare (48000.0, 512);
    for (int t = 0; t < 6; ++t) e.setDevice (0, t, createDevice (DeviceType::Poly));
    e.focusTrack = 0;
    MainComponent mc (e);
    mc.setSize (1600, 1000);
    mc.addToDesktop (0);
    mc.setVisible (true);
    mc.grabKeyboardFocus();
    mc.refreshAll();
    // a click in the rack's scroll area (where the focus would go)
    juce::Component* hit = mc.getComponentAt (800, 300);
    while (hit != nullptr && ! hit->getWantsKeyboardFocus()) hit = hit->getParentComponent();
    ctx.check (hit == &mc, "a click in the rack leaves the keyboard with the app (" + juce::String (hit != nullptr ? hit->getName() : "none") + ")");
    ctx.check (mc.keyPressed (juce::KeyPress (juce::KeyPress::downKey)) && e.focusTrack.load() == 1, "↓ selects the next instrument");
    // page keys: on the rack ← / → step the selected instrument's presets; on the Seq page they walk the bars
    mc.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));   // from "Init" onto the first factory preset
    const auto before = e.device (0, 1)->presetName;
    mc.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    const auto after = e.device (0, 1)->presetName;
    ctx.check (after != before, "Rack: → the next preset (" + before + " -> " + after + ")");
    mc.keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
    ctx.check (e.device (0, 1)->presetName == before, "Rack: ← back to the previous one");
    e.project().patterns[0].lengthBars = 2;
    mc.showPage (1, 1);
    mc.context().viewBar = 0;
    mc.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    ctx.check (mc.context().viewBar == 1 && e.device (0, 1)->presetName == before, "Seq: → the next bar (the preset stays)");
    mc.removeFromDesktop();
}

/** Toolbar menus open at their button (not at the window's corner), by mouse and by Enter. */
MB_TEST (menus_open_at_their_button)
{
    ui::shortcuts().mappings().resetToDefaultMappings();
    Engine e;
    e.prepare (48000.0, 512);
    e.setDevice (0, 1, createDevice (DeviceType::Poly));
    e.focusTrack = 1;
    MainComponent mc (e);
    mc.setSize (1600, 1000);
    mc.addToDesktop (0);
    mc.setVisible (true);
    mc.showPage (1, 1);
    mc.seqView().setStepsView (false);
    mc.refreshAll();
    mc.createComponentSnapshot (mc.getLocalBounds());
    // Tab to the toolbar's Quantize pill, Enter opens its menu
    const bool found = tabTo (mc, [] (const Feedback::Focus& f) { return dynamic_cast<SeqView*> (&f.fb->getView()) != nullptr && f.r.getWidth() > 60 && f.r.getY() > 360 && f.r.getY() < 400 && f.r.getX() > 780 && f.r.getX() < 860; });
    juce::Rectangle<int> pill;
    if (found) pill = Feedback::navFocus().fb->getView().localAreaToGlobal (Feedback::navFocus().r).getSmallestIntegerContainer();
    mc.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    const auto area = mc.seqView().localAreaToGlobal (mc.seqView().lastMenuArea()).getSmallestIntegerContainer();
    ctx.check (found && area == pill, "the menu opens at the pill (" + pill.toString() + " / " + area.toString() + ")");
    juce::PopupMenu::dismissAllActiveMenus();
    mc.removeFromDesktop();
}

/** Changing the scale moves the notes: a new root shifts (C → F up a fourth), a new mode keeps degrees
    (Phrygian D♭ → Minor D), chromatic changes nothing, drums stay; one undo step restores every pattern. */
MB_TEST (scale_change_transposes)
{
    Engine e;
    e.prepare (48000.0, 512);
    e.setDevice (0, 0, createDevice (DeviceType::Poly));
    e.setDevice (0, 1, createDevice (DeviceType::Drums));
    e.setDevice (1, 0, createDevice (DeviceType::Poly));
    MainComponent mc (e);
    auto& p = e.project();
    auto& steps = p.patterns[0].slots[0].track.steps;
    steps[0].addNote (60); steps[0].addNote (61); steps[0].addNote (67);   // C, D♭, G
    ClipNote c; c.start = 0; c.length = 480; c.note = 63; c.vel = 100;   // E♭ in a clip of pattern B
    p.patterns[1].slots[0].track.addClipNote (c);
    auto& drum = p.patterns[0].slots[1].track.lanes[0][0];
    drum.on = true;
    auto isDrum = [&] (int pt, int t) { auto* d = e.device (pt, t); return d == nullptr || d->laneCount() > 0; };
    auto notes = [&] { juce::String s; for (int i = 0; i < steps[0].noteCount; ++i) s << (int) steps[0].notes[i] << " "; return s.trim(); };
    const int phr = 3, minor = 6;
    ScaleSettings from; from.root = 0; from.scale = phr;
    ScaleSettings to = from; to.root = 5;
    ctx.check (scaletranspose::project (p, from, to, isDrum) && notes() == "65 66 72", "C Phrygian → F Phrygian: up a fourth (" + notes() + ")");
    ScaleSettings back = to; back.root = 0;
    scaletranspose::project (p, to, back, isDrum);
    ctx.check (notes() == "60 61 67", "F → C: down a fourth, not up a fifth (" + notes() + ")");
    mc.context().snapshotAll();
    ScaleSettings cmin = back; cmin.scale = minor;
    scaletranspose::project (p, back, cmin, isDrum);
    ctx.check (notes() == "60 62 67", "C Phrygian → C Minor: D♭ becomes D (" + notes() + ")");
    ctx.check (p.patterns[1].slots[0].track.clip[0].note == 63, "E♭ is in both: it stays");
    ScaleSettings fmin = cmin; fmin.root = 5;
    scaletranspose::project (p, back, fmin, isDrum);   // C Phrygian → F Minor from the restored notes below
    ScaleSettings chrom = fmin; chrom.scale = 0;
    ctx.check (! scaletranspose::project (p, fmin, chrom, isDrum), "to Chromatic: no change");
    ctx.check (drum.on && p.patterns[0].slots[1].track.lanes[0][0].on, "drum lanes untouched");
    ctx.check (mc.context().undo() && notes() == "60 61 67" && p.patterns[1].slots[0].track.clip[0].note == 63, "one undo restores every pattern");
    scaletranspose::project (p, back, fmin, isDrum);
    ctx.check (notes() == "65 67 72", "C Phrygian → F Minor: up a fourth and the minor second (" + notes() + ")");
    ctx.check (scaletranspose::note (62, 0, { 0, 1, 3, 5, 7, 8, 10 }, 0, { 0, 2, 3, 5, 7, 8, 10 }) == 63,
               "an off-scale note keeps its distance from the degree below");
}

/** Automation moves the controls: a step lock playing turns its knob (and a macro on it); stopped, they return. */
MB_TEST (automated_knob_moves)
{
    Engine e;
    e.prepare (48000.0, 512);
    auto dev = createDevice (DeviceType::Mono);
    auto* d = dev.get();
    e.setDevice (0, 0, std::move (dev));
    const int cut = d->params().indexOf ("cutoff");
    d->setNorm (cut, 0.2f);
    auto& tr = e.project().patterns[0].slots[0].track;
    for (int s = 0; s < 16; ++s) tr.steps[(size_t) s].addNote (36);
    tr.lockFor (cut, true)->values[0] = d->params()[cut].fromNorm (0.9f);
    ParamBinding knob (nullptr, d, cut);
    ctx.check (std::abs (knob.getNorm() - 0.2f) < 1e-3f, "stopped: the stored value");
    e.project().countIn = false;
    e.play();
    std::vector<float> l (512), r (512);
    e.process (l.data(), r.data(), 512, nullptr);
    e.process (l.data(), r.data(), 512, nullptr);
    ctx.check (std::abs (knob.getNorm() - 0.9f) < 1e-3f, "step 1 plays its lock: the knob shows it (" + juce::String (knob.getNorm()) + ")");
    int macro = -1;
    for (int m = 0; m < 8; ++m)
        if (macro < 0 && d->macros()[(size_t) m].count > 0 && d->macros()[(size_t) m].targets[0].param == cut) macro = m;
    ctx.check (macro >= 0 && std::abs (MacroBinding (nullptr, d, macro).getNorm() - d->getMacro (macro)) > 0.1f,
               "the macro on the locked parameter follows it");
    e.stop();
    e.process (l.data(), r.data(), 512, nullptr);
    ctx.check (std::abs (knob.getNorm() - 0.2f) < 1e-3f, "stopped: back to the stored value (" + juce::String (knob.getNorm()) + ")");
}

/** The layout follows the window's shape: 16:9 is 1778 wide (the pages and the top bar's right edge with it),
    16:10 is 1600, narrower windows keep 1600 and letterbox. */
MB_TEST (layout_follows_window_shape)
{
    Engine e;
    e.prepare (48000.0, 512);
    MainComponent mc (e);
    auto size = [&] (int w, int h) { mc.setSize (w, h); mc.refreshAll(); mc.createComponentSnapshot (mc.getLocalBounds()); };   // zones exist once painted
    size (1920, 1080);
    ctx.check (geo::W == 1778, "16:9 -> 1778 wide (" + juce::String (geo::W) + ")");
    ctx.check (mc.topBarView().getWidth() == 1778 && mc.topBarView().zoneRect ("menu").getRight() > 1740, "the top bar reaches the right edge");
    ctx.check (mc.topBarView().zoneRect ("pat7").getRight() < mc.topBarView().zoneRect ("rename").getX(), "the pattern pads clear the project name");
    size (1600, 1000);
    ctx.check (geo::W == 1600 && mc.topBarView().zoneRect ("pat7").getRight() < mc.topBarView().zoneRect ("rename").getX(), "16:10 -> 1600, still clear");
    size (1200, 1000);
    ctx.check (geo::W == 1600, "narrower windows keep 1600 (letterboxed)");
    size (1600, 1000);   // the other tests lay out at 16:10
}
