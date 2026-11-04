#include "Tests.h"
#include "ui/PianoRoll.h"

using namespace mb;
using namespace mb::ui;

namespace
{
/** A piano roll on its own: 1000 × 360 (a bar in view: a cell is 58 px wide, the grid from x = 72). */
struct RollRig
{
    Engine engine;
    AppContext ctx { engine, true };
    PianoRoll roll { ctx };
    RollRig()
    {
        engine.prepare (48000.0, 256);
        engine.focusTrack = 1;
        engine.project().patterns[0].lengthBars = 4;
        ctx.rollSettings.tool = 1;   // Draw (the select-tool tests switch)
        ctx.rollSettings.fold = ctx.rollSettings.scale = false;   // every semitone a row (the app folds to the scale by default)
        roll.setSize (1000, 360);
        roll.topNote = 72;
    }
    Track& tr() { return engine.project().patterns[0].slots[1].track; }
    juce::Point<float> cell (double step, int note, float fracX = 0.5f) const { return roll.pointFor (step + fracX, note); }
    juce::MouseEvent ev (juce::Point<float> p, juce::ModifierKeys mods = {}, juce::Point<float> down = {}, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &roll, &roll, now, down, now, clicks, p != down);
    }
    void click (juce::Point<float> p, juce::ModifierKeys mods = {}) { roll.mouseDown (ev (p, mods, p)); roll.mouseUp (ev (p, mods, p)); }
    void drag (juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = {})
    {
        roll.mouseDown (ev (from, mods, from));
        for (int i = 1; i <= 4; ++i) roll.mouseDrag (ev (from + (to - from) * (i / 4.0f), mods, from));
        roll.mouseUp (ev (to, mods, from));
    }
    bool key (int code, juce::ModifierKeys mods = {}) { return roll.keyCommand (juce::KeyPress (code, mods, 0)); }
    int lenOf (int step, int note) { const auto& s = tr().steps[(size_t) step]; const int i = s.indexOf (note); return i >= 0 ? s.len[i] : -1; }
};
const juce::ModifierKeys kShift (juce::ModifierKeys::shiftModifier), kAlt (juce::ModifierKeys::altModifier), kCmd (juce::ModifierKeys::commandModifier);
} // namespace

MB_TEST (pianoroll_add_draw_select)
{
    RollRig r;
    r.click (r.cell (2, 60));
    ctx.check (r.tr().steps[2].hasNote (60) && r.lenOf (2, 60) == 96, "click adds a note with the default length");
    r.drag (r.cell (4, 62, 0.3f), r.cell (6, 62, 0.95f));
    ctx.check (r.lenOf (4, 62) >= 3 * 128 - 40 && r.lenOf (4, 62) <= 3 * 128 + 40, "click-drag draws a note with a length (" + juce::String (r.lenOf (4, 62)) + ")");
    r.click (r.cell (9, 64));
    ctx.check (r.lenOf (9, 64) == r.lenOf (4, 62), "a new note takes the length of the last drawn one");
    r.click (r.cell (2, 60, 0.4f));
    ctx.check (r.roll.selection().size() == 1 && r.roll.selection()[0].step == 2, "clicking a note selects it (and keeps it)");
    ctx.check (r.tr().steps[2].hasNote (60), "selecting does not delete");
    r.click (r.cell (9, 64, 0.3f));
    r.click (r.cell (2, 60, 0.3f), kShift);
    ctx.check (r.roll.selection().size() == 2, "Shift-click adds to the selection");
    r.drag (r.cell (1, 66), r.cell (10, 58), kShift);
    ctx.check (r.roll.selection().size() == 3, "a Shift-drag box selects the notes inside it (" + juce::String ((int) r.roll.selection().size()) + ")");
    ctx.check (r.key (juce::KeyPress::escapeKey) && ! r.roll.hasSelection(), "Esc clears the selection");
    ctx.check (r.key ('A', kCmd) && r.roll.selection().size() == 3, "Ctrl+A selects every note");
}

MB_TEST (pianoroll_move_copy_resize)
{
    RollRig r;
    r.click (r.cell (2, 60));
    r.drag (r.cell (2, 60, 0.4f), r.cell (5, 63, 0.4f));
    ctx.check (! r.tr().steps[2].hasNote (60) && r.tr().steps[5].hasNote (63), "dragging a note moves it in time and pitch");
    r.drag (r.cell (5, 63, 0.4f), r.cell (7, 66, 0.4f), kShift);
    ctx.check (r.tr().steps[7].hasNote (63), "Shift keeps the pitch while moving");
    r.drag (r.cell (7, 63, 0.4f), r.cell (9, 63, 0.4f), kCmd);
    ctx.check (r.tr().steps[7].hasNote (63) && r.tr().steps[9].hasNote (63), "Ctrl-drag copies");
    const auto end = r.cell (9, 63, 0.75f) - juce::Point<float> (4.0f, 0.0f);   // the right end of a 3/4-step note
    r.drag (end, end + juce::Point<float> (2 * 58.0f, 0.0f));
    ctx.check (r.lenOf (9, 63) >= 2 * 128 + 64 && r.lenOf (9, 63) <= 3 * 128, "dragging the right end lengthens it (" + juce::String (r.lenOf (9, 63)) + ")");
    const int before = r.lenOf (9, 63);
    r.drag (r.cell (9, 63, 0.0f) + juce::Point<float> (4.0f, 0.0f), r.cell (10, 63, 0.0f) + juce::Point<float> (4.0f, 0.0f));
    ctx.check (r.tr().steps[10].hasNote (63) && r.lenOf (10, 63) == before - 128, "dragging the left end moves the start and keeps the end");
}

MB_TEST (pianoroll_per_note_and_across_bars)
{
    RollRig r;
    auto& s = r.tr().steps[0];
    s.addNote (60, 100, 128); s.addNote (64, 60, 512); s.addNote (67, 90, 64);
    ctx.check (s.len[1] == 512 && s.vel[1] == 60 && s.len[2] == 64, "each note of a chord keeps its own length and velocity");
    r.drag (r.cell (0, 67, 0.2f), r.cell (3, 67, 0.2f));
    ctx.check (s.noteCount == 2 && r.lenOf (3, 67) == 64 && r.tr().steps[3].vel[0] == 90, "moving one note of a chord carries its own values");
    // a note started in bar 1 rings into bar 2 and can be picked there
    r.tr().steps[14].addNote (70, 100, 512);
    r.ctx.viewBar = 1;
    r.roll.syncView();
    r.click (r.cell (17, 70, 0.5f));
    ctx.check (r.roll.selection().size() == 1 && r.roll.selection()[0].step == 14, "a note from the previous bar is selectable where it rings");
}

MB_TEST (pianoroll_keys_clipboard_undo)
{
    RollRig r;
    r.click (r.cell (1, 60));
    r.click (r.cell (1, 60, 0.3f));
    ctx.check (r.key (juce::KeyPress::rightKey) && r.tr().steps[2].hasNote (60), "→ nudges a step later");
    ctx.check (r.key (juce::KeyPress::upKey) && r.tr().steps[2].hasNote (61), "↑ a semitone up");
    ctx.check (r.key (juce::KeyPress::upKey, kShift) && r.tr().steps[2].hasNote (73), "Shift+↑ an octave");
    ctx.check (r.key (juce::KeyPress::rightKey, kShift) && r.tr().steps[18].hasNote (73), "Shift+→ a bar");
    ctx.check (r.key ('D', kCmd) && r.tr().steps[19].hasNote (73) && r.tr().steps[18].hasNote (73), "Ctrl+D duplicates right after");
    r.key ('C', kCmd);
    r.key ('V', kCmd);
    ctx.check (r.tr().steps[20].hasNote (73), "Ctrl+C / Ctrl+V pastes after the selection");
    r.tr().steps[20].microTiming = 9;
    ctx.check (r.key ('Q') && r.tr().steps[20].microTiming == 0, "Q quantizes the selected notes to the grid");
    r.ctx.undo();
    ctx.check (r.key (juce::KeyPress::deleteKey) && ! r.tr().steps[20].hasNote (73), "Delete removes the selection");
    ctx.check (r.ctx.undo() && r.tr().steps[20].hasNote (73), "undo brings it back");
    ctx.check (r.ctx.redo() && ! r.tr().steps[20].hasNote (73), "redo removes it again");
    int undos = 0;
    while (r.ctx.undo()) ++undos;
    ctx.check (undos >= 6 && r.tr().steps[1].noteCount == 0, "every edit is its own undo step, back to the empty pattern");
    r.roll.setFirstStep (0);   // the nudges scrolled to bar 2
    r.click (r.cell (3, 58));
    ctx.check (r.tr().steps[3].hasNote (58), "(a note to delete)");
    r.roll.mouseDown (r.ev (r.cell (3, 58, 0.3f), juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), r.cell (3, 58, 0.3f)));
    ctx.check (! r.tr().steps[3].hasNote (58), "right-click deletes a note");
    r.click (r.cell (5, 58));
    ctx.check (r.tr().steps[5].hasNote (58), "(another note)");
    r.roll.mouseDoubleClick (r.ev (r.cell (5, 58, 0.3f), {}, r.cell (5, 58, 0.3f), 2));
    ctx.check (! r.tr().steps[5].hasNote (58), "double-click deletes a note");
}

MB_TEST (pianoroll_select_tool)
{
    RollRig r;
    r.ctx.rollSettings.tool = 0;   // Select
    r.click (r.cell (2, 60));
    ctx.check (r.tr().steps[2].noteCount == 0, "with the Select tool a click on empty space adds nothing");
    r.roll.mouseDoubleClick (r.ev (r.cell (2, 60), {}, r.cell (2, 60), 2));
    ctx.check (r.tr().steps[2].hasNote (60), "a double-click adds a note");
    r.ctx.rollSettings.tool = 1;
    r.click (r.cell (4, 62)); r.click (r.cell (6, 64));
    r.ctx.rollSettings.tool = 0;
    r.drag (r.cell (1, 66), r.cell (7, 58));
    ctx.check (r.roll.selection().size() == 3, "a plain drag on empty space selects a box of notes");
    r.click (r.cell (10, 57));
    ctx.check (! r.roll.hasSelection(), "a click on empty space clears the selection");
}

MB_TEST (pianoroll_functions)
{
    RollRig r;
    auto& tr = r.tr();
    auto reset = [&] { tr.clear(); r.roll.clearSelection(); };
    reset(); tr.steps[0].addNote (60, 100, 64); tr.steps[4].addNote (62, 100, 64);
    r.roll.legato();
    ctx.check (r.lenOf (0, 60) == 4 * 128, "legato stretches a note to the next one");
    reset(); tr.steps[0].addNote (60, 100, 128); tr.steps[3].addNote (64, 100, 128);
    r.roll.reverse();
    ctx.check (tr.steps[0].hasNote (64) && tr.steps[3].hasNote (60), "reverse mirrors the notes in time");
    reset(); tr.steps[0].addNote (60); tr.steps[0].addNote (67);
    r.roll.invertPitch();
    ctx.check (tr.steps[0].hasNote (67) && tr.steps[0].hasNote (60), "invert flips pitches around the middle (a fifth stays a fifth)");
    reset(); tr.steps[2].addNote (61);
    r.engine.project().scale = { true, 0, 1, 0, false };
    r.roll.fitToScale();
    ctx.check (tr.steps[2].hasNote (60), "fit to scale moves C# to C in C major");
    reset(); tr.steps[0].addNote (60, 100, 3 * 128);
    r.roll.chop();
    ctx.check (tr.steps[0].hasNote (60) && tr.steps[1].hasNote (60) && tr.steps[2].hasNote (60) && r.lenOf (0, 60) == 128, "chop splits into one-step notes");
    r.roll.selectAll(); r.roll.glue();
    ctx.check (tr.steps[1].noteCount == 0 && r.lenOf (0, 60) == 3 * 128, "glue joins them back");
    reset(); tr.steps[0].addNote (60); tr.steps[0].addNote (64); tr.steps[0].addNote (67);
    r.roll.strum();
    ctx.check (tr.steps[0].off[0] == 0 && tr.steps[0].off[2] > tr.steps[0].off[1] && tr.steps[0].vel[2] < tr.steps[0].vel[0], "strum staggers and softens the upper notes");
    r.roll.quantize (1.0f, false);
    ctx.check (tr.steps[0].off[2] == 0, "quantize puts them back on the grid");
    reset(); tr.steps[0].addNote (60, 100, 128); tr.steps[2].addNote (62, 100, 128);
    r.roll.stretch (2.0f);
    ctx.check (tr.steps[4].hasNote (62) && r.lenOf (0, 60) == 256, "stretch x2 doubles positions and lengths");
    r.roll.transpose (12);
    ctx.check (tr.steps[0].hasNote (72), "transpose an octave");
    r.roll.velocityRamp (true);
    ctx.check (tr.steps[0].vel[0] < tr.steps[4].vel[0], "velocity ramp up");
    reset(); tr.steps[0].addNote (60);
    r.roll.rotate (1);
    ctx.check (tr.steps[1].hasNote (60), "rotate right moves the pattern a step");
    r.roll.rotate (-1); r.roll.rotate (-1);
    ctx.check (tr.steps[(size_t) (4 * 16 - 1)].hasNote (60), "rotate left wraps to the end");
    reset(); tr.steps[3].addNote (60); tr.steps[5].addNote (62);
    r.roll.selectAll(); r.roll.clearSelection();
    r.ctx.viewBar = 0;
    r.click (r.cell (3, 60, 0.3f));
    r.roll.insertStep (true);
    ctx.check (tr.steps[4].hasNote (60) && tr.steps[6].hasNote (62), "insert step pushes later steps right");
    r.click (r.cell (4, 60, 0.3f));
    r.roll.toggleMute();
    ctx.check (tr.steps[4].isMuted (0), "mute keeps the note but silences it");
    ctx.check (r.ctx.undo() && ! tr.steps[4].isMuted (0), "and every function is undoable");
}

MB_TEST (pianoroll_view_zoom_fold)
{
    RollRig r;
    ctx.check (r.roll.visibleSteps() == 16, "1 bar in view");
    r.roll.zoomToFit();
    ctx.check (r.roll.visibleSteps() == 64 && r.roll.firstStep() == 0, "Fit shows the whole 4-bar pattern");
    r.roll.zoomTime (0.5, r.cell (40, 60).x);
    ctx.check (r.roll.visibleSteps() == 32 && r.roll.firstStep() > 0 && r.roll.firstStep() <= 40, "zooming in keeps the point under the mouse in view");
    ctx.check (r.key ('=', kCmd) && r.roll.visibleSteps() == 16, "Ctrl+= zooms in");
    r.key ('=', kCmd); r.key ('=', kCmd);
    ctx.check (r.roll.visibleSteps() == 4, "down to a quarter bar");
    ctx.check (r.key ('-', kCmd) && r.roll.visibleSteps() == 8, "Ctrl+- zooms out");
    const int rows = r.roll.rows();
    ctx.check (r.key ('=', juce::ModifierKeys (juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier)) && r.roll.rows() < rows, "Ctrl+Shift+= shows fewer, taller rows");
    r.key ('0', kCmd);
    // the ruler: dragging sideways scrolls
    r.roll.zoomTime (0.25);
    const int before = r.roll.firstStep();
    r.drag ({ 600.0f, 8.0f }, { 400.0f, 8.0f });
    ctx.check (r.roll.firstStep() > before, "dragging the ruler left scrolls later (" + juce::String (before) + " -> " + juce::String (r.roll.firstStep()) + ")");
    const int vis = r.roll.visibleSteps();
    r.drag ({ 500.0f, 8.0f }, { 500.0f, 80.0f });
    ctx.check (r.roll.visibleSteps() < vis, "dragging the ruler down zooms in");
    r.roll.zoomToFit(); r.ctx.rollSettings.viewSteps = 16; r.roll.setFirstStep (0);
    r.tr().steps[0].addNote (40); r.tr().steps[1].addNote (90);
    r.ctx.rollSettings.fold = true;
    // folded: only the two pitches in use are rows, the top one is 90
    const auto top = r.roll.pointFor (0, 90);
    r.roll.mouseDown (r.ev ({ 20.0f, top.y }, kShift, { 20.0f, top.y }));
    ctx.check (r.roll.selection().size() == 1 && r.roll.selection()[0].note == 90, "folded rows: the top row is the highest note in use");
}

/** Grid and snapping (docs/sequencer.md 48–57): drawing, moving and resizing land on the grid; triplets; Alt is free. */
MB_TEST (pianoroll_grid_snap)
{
    RollRig r;
    auto& rs = r.ctx.rollSettings;
    rs.grid = 4;   // 1/8: two steps at 1/16
    ctx.check (r.roll.gridTicks() == kPPQ / 2, "1/8 is two steps");
    r.click (r.cell (5, 60, 0.7f));
    ctx.check (r.tr().steps[4].hasNote (60) && r.tr().steps[4].off[0] == 0, "a drawn note starts on the grid line before the click");
    r.drag (r.cell (4, 60, 0.4f), r.cell (6, 60, 0.6f));   // 2.2 steps later: nearest 1/8 line is step 6
    ctx.check (r.tr().steps[6].hasNote (60) && r.tr().steps[6].off[0] == 0, "a moved note snaps to the nearest grid line");
    rs.grid = 3;
    r.tr().steps[6].off[0] = 7;   // off the grid
    r.drag (r.cell (6, 60, 0.5f), r.cell (8, 60, 0.4f));
    ctx.check (r.tr().steps[8].hasNote (60) && r.tr().steps[8].off[0] == 0, "an off-grid note moved lands on the grid");
    r.drag (r.cell (8, 60, 0.5f), r.cell (9, 60, 0.75f), kAlt);
    const auto& st = r.tr().steps[9].hasNote (60) ? r.tr().steps[9] : r.tr().steps[10];
    ctx.check (st.hasNote (60) && st.off[st.indexOf (60)] != 0, "Alt moves it off the grid");
    // triplets: 1/16T at 1/16 steps is 2/3 of a step
    rs.triplet = true;
    ctx.check (r.roll.gridTicks() == kPPQ / 6, "a 1/16 triplet is 2/3 of a step");
    r.tr().clear(); r.roll.clearSelection();
    r.click (r.cell (0, 62, 0.8f));          // tick ~38 -> 32 (the second triplet line)
    const auto& s0 = r.tr().steps[1];
    ctx.check (s0.hasNote (62) && s0.startTick (1, 0) == 32, "drawing on a triplet grid: 2/3 of a step (start " + juce::String (s0.hasNote (62) ? s0.startTick (1, 0) : -1) + " / 48)");
    r.roll.selectAll();
    r.roll.quantize (1.0f, true);
    ctx.check (r.lenOf (1, 62) > 0 && std::abs (r.lenOf (1, 62) - 85) <= 2, "quantize lengths to the triplet grid");
    rs.triplet = false;
    // snap off
    ctx.check (r.key ('4', kCmd) && ! rs.snap, "Ctrl+4 turns snapping off");
    r.tr().clear(); r.roll.clearSelection();
    r.click (r.cell (3, 64, 0.5f));
    ctx.check (r.tr().steps[3].hasNote (64) || r.tr().steps[4].hasNote (64), "with snap off a note starts where clicked");
    const auto& s3 = r.tr().steps[3].hasNote (64) ? r.tr().steps[3] : r.tr().steps[4];
    const int tick = s3.startTick (r.tr().steps[3].hasNote (64) ? 3 : 4, s3.indexOf (64));
    ctx.check (std::abs (tick - (3 * 48 + 24)) <= 2, "at its own tick (" + juce::String (tick) + ")");
    rs.snap = true;
    // nudge by the grid value
    rs.grid = 5;   // 1/4
    r.tr().clear(); r.click (r.cell (0, 60)); r.click (r.cell (0, 60, 0.3f));
    ctx.check (r.key (juce::KeyPress::rightKey) && r.tr().steps[4].hasNote (60), "→ nudges a grid unit (a quarter: 4 steps)");
}

/** Several bars: notes drawn, moved and resized across bar lines, notes longer than a bar, the view scrolling. */
MB_TEST (pianoroll_multi_bar)
{
    RollRig r;
    r.roll.zoomToFit();   // the whole 4-bar pattern
    r.drag (r.cell (12, 60, 0.1f), r.cell (40, 60, 0.9f));
    ctx.check (r.tr().steps[12].hasNote (60) && r.lenOf (12, 60) >= 28 * 128 && r.lenOf (12, 60) <= 30 * 128,
               "a note drawn across two bar lines (" + juce::String (r.lenOf (12, 60) / 128.0) + " steps)");
    r.drag (r.cell (13, 60, 0.5f), r.cell (21, 60, 0.5f));
    ctx.check (r.tr().steps[20].hasNote (60), "moved into the next bar");
    r.roll.selectAll();
    r.roll.copySelection (false);
    r.roll.paste();
    ctx.check (r.roll.selection().size() == 1 && r.roll.selection()[0].step >= 48 && r.roll.selection()[0].step == 20 + r.lenOf (20, 60) / 128,
               "pasted right after it, in bar 4 (step " + juce::String (r.roll.selection().empty() ? -1 : r.roll.selection()[0].step) + ")");
    // dragging past the edge scrolls
    r.ctx.rollSettings.viewSteps = 16; r.roll.setFirstStep (0);
    r.tr().clear(); r.roll.clearSelection();
    r.click (r.cell (10, 64));
    r.roll.mouseDown (r.ev (r.cell (10, 64, 0.3f), {}, r.cell (10, 64, 0.3f)));
    for (int i = 0; i < 12; ++i) r.roll.mouseDrag (r.ev ({ 999.0f, r.cell (10, 64).y }, {}, r.cell (10, 64, 0.3f)));
    r.roll.mouseUp (r.ev ({ 999.0f, r.cell (10, 64).y }, {}, r.cell (10, 64, 0.3f)));
    ctx.check (r.roll.firstStep() > 0 && r.roll.selection().size() == 1 && r.roll.selection()[0].step > 16,
               "dragging a note past the edge scrolls and carries it into the next bar (step " + juce::String (r.roll.selection().empty() ? -1 : r.roll.selection()[0].step) + ")");
    // the bar strip / ← → pick a bar: the view goes there
    r.ctx.viewBar = 3; r.roll.syncView();
    ctx.check (r.roll.firstStep() == 48, "picking bar 4 scrolls the roll to it");
}

/** Double-clicking a note removes it and nothing else: its auditions are not recorded (Step record armed), and
    the undo history keeps the removal. */
MB_TEST (pianoroll_double_click_while_recording)
{
    RollRig r;
    auto& p = r.engine.project();
    p.recordMode = 1;
    r.engine.setRecord (true);
    r.tr().steps[4].addNote (60);
    std::vector<float> l (256), rr (256);
    auto run = [&] { for (int i = 0; i < 4; ++i) r.engine.process (l.data(), rr.data(), 256, nullptr); };
    const int cursor = r.engine.stepRecordCursor.load();
    const auto at = r.cell (4, 60, 0.3f);
    r.click (at); run();
    r.roll.mouseDown (r.ev (at, {}, at, 2));
    r.roll.mouseDoubleClick (r.ev (at, {}, at, 2));
    r.roll.mouseUp (r.ev (at, {}, at, 2));
    run();
    int notes = 0;
    for (int s = 0; s < 64; ++s) notes += r.tr().steps[(size_t) s].noteCount;
    ctx.check (notes == 0, "the note is gone and no note was added (" + juce::String (notes) + ")");
    ctx.check (r.engine.stepRecordCursor.load() == cursor, "the step-record cursor did not move");
    ctx.check (r.ctx.undo() && r.tr().steps[4].hasNote (60), "undo brings the note back");
}

/** Scale lock on: the roll shows only the scale's notes (plus notes already off it); drawing stays in the scale. */
MB_TEST (pianoroll_scale_lock_rows)
{
    RollRig r;
    auto& sc = r.engine.project().scale;
    sc = { true, 0, 1, 0, false };   // C major, locked
    r.tr().steps[8].addNote (66);   // F#: off the scale, already there
    r.roll.createComponentSnapshot (r.roll.getLocalBounds());
    ctx.check (r.cell (0, 61).y < PianoRoll::kRuler, "C# has no row");
    ctx.check (r.cell (0, 62).y > r.cell (0, 64).y && r.cell (0, 64).y - r.cell (0, 65).y > 15.0f, "D, E, F are adjacent rows");
    ctx.check (r.cell (0, 66).y > 0.0f && r.cell (0, 66).y < 360.0f, "the off-scale F# in the track keeps a row");
    r.click (r.cell (2, 64));
    ctx.check (r.tr().steps[2].hasNote (64), "drawing on a row writes its scale note");
    sc.lock = false;
    r.roll.createComponentSnapshot (r.roll.getLocalBounds());
    ctx.check (r.cell (0, 61).y > 0.0f && r.cell (0, 61).y < 360.0f, "unlocked: every pitch again");
}

/** The piano roll on a clip (docs/sequencer.md): notes anywhere, the same pitch twice in one step, moving,
    resizing, functions and undo work on clip notes. */
MB_TEST (pianoroll_clip_mode)
{
    RollRig r;
    r.tr().pianoRoll = true;
    auto& rs = r.ctx.rollSettings;
    rs.grid = 2; rs.lengthMode = 1;   // 1/32 grid and notes: two of one pitch inside a step
    r.click (r.cell (4, 60, 0.1f));
    r.click (r.cell (4, 60, 0.6f));
    const int st = r.engine.project().stepTicks (0);
    ctx.check (r.tr().clip.size() == 2 && r.tr().clip[0].start == 4 * st && r.tr().clip[1].start == 4 * st + st / 2,
               "two notes of one pitch in one step (" + juce::String ((int) r.tr().clip.size()) + ")");
    ctx.check (! r.tr().hasStepNotes(), "nothing written to the steps");
    r.drag (r.cell (4, 60, 0.1f), r.cell (9, 64, 0.1f));
    ctx.check (r.tr().clip[0].start == 9 * st && r.tr().clip[0].note == 64 || r.tr().clip[1].start == 9 * st, "a clip note moves in time and pitch");
    const auto* moved = [&] () -> const ClipNote* { for (auto& n : r.tr().clip) if (n.note == 64) return &n; return nullptr; }();
    ctx.check (moved != nullptr && moved->start == 9 * st, "to step 10, E4");
    const int lenBefore = moved ? moved->length : 0;
    r.drag (r.cell (9, 64, 0.0f) + juce::Point<float> (lenBefore / (float) st * 58.0f - 3.0f, 0.0f), r.cell (12, 64, 0.0f));
    moved = [&] () -> const ClipNote* { for (auto& n : r.tr().clip) if (n.note == 64) return &n; return nullptr; }();
    ctx.check (moved && moved->length == 3 * st, "its end drags to the grid (" + juce::String (moved ? moved->length / (double) st : 0.0) + " steps)");
    r.roll.selectAll();
    r.roll.transpose (12);
    ctx.check (r.tr().clip.size() == 2 && r.tr().clip[0].note >= 72, "functions work on clip notes");
    ctx.check (r.ctx.undo() && r.tr().clip.size() == 2 && (r.tr().clip[0].note == 64 || r.tr().clip[0].note == 60), "and undo");
    r.roll.selectAll();
    r.key (juce::KeyPress::deleteKey);
    ctx.check (r.tr().clip.empty() && r.tr().pianoRoll, "Delete empties the clip (still a piano roll)");
}

/** Paint and Erase tools, dotted grids, the loop range and seeking from the ruler (docs/sequencer.md P0). */
MB_TEST (pianoroll_tools_loop_seek)
{
    RollRig r;
    auto& rs = r.ctx.rollSettings;
    rs.tool = 2;   // Paint
    rs.grid = 4;   // 1/8
    r.drag (r.cell (0, 60, 0.1f), r.cell (7, 60, 0.9f));
    int n = 0;
    for (int s = 0; s < 16; ++s) n += r.tr().steps[(size_t) s].hasNote (60);
    ctx.check (n == 4 && r.tr().steps[0].hasNote (60) && r.tr().steps[6].hasNote (60), "Paint lays a note per 1/8 along the drag (" + juce::String (n) + ")");
    ctx.check (r.ctx.undo() && ! r.tr().steps[0].hasNote (60), "the stroke is one undo step");
    r.ctx.redo();
    rs.tool = 3;   // Erase
    r.drag (r.cell (0, 60, 0.3f), r.cell (5, 60, 0.3f));
    ctx.check (! r.tr().steps[0].hasNote (60) && ! r.tr().steps[2].hasNote (60) && ! r.tr().steps[4].hasNote (60) && r.tr().steps[6].hasNote (60),
               "Erase removes what the drag passes over");
    rs.tool = 1;
    rs.dotted = true;
    ctx.check (r.roll.gridTicks() == kPPQ * 3 / 4 && r.roll.gridLabel().contains ("1/8D"), "a dotted 1/8 grid is three 1/16 steps");
    rs.dotted = false;
    // Shift-drag in the ruler: a loop range; the toolbar's Loop turns it off and on
    r.drag ({ (float) r.cell (4, 60, 0.0f).x, 8.0f }, { (float) r.cell (8, 60, 0.0f).x, 8.0f }, kShift);
    const auto& pt = r.engine.project().patterns[0];
    ctx.check (pt.loopOn && pt.loopStart == 4 && pt.loopEnd == 8, "Shift-drag in the ruler sets the loop (" + juce::String (pt.loopStart) + "-" + juce::String (pt.loopEnd) + ")");
    r.roll.toggleLoop();
    ctx.check (! pt.loopOn && pt.loopEnd == 8, "Loop off keeps the range");
    // a click on the ruler moves the playhead
    r.click ({ (float) r.cell (10, 60, 0.2f).x, 8.0f });
    std::vector<float> l (256), rr (256);
    r.engine.process (l.data(), rr.data(), 256, nullptr);
    ctx.check (r.engine.currentStep() == 10, "a click on the ruler seeks (" + juce::String (r.engine.currentStep()) + ")");
}

/** Note lock, chord tools and fit to notes / selection (docs/sequencer.md P1). */
MB_TEST (pianoroll_lock_chords_fit)
{
    RollRig r;
    r.tr().pianoRoll = true;
    const int st = r.engine.project().stepTicks (0);
    auto add = [&] (int step, int note) { ClipNote n; n.start = step * st; n.length = st; n.note = (uint8_t) note; return r.tr().addClipNote (n); };
    add (0, 60); add (0, 64); add (0, 67); add (8, 62); add (8, 65); add (8, 69);
    // lock the first chord: functions and deletes leave it alone
    r.click (r.cell (0, 60, 0.3f));
    r.roll.selectChords();
    ctx.check (r.roll.selection().size() == 3, "Select chords takes every note starting with the selection");
    r.key ('L', kCmd);
    r.roll.selectAll();
    r.roll.transpose (1);
    int moved = 0, still = 0;
    for (auto& n : r.tr().clip) { if (n.start == 0) still += (n.note == 60 || n.note == 64 || n.note == 67); else moved += (n.note == 63 || n.note == 66 || n.note == 70); }
    ctx.check (still == 3 && moved == 3, "locked notes keep their pitch; the rest transpose");
    r.roll.selectAll();
    r.key (juce::KeyPress::deleteKey);
    ctx.check (r.tr().clip.size() == 3, "Delete leaves the locked notes");
    r.drag (r.cell (0, 64, 0.3f), r.cell (4, 64, 0.3f));
    ctx.check (r.tr().clip[0].start == 0 && r.tr().clip[1].start == 0, "a locked note does not move");
    r.roll.selectAll(); r.key ('L', kCmd);   // unlock
    // chord tools
    r.roll.chordInvert (true);
    std::vector<int> notes; for (auto& n : r.tr().clip) notes.push_back (n.note);
    std::sort (notes.begin(), notes.end());
    ctx.check (notes == std::vector<int> ({ 64, 67, 72 }), "invert up: C E G becomes E G C");
    r.roll.selectAll(); r.roll.chordClose();
    notes.clear(); for (auto& n : r.tr().clip) notes.push_back (n.note);
    std::sort (notes.begin(), notes.end());
    ctx.check (notes == std::vector<int> ({ 64, 67, 72 }) || notes.back() - notes.front() < 12, "close voicing keeps the chord within an octave");
    r.roll.selectAll(); r.roll.chordAddBass();
    ctx.check (r.tr().clip.size() == 4, "add bass adds the lowest note an octave down");
    // fit to the notes
    add (40, 90);
    r.roll.fitNotes (false);
    ctx.check (r.roll.firstStep() == 0 && r.roll.visibleSteps() >= 41 && r.roll.rows() >= 90 - 52, "fit shows every note in time and pitch");
}

/** Scale lock: dragging a note up / down moves it a row (a scale step) and the rows never change mid-drag;
    ↑ / ↓ move a scale step, Shift an octave. */
MB_TEST (pianoroll_scale_lock_moves)
{
    RollRig r;
    r.engine.project().scale = { true, 0, 1, 0, false };   // C major, locked
    r.click (r.cell (2, 64));                               // E
    r.roll.createComponentSnapshot (r.roll.getLocalBounds());
    const float rowY = r.cell (0, 64).y - r.cell (0, 65).y;
    const auto from = r.cell (2, 64, 0.3f);
    r.roll.mouseDown (r.ev (from, {}, from));
    float lastTop = r.cell (0, 72).y;
    bool steady = true;
    for (int k = 1; k <= 8; ++k)
    {
        r.roll.mouseDrag (r.ev (from - juce::Point<float> (0.0f, rowY * 2.0f * k / 8.0f), {}, from));
        r.roll.createComponentSnapshot (r.roll.getLocalBounds());
        steady = steady && std::abs (r.cell (0, 72).y - lastTop) < 0.01f;
        for (auto& ref : r.roll.selection()) steady = steady && ref.note != 66 && ref.note != 68;   // never off the scale
    }
    r.roll.mouseUp (r.ev (from - juce::Point<float> (0.0f, rowY * 2.0f), {}, from));
    ctx.check (steady, "the rows stay put and the note never passes an off-scale pitch");
    ctx.check (r.tr().steps[2].hasNote (67), "two rows up: E to G");
    r.click (r.cell (2, 67, 0.3f));
    ctx.check (r.key (juce::KeyPress::upKey) && r.tr().steps[2].hasNote (69), "up: a scale step (G to A)");
    ctx.check (r.key (juce::KeyPress::downKey, kShift) && r.tr().steps[2].hasNote (57), "Shift+down: an octave");
}

/** The toolbar's transpose: ±1 is a scale step under scale lock (F Phrygian: F → G♭ → A♭), ±12 an octave. */
MB_TEST (pianoroll_transpose_buttons)
{
    RollRig r;
    r.engine.project().scale = { true, 5, 3, 0, false };   // F Phrygian, locked
    r.tr().steps[0].addNote (65);
    r.roll.transposeBy (1, false);
    ctx.check (r.tr().steps[0].hasNote (66), "+1: F to G flat");
    r.roll.transposeBy (1, false);
    ctx.check (r.tr().steps[0].hasNote (68), "+1: G flat to A flat");
    r.roll.transposeBy (-1, true);
    ctx.check (r.tr().steps[0].hasNote (56), "-12: an octave down");
    r.engine.project().scale.lock = false;
    r.roll.transposeBy (1, false);
    ctx.check (r.tr().steps[0].hasNote (57), "unlocked: +1 is a semitone");
}

/** The wheel: plainly it scrolls the notes (the rows keep their height, folded to the scale too); Ctrl over the keys
    zooms the rows, Ctrl over the grid (or the bar ruler) zooms time. */
MB_TEST (pianoroll_wheel_scrolls_and_zooms)
{
    for (bool fold : { true, false })
    {
        RollRig r;
        r.ctx.rollSettings.fold = r.ctx.rollSettings.scale = fold;
        for (int n = 60; n <= 74; n += 2) { ClipNote c; c.start = 0; c.length = 480; c.note = (uint8_t) n; r.tr().pianoRoll = true; r.tr().addClipNote (c); }
        r.roll.createComponentSnapshot (r.roll.getLocalBounds());   // lays the rows out
        auto wheel = [&] (juce::Point<float> p, float dy, juce::ModifierKeys mods = {})
        {
            juce::MouseWheelDetails w {}; w.deltaY = dy;
            r.roll.mouseWheelMove (r.ev (p, mods, p), w);
            r.roll.createComponentSnapshot (r.roll.getLocalBounds());
        };
        const juce::String what = fold ? "folded: " : "chromatic: ";
        const auto gridP = r.cell (4, 60), keysP = juce::Point<float> (20.0f, gridP.y);
        const int steps0 = r.roll.visibleSteps();
        const float h0 = r.roll.rowHeight();
        // Ctrl over the keys: taller rows (folded, all the rows fitted: now they don't, there is something to scroll)
        for (int k = 0; k < 3; ++k) wheel (keysP, 0.2f, juce::ModifierKeys::ctrlModifier);
        const float h1 = r.roll.rowHeight();
        ctx.check (h1 > h0 + 0.5f && r.roll.visibleSteps() == steps0, what + "Ctrl over the keys zooms the rows in (" + juce::String (h0, 1) + " -> " + juce::String (h1, 1) + ")");
        // the wheel alone: scrolls, the rows keep their height down to the last ones
        const int top0 = r.roll.noteAtRow (0);
        for (int k = 0; k < 12; ++k) wheel (gridP, -0.2f);   // down, past the end of the rows
        ctx.check (r.roll.noteAtRow (0) < top0 && std::abs (r.roll.rowHeight() - h1) < 0.01f && r.roll.visibleSteps() == steps0,
                   what + "the wheel scrolls down, no zoom (top " + juce::String (top0) + " -> " + juce::String (r.roll.noteAtRow (0))
                   + ", row height " + juce::String (h1, 1) + " -> " + juce::String (r.roll.rowHeight(), 1) + ")");
        // Ctrl over the grid: time
        wheel (r.cell (4, r.roll.noteAtRow (2)), 0.2f, juce::ModifierKeys::ctrlModifier);
        ctx.check (r.roll.visibleSteps() < steps0 && std::abs (r.roll.rowHeight() - h1) < 0.01f, what + "Ctrl over the grid zooms time in");
    }
}
