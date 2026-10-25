#include "SeqView.h"
#include <climits>

namespace mb::ui
{
using namespace geo;
using R = juce::Rectangle<float>;

namespace
{
// the track rows (one per instrument track, compact enough for eight), then the detail editor
constexpr float kBarY = 16, kBarH = 56, kRowsY = 84, kRowH = 30, kRowsH = kTracks * kRowH + 16, kDetailY = kRowsY + kRowsH + 12;

void dashedBox (juce::Graphics& g, R r, juce::Colour c)
{
    juce::Path p;
    p.addRoundedRectangle (r, 9.0f);
    const float dash[] = { 5.0f, 3.0f };
    juce::PathStrokeType (2.0f).createDashedStroke (p, p, dash, 2);
    g.setColour (c); g.fillPath (p);
}
}

SeqView::SeqView (AppContext& c) : ctx (c), roll (c)
{
    setTooltip ("Click a step to toggle it (drag to paint)   Drag values   Right-click: clear a value");
    roll.setTooltip ("Click: add a note (drag for its length) or select one   Drag: move (Ctrl: copy, Alt: off the grid)   Drag the ends: length   "
                     "Shift/Ctrl-drag: select   Ctrl+wheel: zoom   Alt+wheel: note height   Ruler: drag to scroll / zoom");
    addChildComponent (roll);
    fb.cursorFor = [this] (juce::Point<float> p)
    {
        const auto t = locate (p);
        return juce::MouseCursor (t.kind == Target::None ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    };
}

void SeqView::resized()
{
    const auto r = rollArea();
    roll.setBounds (juce::Rectangle<float> (M + 16, r.getY(), r.getRight() - (M + 16), r.getHeight()).toNearestInt());
}

bool SeqView::keyCommand (const juce::KeyPress& k) { return roll.isVisible() && roll.keyCommand (k); }

int SeqView::spb() const { return ctx.engine.project().stepsPerBar (ctx.focusPattern()); }
Track& SeqView::track (int t) { return ctx.engine.project().patterns[(size_t) ctx.focusPattern()].slots[(size_t) t].track; }

R SeqView::stepCell (R area, int i, int n, float gap) const
{
    const float cw = (area.getWidth() - gap * (n - 1)) / n;
    return { area.getX() + i * (cw + gap), area.getY(), cw, area.getHeight() };
}

R SeqView::rollArea() const
{
    const float dh = (float) getHeight() - kDetailY - M;
    // a clip has one velocity lane under it; steps viewed as a roll keep their four step lanes
    const bool clip = const_cast<SeqView*> (this)->track (ctx.focusTrack()).pianoRoll;
    const float lanesH = clip ? 90.0f + 6 : 4 * 40.0f;
    return { M + 16 + 64 + 8, kDetailY + 56, W - 2 * M - 32 - 64 - 8, dh - 56 - lanesH - 24 };
}

R SeqView::laneArea (int lane) const
{
    const auto roll = rollArea();
    return { roll.getX(), roll.getBottom() + 12 + lane * 40.0f, roll.getWidth(), 34 };
}

R SeqView::notesArea() const { return { M + 16 + 108, kDetailY + 60, W - 2 * M - 32 - 108, 190 }; }
R SeqView::stepLaneArea (int lane) const { const auto n = notesArea(); return { n.getX(), n.getBottom() + 18 + lane * 46.0f, n.getWidth(), 38 }; }
R SeqView::clipLaneArea() const { const auto r = rollArea(); return { r.getX(), r.getBottom() + 18, r.getWidth(), 90 }; }

bool SeqView::rollView() const
{
    auto* dev = ctx.engine.device (ctx.focusPattern(), ctx.focusTrack());
    if (dev == nullptr || dev->laneCount() > 0) return false;
    return const_cast<SeqView*> (this)->track (ctx.focusTrack()).pianoRoll || ! stepsView;
}

void SeqView::convertToPianoRoll()
{
    // steps → clip, one way (docs/sequencer.md); one undo step
    const int pat = ctx.focusPattern();
    auto& tr = track (ctx.focusTrack());
    if (tr.pianoRoll) return;
    ctx.snapshot (pat);
    {
        const Engine::ScopedEdit ed (ctx.engine);
        tr.convertToPianoRoll (ctx.engine.project().stepTicks (pat), ctx.engine.project().patternSteps (pat));
    }
    roll.clearSelection();
    ctx.markDirty();
    resized();
    repaint();
}

void SeqView::refresh()
{
    const int pat = ctx.focusPattern();
    if (ctx.focusTrack() != scrolledFor)
    {
        // open the piano roll on the track's notes
        scrolledFor = ctx.focusTrack();
        int hi = -1, lo = 128;
        for (auto& st : track (scrolledFor).steps)
            for (int k = 0; k < st.noteCount; ++k) { hi = std::max (hi, (int) st.notes[k]); lo = std::min (lo, (int) st.notes[k]); }
        const int rows = roll.rows();
        if (hi >= 0) roll.topNote = juce::jlimit (rows - 1, 127, std::max (hi + 2, lo + rows - 3));
        else roll.topNote = juce::jlimit (rows - 1, 127, ctx.lastNote[(size_t) scrolledFor] + 9);
        roll.clearSelection();
    }
    // the view follows the playing bar
    if (ctx.followPlayhead && ctx.engine.isPlaying() && ctx.engine.activePattern() == pat)
        ctx.viewBar = juce::jlimit (0, std::max (0, ctx.engine.project().patterns[(size_t) pat].lengthBars - 1), ctx.engine.currentStep() / std::max (1, spb()));
    roll.syncView();
    const int kind = track (ctx.focusTrack()).pianoRoll ? 1 : 0;
    if (kind != lastKind) { lastKind = kind; resized(); }   // the roll is taller over a clip (one lane under it)
    roll.setVisible (rollView());
    roll.validateSelection();
    auto& proj = ctx.engine.project();
    juce::String s;
    roll.appendSignature (s);
    s << (int) ctx.followPlayhead << (int) ctx.undoStack.size() << (int) ctx.redoStack.size() << drumLaneView;
    s << pat << ctx.focusTrack() << ctx.viewBar << (int) stepsView << kind << lockParam << drumLaneView << proj.patterns[(size_t) pat].lengthBars
      << proj.patterns[(size_t) pat].stepRes << (ctx.engine.isPlaying() ? ctx.engine.currentStep() : -1) << proj.recordQuantize << (int) proj.overdub
      << (ctx.engine.isRecording() && proj.recordMode == 1 ? ctx.engine.stepRecordCursor.load() : -1);
    const int first = ctx.viewBar * spb();
    for (int t = 0; t < kTracks; ++t)
    {
        auto& tr = track (t);
        s << (ctx.engine.device (pat, t) != nullptr ? 1 : 0) << (int) proj.patterns[(size_t) pat].slots[(size_t) t].mixer.mute
          << (int) proj.patterns[(size_t) pat].slots[(size_t) t].mixer.solo;
        for (int i = first; i < first + spb() && i < kMaxSteps; ++i)
        {
            const auto& st = tr.steps[(size_t) i];
            s << (int) st.noteCount;
            for (int k = 0; k < st.noteCount; ++k) s << (int) st.notes[k] << "," << (int) st.vel[k] << "," << (int) st.len[k];
            s << (int) st.microTiming << (int) st.flags
              << (int) st.probability << (int) st.ratchet;
            for (int l = 0; l < kLanes; ++l) { const auto& d = tr.lanes[(size_t) l][(size_t) i]; s << (int) d.on << (int) d.velocity << (int) d.probability << (int) d.ratchet << (int) d.microTiming; }
            for (auto& lk : tr.locks) s << lk.values[(size_t) i];
        }
        if (tr.pianoRoll) { s << "c" << (int) tr.clip.size(); for (auto& n : tr.clip) s << n.start + n.length * 3 + n.vel * 7 + n.note; }
    }
    if (s != sig) { sig = s; repaint(); roll.repaint(); }
}

void SeqView::paint (juce::Graphics& g)
{
    auto& proj = ctx.engine.project();
    const int pat = ctx.focusPattern(), sel = ctx.focusTrack();
    auto& pattern = proj.patterns[(size_t) pat];
    const int bars = pattern.lengthBars, steps = spb();
    ctx.viewBar = std::min (ctx.viewBar, bars - 1);
    const int first = ctx.viewBar * steps;
    const bool playingHere = ctx.engine.isPlaying() && ctx.engine.activePattern() == pat;
    const int play = playingHere ? ctx.engine.currentStep() : -1;
    const float x = M, w = W - 2 * M;
    fb.begin();
    // step recording: the cursor where the next note or chord is written
    const bool stepRec = ctx.engine.isRecording() && proj.recordMode == 1;
    const int cursor = stepRec ? ctx.engine.stepRecordCursor.load() % std::max (1, proj.patternSteps (pat)) : -1;

    // bar strip
    paint::card (g, { x, kBarY, w, kBarH });
    g.setColour (col::control); g.fillRoundedRectangle (x + 14, kBarY + 12, 120, 32, 16);
    paint::chevron (g, x + 30, kBarY + 28, 8, col::quiet, 'l');
    paint::text (g, juce::String (bars) + (bars == 1 ? " bar" : " bars"), x + 74, kBarY + 33, 13, Weight::Black, col::text, juce::Justification::horizontallyCentred);
    paint::chevron (g, x + 118, kBarY + 28, 8, col::quiet, 'r');
    fb.hot ({ x + 14, kBarY + 12, 36, 32 }, 16); fb.hot ({ x + 98, kBarY + 12, 36, 32 }, 16);
    for (auto& r : paint::segmented (g, { x + 148, kBarY + 12, 170, 32 }, { "1/8", "1/16", "1/32" }, pattern.stepRes, col::text, 12)) fb.hot (r.reduced (2));
    const float bx0 = x + 340, bw = (w - 340 - 16) / 16.0f;
    const int playBar = play >= 0 ? play / steps : -1;
    // the bars in the piano roll's view are lit
    const bool rollShown = roll.isVisible();
    const int viewFrom = roll.firstStep(), viewTo = viewFrom + roll.visibleSteps();
    for (int b = 0; b < 16; ++b)
    {
        const bool on = b < bars;
        const bool cur = rollShown ? (b * steps < viewTo && (b + 1) * steps > viewFrom && on) : b == ctx.viewBar;
        const R r (bx0 + b * bw + 2, kBarY + 14, bw - 4, 28);
        g.setColour (cur ? col::pattern.withAlpha (0.9f) : (on ? col::control : col::bg));
        g.fillRoundedRectangle (r, 8);
        if (on) fb.hot (r);
        paint::text (g, juce::String (b + 1), r.getCentreX(), kBarY + 33, 11, Weight::Black, cur ? col::bg : (on ? col::quiet : col::faint), juce::Justification::horizontallyCentred);
        if (b == playBar)
        {
            const float frac = (float) (play % steps + ctx.engine.patternProgress() * 0.0f) / (float) steps;
            g.setColour (cur ? col::bg : col::pattern); g.fillRoundedRectangle (r.getX(), r.getBottom() - 4, r.getWidth() * juce::jmax (0.08f, frac), 3, 1.5f);
        }
    }

    // track rows
    paint::card (g, { x, kRowsY, w, kRowsH });
    for (int t = 0; t < kTracks; ++t)
    {
        const float ry = kRowsY + 8 + t * kRowH;
        auto* dev = ctx.engine.device (pat, t);
        const auto& mixer = pattern.slots[(size_t) t].mixer;
        if (t == sel) { g.setColour (col::control.withAlpha (0.6f)); g.fillRoundedRectangle (x + 8, ry, w - 16, kRowH - 2, 8); }
        paint::trackBadge (g, x + 30, ry + 14, t, 10);
        paint::text (g, dev ? deviceTypeName (dev->type()) : juce::String ("Empty"), x + 48, ry + 19, 12, Weight::Black, t == sel ? col::text : col::quiet);
        paint::msButtons (g, x + 196, ry + 3, mixer.mute, mixer.solo, 22);
        // input: the selected track takes the keyboard and MIDI controllers
        if (t == sel)
        {
            const R inR (x + 164, ry + 3, 24, 22);
            g.setColour (col::track (t)); g.fillRoundedRectangle (inR, 6);
            paint::keysIcon (g, inR.getCentreX(), inR.getCentreY(), 12, col::bg);
        }
        fb.hot ({ x + 8, ry, 186, kRowH - 2 }, 8); fb.hot ({ x + 196, ry + 3, 22, 22 }, 6); fb.hot ({ x + 224, ry + 3, 22, 22 }, 6);
        const R stepsR (x + 280, ry + 3, w - 280 - 20, 22);
        auto& tr = track (t);
        if (tr.pianoRoll)
        {
            // a piano-roll track: its notes in miniature across the bar in view
            g.setColour (col::control); g.fillRoundedRectangle (stepsR, 8);
            fb.hot (stepsR, 8);
            const int stT = proj.stepTicks (pat), a = first * stT, b = (first + steps) * stT;
            int lo = 127, hi = 0;
            for (auto& n : tr.clip) { lo = std::min (lo, (int) n.note); hi = std::max (hi, (int) n.note); }
            for (auto& n : tr.clip)
            {
                if (n.end() <= a || n.start >= b) continue;
                const float x0 = stepsR.getX() + stepsR.getWidth() * (float) (std::max (n.start, a) - a) / (float) (b - a);
                const float x1 = stepsR.getX() + stepsR.getWidth() * (float) (std::min (n.end(), b) - a) / (float) (b - a);
                const float yy = stepsR.getBottom() - 6 - (stepsR.getHeight() - 12) * (hi > lo ? (n.note - lo) / (float) (hi - lo) : 0.5f);
                g.setColour (col::track (t).withAlpha (mixer.mute ? 0.35f : 0.5f + 0.5f * n.vel / 127.0f));
                g.fillRoundedRectangle (x0, yy - 1.5f, std::max (3.0f, x1 - x0 - 1), 3, 1.5f);
            }
            if (play >= first && play < first + steps)
            {
                const float px = stepsR.getX() + stepsR.getWidth() * (play - first) / (float) steps;
                g.setColour (col::pattern); g.drawLine (px, stepsR.getY(), px, stepsR.getBottom(), 1.5f);
            }
            continue;
        }
        for (int i = 0; i < steps; ++i)
        {
            const auto c = stepCell (stepsR, i, steps, 6);
            bool on = false;
            float vel = 0.8f;
            if (dev && dev->laneCount() > 0) { for (int l = 0; l < kLanes; ++l) if (tr.lanes[(size_t) l][(size_t) (first + i)].on) { on = true; } }
            else { const auto& st = tr.steps[(size_t) (first + i)]; on = st.noteCount > 0; vel = st.velocity() / 127.0f; }
            g.setColour (on ? col::track (t).withAlpha (mixer.mute ? 0.35f : 0.5f + 0.45f * vel) : ((i / 4) % 2 == 0 ? col::control : col::altCell));
            g.fillRoundedRectangle (c, 8);
            fb.hot (c);
            if (first + i == play) { g.setColour (col::pattern); g.drawRoundedRectangle (c.expanded (3), 10, 1.5f); }
            if (t == sel && first + i == cursor) dashedBox (g, c.expanded (3), col::record);
        }
    }

    // detail editor
    const R det (x, kDetailY, w, (float) getHeight() - kDetailY - M);
    paint::card (g, det);
    auto* dev = ctx.engine.device (pat, sel);
    const auto colour = col::track (sel);
    paint::trackBadge (g, x + 30, kDetailY + 28, sel, 12);
    paint::text (g, dev ? deviceTypeName (dev->type()) : juce::String ("Empty"), x + 52, kDetailY + 33, 15, Weight::Black, col::text);
    if (dev == nullptr) { fb.paint (g); return; }
    auto& tr = track (sel);
    const bool drum = dev->laneCount() > 0;
    // Follow (the view follows the playing bar), at the right end of the header
    const float fw = paint::pill (g, x + w - 16, kDetailY + 14, "Follow", 11, ctx.followPlayhead ? colour : col::control, ctx.followPlayhead ? col::bg : col::quiet, 26, 12,
                                  ctx.followPlayhead ? Weight::Black : Weight::Bold, false, juce::Justification::right);
    followR = { x + w - 16 - fw, kDetailY + 14, fw, 26 };
    fb.hot (followR, 13);
    toolZones.clear();

    if (drum)
    {
        for (auto& r : paint::segmented (g, { x + 160, kDetailY + 12, 360, 30 }, { "Vel", "Prob", "Ratchet", "Micro", "Cond" }, drumLaneView, colour, 12)) fb.hot (r.reduced (2));
        const R grid (x + 16 + 72, kDetailY + 56, w - 32 - 72, det.getHeight() - 56 - 110);
        const float lh = grid.getHeight() / 8.0f;
        const int selLane = ctx.selection (dev);
        for (int l = 0; l < 8; ++l)
        {
            const R row (grid.getX(), grid.getY() + l * lh, grid.getWidth(), lh - 4);
            paint::text (g, dev->laneName (l), grid.getX() - 10, row.getCentreY() + 4, 11, l == selLane ? Weight::Black : Weight::Bold, l == selLane ? col::text : col::quiet, juce::Justification::right);
            for (int i = 0; i < steps; ++i)
            {
                const auto c = stepCell (row, i, steps, 4);
                const auto& d = tr.lanes[(size_t) l][(size_t) (first + i)];
                g.setColour (d.on ? colour.withAlpha (0.4f + 0.6f * d.velocity / 127.0f) : ((i / 4) % 2 == 0 ? col::control : col::altCell));
                g.fillRoundedRectangle (c, 5);
                fb.hot (c, 5);
                if (d.on && d.ratchet > 1) paint::text (g, juce::String (d.ratchet), c.getCentreX(), c.getCentreY() + 4, 10, Weight::Black, col::bg, juce::Justification::horizontallyCentred);
                if (d.on && d.probability < 100) { g.setColour (col::bg.withAlpha (0.5f)); g.fillRect (c.getX() + 3, c.getBottom() - 4, (c.getWidth() - 6) * d.probability / 100.0f, 2.0f); }
            }
            if (l == selLane) { g.setColour (colour.withAlpha (0.5f)); g.drawRoundedRectangle (row.expanded (3, 1), 6, 1.0f); }
        }
        if (play >= first && play < first + steps) { const auto c = stepCell (grid, play - first, steps, 4); g.setColour (col::pattern); g.drawRoundedRectangle (c.expanded (2), 6, 1.5f); }
        if (cursor >= first && cursor < first + steps)
        {
            const auto c = stepCell (grid, cursor - first, steps, 4);
            g.setColour (col::record.withAlpha (0.08f)); g.fillRect (c.withY (grid.getY()).withHeight (grid.getHeight()));
            g.setColour (col::record); g.drawLine (c.getX(), grid.getY(), c.getX(), grid.getBottom(), 2.0f);
        }
        fb.paint (g);
        // lane values of the selected part
        const R lane (grid.getX(), det.getBottom() - 96, grid.getWidth(), 80);
        paint::text (g, dev->laneName (selLane), grid.getX() - 10, lane.getCentreY() + 4, 11, Weight::Black, col::text, juce::Justification::right);
        for (int i = 0; i < steps; ++i)
        {
            const auto c = stepCell (lane, i, steps, 4);
            const auto& d = tr.lanes[(size_t) selLane][(size_t) (first + i)];
            g.setColour (col::control); g.fillRoundedRectangle (c, 4);
            if (! d.on) continue;
            g.setColour (colour);
            if (drumLaneView == 4)
            {
                if (d.cond != 0) g.fillRoundedRectangle (c.reduced (0, 20), 6);
                paint::text (g, d.cond != 0 ? juce::String (trigConditionName (d.cond)) : juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")), c.getCentreX(), c.getCentreY() + 4, 10, Weight::Black, d.cond != 0 ? col::bg : col::faint, juce::Justification::horizontallyCentred);
            }
            else if (drumLaneView == 3)
            {
                const float mid = c.getCentreY(), off = d.microTiming / 24.0f * c.getHeight() * 0.5f;
                g.fillRoundedRectangle (c.getX(), std::min (mid, mid - off), c.getWidth(), std::max (2.0f, std::abs (off)), 2);
                g.setColour (col::faint); g.drawHorizontalLine ((int) mid, c.getX(), c.getRight());
            }
            else
            {
                const float v = drumLaneView == 0 ? d.velocity / 127.0f : (drumLaneView == 1 ? d.probability / 100.0f : d.ratchet / 4.0f);
                g.fillRoundedRectangle (c.getX(), c.getBottom() - c.getHeight() * v, c.getWidth(), c.getHeight() * v, 4);
            }
        }
        return;
    }

    // which kind of sequence: a step sequence (Steps, or viewed as a piano roll; converted one way) or a clip
    convertR = {};
    if (tr.pianoRoll)
    {
        paint::pill (g, x + 130, kDetailY + 13, "Piano roll", 12, colour, col::bg, 30, 12, Weight::Black);
    }
    else
    {
        for (auto& r : paint::segmented (g, { x + 130, kDetailY + 12, 170, 30 }, { "Steps", "Piano roll" }, stepsView ? 0 : 1, colour, 12)) fb.hot (r.reduced (2));
        const float pw = paint::pill (g, x + 308, kDetailY + 13, "Convert to roll", 11, col::control, col::quiet, 30, 12, Weight::Bold);
        convertR = { x + 308, kDetailY + 13, pw, 30 };
        fb.hot (convertR, 15);
    }
    if (! rollView())
    {
        paintStepsView (g, tr, *dev, colour, first, play, cursor);
        fb.paint (g);
        return;
    }
    paintToolbar (g, colour);
    if (tr.pianoRoll) { paintClipLane (g, tr, colour); fb.paint (g); return; }
    {
        // the piano roll itself is the PianoRoll child; this draws the lanes under it (zoomed with it)
        const auto roll = rollArea();
        const float kx = M + 16;
        const int vis = this->roll.visibleSteps(), total = proj.patternSteps (pat), first = this->roll.firstStep();
        const float cw = roll.getWidth() / vis;
        // lanes: velocity, accent / slide (Acid) or probability / ratchet, condition
        const bool acid = dev->type() == DeviceType::Acid;
        const char* names[4] = { "Velocity", acid ? "Accent" : "Prob", acid ? "Slide" : "Ratchet", "Condition" };
        for (int l = 0; l < 4; ++l)
        {
            const auto la = laneArea (l);
            paint::text (g, names[l], kx + 64, la.getCentreY() + 4, 11, Weight::Bold, col::quiet, juce::Justification::right);
            for (int i = 0; i < vis && first + i < total; ++i)
            {
                const auto& st = tr.steps[(size_t) (first + i)];
                const R c (la.getX() + i * cw + 2, la.getY(), cw - 4, la.getHeight());
                g.setColour (col::control); g.fillRoundedRectangle (c, 5);
                fb.hot (c, 5);
                if (st.noteCount == 0) continue;
                if (l == 3)
                {
                    if (st.cond != 0) { g.setColour (colour.withAlpha (0.85f)); g.fillRoundedRectangle (c.reduced (0, 3), 6); }
                    if (cw > 22) paint::text (g, st.cond != 0 ? juce::String (trigConditionName (st.cond)) : juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")), c.getCentreX(), c.getCentreY() + 4, 10, Weight::Black,
                                              st.cond != 0 ? col::bg : col::faint, juce::Justification::horizontallyCentred);
                    continue;
                }
                if (l == 0)
                {
                    // the selected note's velocity when one is selected on this step, else the loudest note's
                    int v127 = st.velocity();
                    for (auto& r : this->roll.selection()) if (r.step == first + i) { const int k = st.indexOf (r.note); if (k >= 0) { v127 = st.vel[k]; break; } }
                    const float v = v127 / 127.0f;
                    g.setColour (colour); g.fillRoundedRectangle (c.getX(), c.getBottom() - c.getHeight() * v, c.getWidth(), c.getHeight() * v, 5);
                }
                else if (acid)
                {
                    const bool on = l == 1 ? (st.flags & kAccent) != 0 : (st.flags & kSlide) != 0;
                    if (on) { g.setColour (colour); g.fillRoundedRectangle (c.reduced (0, 3), 6); paint::text (g, l == 1 ? "A" : "S", c.getCentreX(), c.getCentreY() + 4, 11, Weight::Black, col::bg, juce::Justification::horizontallyCentred); }
                }
                else
                {
                    const float v = l == 1 ? st.probability / 100.0f : st.ratchet / 4.0f;
                    g.setColour (colour.withAlpha (0.8f)); g.fillRoundedRectangle (c.getX(), c.getBottom() - c.getHeight() * v, c.getWidth(), c.getHeight() * v, 5);
                    if (l == 2 && st.ratchet > 1) paint::text (g, juce::String (st.ratchet), c.getCentreX(), c.getY() + 13, 10, Weight::Black, col::bg, juce::Justification::horizontallyCentred);
                }
            }
        }
        fb.paint (g);
    }
}

juce::String SeqView::laneName (int lane) const
{
    switch (lane)
    {
        case -1: return "Velocity";
        case DeviceProcessor::kBend: return "Pitch bend";
        case DeviceProcessor::kModWheel: return "Mod wheel";
        case DeviceProcessor::kExpression: return "Expression";
        case DeviceProcessor::kPressure: return "Pressure";
        default: break;
    }
    if (lane >= 100 && lane < 108)
    {
        auto* dev = ctx.focusDevice();
        return dev != nullptr ? const_cast<DeviceProcessor*> (dev)->macros()[(size_t) (lane - 100)].name : "Macro " + juce::String (lane - 99);
    }
    return "CC " + juce::String (lane);
}

void SeqView::paintClipLane (juce::Graphics& g, Track& tr, juce::Colour colour)
{
    // the clip's lane: velocity (a stalk per note; drag over them) or a controller (drawn on the grid)
    const auto la = clipLaneArea();
    const float lx = M + 16 + 64;
    const auto name = laneName (clipLane);
    paint::text (g, name, lx, la.getCentreY() + 4, name.length() > 9 ? 10.0f : 11.0f, Weight::Bold, col::quiet, juce::Justification::right);
    paint::chevron (g, lx - 22, la.getCentreY() + 16, 7, col::quiet, 'd');
    fb.hot ({ M + 16, la.getY(), 64, la.getHeight() }, 8);
    g.setColour (juce::Colour (0xff1B1D22)); g.fillRoundedRectangle (la, 8);
    fb.hot (la, 8);
    g.saveState();
    g.reduceClipRegion (la.toNearestInt());
    if (clipLane >= 0)
    {
        // a controller lane: the value held from each point to the next (bend is centred)
        const bool bipolar = clipLane == DeviceProcessor::kBend;
        auto yFor = [&] (float v) { const float n = bipolar ? (v + 1.0f) * 0.5f : v; return la.getBottom() - 4 - (la.getHeight() - 8) * n; };
        if (bipolar) { g.setColour (col::divider); g.drawHorizontalLine ((int) yFor (0.0f), la.getX(), la.getRight()); }
        std::vector<const CCPoint*> pts;
        for (auto& p : tr.cc) if (p.lane == clipLane) pts.push_back (&p);
        for (size_t k = 0; k < pts.size(); ++k)
        {
            const float x0 = roll.getX() + roll.xOfTick (pts[k]->tick);
            const float x1 = k + 1 < pts.size() ? roll.getX() + roll.xOfTick (pts[k + 1]->tick) : la.getRight();
            const float y = yFor (pts[k]->value);
            g.setColour (colour.withAlpha (0.25f));
            if (bipolar) g.fillRect (R (x0, std::min (y, yFor (0.0f)), x1 - x0, std::abs (y - yFor (0.0f))));
            else g.fillRect (R (x0, y, x1 - x0, la.getBottom() - y));
            g.setColour (colour);
            g.drawLine (x0, y, x1, y, 2.0f);
            if (k + 1 < pts.size()) g.drawLine (x1, y, x1, yFor (pts[k + 1]->value), 1.0f);
            g.fillEllipse (x0 - 3, y - 3, 6, 6);
        }
        if (pts.empty())
            paint::text (g, "Drag to draw " + laneName (clipLane).toLowerCase() + juce::String::fromUTF8 (" Â· right-drag erases"), la.getX() + 12, la.getCentreY() + 4, 10, Weight::Bold, col::faint);
        g.restoreState();
        return;
    }
    for (auto& n : tr.clip)
    {
        const float sx = roll.getX() + roll.xOfTick (n.start) + 2;
        if (sx < la.getX() - 4 || sx > la.getRight() + 4) continue;
        const float top = la.getBottom() - 4 - (la.getHeight() - 12) * n.vel / 127.0f;
        const bool sel = std::find (roll.selection().begin(), roll.selection().end(), PianoRoll::NoteRef { (int) n.id, n.note }) != roll.selection().end();
        g.setColour (n.muted ? col::faint : (sel ? col::text : colour));
        g.drawLine (sx, la.getBottom() - 4, sx, top, 2.0f);
        g.fillEllipse (sx - 4, top - 4, 8, 8);
    }
    g.restoreState();
}

void SeqView::paintStepsView (juce::Graphics& g, Track& tr, DeviceProcessor& dev, juce::Colour colour, int first, int play, int cursor)
{
    // the step sequencer of a melodic track (mockup 02_seq): the bar in view as note cells, a lane per setting
    const int steps = spb();
    const float lx = M + 16 + 100;
    // undo / redo beside Follow
    for (int k = 0; k < 2; ++k)
    {
        const R b (followR.getX() - 76 + k * 32, kDetailY + 14, 26, 26);
        const bool can = k == 0 ? ! ctx.undoStack.empty() : ! ctx.redoStack.empty();
        g.setColour (col::control); g.fillEllipse (b);
        paint::undoIcon (g, b.getCentreX(), b.getCentreY() - 1, 12, can ? col::text : col::faint, k == 1);
        toolZones.push_back ({ b, k == 0 ? TUndo : TRedo });
        fb.hot (b, 13);
    }
    const auto na = notesArea();
    paint::text (g, "Notes", lx, na.getCentreY() + 4, 11, Weight::Bold, col::quiet, juce::Justification::right);
    for (int i = 0; i < steps; ++i)
    {
        const auto c = stepCell (na, i, steps, 6);
        const auto& st = tr.steps[(size_t) (first + i)];
        const bool on = st.noteCount > 0;
        g.setColour (on ? colour.withAlpha (0.45f + 0.55f * st.velocity() / 127.0f) : ((i / 4) % 2 == 0 ? col::control : col::altCell));
        g.fillRoundedRectangle (c, 10);
        fb.hot (c, 10);
        if (on)
        {
            // the step's notes, highest first (a chord stacks)
            std::vector<int> ns (st.notes, st.notes + st.noteCount);
            std::sort (ns.rbegin(), ns.rend());
            const float lineH = std::min (18.0f, (c.getHeight() - 40) / (float) ns.size());
            float yy = c.getCentreY() - lineH * ns.size() / 2 + lineH * 0.75f;
            for (int n : ns)
            {
                const auto name = juce::MidiMessage::getMidiNoteName (n, true, true, 4);
                paint::text (g, name, c.getCentreX(), yy, ns.size() > 3 ? 11.0f : 14.0f, Weight::Black, col::bg, juce::Justification::horizontallyCentred);
                yy += lineH;
            }
            if (c.getWidth() > 30)
            {
                paint::chevron (g, c.getCentreX(), c.getY() + 16, 8, col::bg, 'u');
                paint::chevron (g, c.getCentreX(), c.getBottom() - 16, 8, col::bg, 'd');
            }
        }
        if (first + i == play) { g.setColour (col::pattern); g.drawRoundedRectangle (c.expanded (3), 12, 2.0f); }
        if (first + i == cursor) dashedBox (g, c.expanded (3), col::record);
    }
    const bool acid = dev.type() == DeviceType::Acid;
    const char* names[kStepLanes] = { "Velocity", "Gate", acid ? "Accent" : "Prob", acid ? "Slide" : "Ratchet", "Condition", "" };
    LockLane* locks = lockParam >= 0 ? tr.lockFor (lockParam, false) : nullptr;
    for (int l = 0; l < kStepLanes; ++l)
    {
        const auto la = stepLaneArea (l);
        if (l < 5) paint::text (g, names[l], lx, la.getCentreY() + 4, 11, Weight::Bold, col::quiet, juce::Justification::right);
        else
        {
            // the lock lane: pick its parameter
            const auto label = "Lock: " + (lockParam >= 0 && lockParam < dev.params().size() ? dev.params()[lockParam].name : juce::String ("choose"));
            const float pw = textWidth (label, 10) + 30;
            lockPickR = { lx - pw + 6, la.getY() + 6, pw, 26 };
            paint::pill (g, lockPickR.getX(), lockPickR.getY(), label, 10, col::control, col::quiet, 26, 8, Weight::Bold, true);
            fb.hot (lockPickR, 13);
        }
        for (int i = 0; i < steps; ++i)
        {
            const auto c = stepCell (la, i, steps, 6);
            const auto& st = tr.steps[(size_t) (first + i)];
            g.setColour (col::control); g.fillRoundedRectangle (c, 6);
            fb.hot (c, 6);
            if (l == 5)
            {
                if (locks == nullptr || std::isnan (locks->values[(size_t) (first + i)])) continue;
                const float v = dev.params()[lockParam].toNorm (locks->values[(size_t) (first + i)]);
                g.setColour (col::pattern); g.fillRoundedRectangle (c.getX(), c.getBottom() - c.getHeight() * v, c.getWidth(), c.getHeight() * v, 6);
                continue;
            }
            if (st.noteCount == 0) continue;
            g.setColour (colour);
            switch (l)
            {
                case 0: { const float v = st.velocity() / 127.0f; g.fillRoundedRectangle (c.getX(), c.getBottom() - c.getHeight() * v, c.getWidth(), c.getHeight() * v, 6); break; }
                case 1: { const float v = std::min (1.0f, st.maxLen() / 256.0f); g.setColour (colour.withAlpha (0.85f)); g.fillRoundedRectangle (c.getX(), c.getY() + 6, std::max (4.0f, c.getWidth() * v), c.getHeight() - 12, 5); break; }
                case 2: case 3:
                    if (acid)
                    {
                        const bool on = l == 2 ? (st.flags & kAccent) != 0 : (st.flags & kSlide) != 0;
                        if (on) { g.fillRoundedRectangle (c.reduced (0, 4), 6); paint::text (g, l == 2 ? "A" : "S", c.getCentreX(), c.getCentreY() + 4, 11, Weight::Black, col::bg, juce::Justification::horizontallyCentred); }
                    }
                    else
                    {
                        const float v = l == 2 ? st.probability / 100.0f : st.ratchet / 4.0f;
                        g.setColour (colour.withAlpha (0.8f)); g.fillRoundedRectangle (c.getX(), c.getBottom() - c.getHeight() * v, c.getWidth(), c.getHeight() * v, 6);
                        if (l == 3 && st.ratchet > 1) paint::text (g, juce::String (st.ratchet), c.getCentreX(), c.getY() + 14, 10, Weight::Black, col::bg, juce::Justification::horizontallyCentred);
                    }
                    break;
                case 4:
                    if (st.cond != 0) { g.setColour (colour.withAlpha (0.85f)); g.fillRoundedRectangle (c.reduced (0, 4), 6); }
                    if (c.getWidth() > 22) paint::text (g, st.cond != 0 ? juce::String (trigConditionName (st.cond)) : juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")), c.getCentreX(), c.getCentreY() + 4, 10, Weight::Black,
                                                        st.cond != 0 ? col::bg : col::faint, juce::Justification::horizontallyCentred);
                    break;
                default: break;
            }
        }
    }
    paint::text (g, juce::String::fromUTF8 ("Click: a step on / off  \xc2\xb7  Drag up / down: pitch (Shift: octave)  \xc2\xb7  Right-click: clear  \xc2\xb7  Piano roll: view and edit these steps as notes"),
                 notesArea().getX(), stepLaneArea (kStepLanes - 1).getBottom() + 26, 10, Weight::Bold, col::faint);
}

SeqView::Target SeqView::locate (juce::Point<float> p) const
{
    Target t;
    const float x = M, w = W - 2 * M;
    const int steps = spb();
    auto* dev = ctx.engine.device (ctx.focusPattern(), ctx.focusTrack());
    if (p.y >= kBarY && p.y < kBarY + kBarH)
    {
        if (p.x >= x + 14 && p.x < x + 50) t.kind = Target::BarLess;
        else if (p.x >= x + 98 && p.x < x + 134) t.kind = Target::BarMore;
        else if (p.x >= x + 148 && p.x < x + 318) { t.kind = Target::Res; t.a = juce::jlimit (0, 2, (int) ((p.x - x - 148) / (170.0f / 3))); }
        else if (p.x >= x + 340) { t.kind = Target::Bar; t.a = juce::jlimit (0, 15, (int) ((p.x - x - 340) / ((w - 340 - 16) / 16.0f))); }
        return t;
    }
    if (p.y >= kRowsY && p.y < kRowsY + kRowsH)
    {
        const int row = juce::jlimit (0, kTracks - 1, (int) ((p.y - kRowsY - 8) / kRowH));
        t.a = row;
        const float ry = kRowsY + 8 + row * kRowH;
        if (R (x + 196, ry + 3, 22, 22).contains (p)) t.kind = Target::RowMute;
        else if (R (x + 224, ry + 3, 22, 22).contains (p)) t.kind = Target::RowSolo;
        else if (p.x >= x + 280)
        {
            const R stepsR (x + 280, ry + 3, w - 280 - 20, 22);
            const float cw = (stepsR.getWidth() - 6.0f * (steps - 1)) / steps;
            t.kind = Target::RowStep; t.b = juce::jlimit (0, steps - 1, (int) ((p.x - stepsR.getX()) / (cw + 6)));
        }
        else t.kind = Target::RowSelect;
        return t;
    }
    if (p.y < kDetailY || dev == nullptr) return t;
    if (p.y < kDetailY + 48)
    {
        if (followR.contains (p)) { t.kind = Target::Follow; return t; }
        for (auto& [r, id] : toolZones) if (r.contains (p)) { t.kind = Target::Tool; t.a = id; return t; }
        if (dev->laneCount() > 0 && p.x >= x + 160 && p.x < x + 520) { t.kind = Target::DrumLaneView; t.a = juce::jlimit (0, 4, (int) ((p.x - x - 160) / 72.0f)); }
        else if (dev->laneCount() == 0 && convertR.contains (p)) t.kind = Target::Convert;
        else if (dev->laneCount() == 0 && ! const_cast<SeqView*> (this)->track (ctx.focusTrack()).pianoRoll && p.x >= x + 130 && p.x < x + 300)
        { t.kind = Target::ViewSwitch; t.a = p.x < x + 215 ? 0 : 1; }
        return t;
    }
    const float detH = (float) getHeight() - kDetailY - M;
    if (dev->laneCount() > 0)
    {
        const R grid (x + 16 + 72, kDetailY + 56, w - 32 - 72, detH - 56 - 110);
        const R lane (grid.getX(), kDetailY + detH - 96, grid.getWidth(), 80);
        const float cw = (grid.getWidth() - 4.0f * (steps - 1)) / steps;
        if (grid.contains (p)) { t.kind = Target::DrumCell; t.a = juce::jlimit (0, 7, (int) ((p.y - grid.getY()) / (grid.getHeight() / 8))); t.b = juce::jlimit (0, steps - 1, (int) ((p.x - grid.getX()) / (cw + 4))); }
        else if (p.x < grid.getX() && p.y >= grid.getY() && p.y < grid.getBottom()) { t.kind = Target::DrumCell; t.a = juce::jlimit (0, 7, (int) ((p.y - grid.getY()) / (grid.getHeight() / 8))); t.b = -1; }
        else if (lane.expanded (0, 8).contains (p)) { t.kind = Target::DrumLane; t.b = juce::jlimit (0, steps - 1, (int) ((p.x - lane.getX()) / (cw + 4))); }
        return t;
    }
    if (! rollView())
    {
        // the Steps view: note cells, lanes, the lock lane's parameter
        if (lockPickR.contains (p)) { t.kind = Target::LockParam; return t; }
        const auto na = notesArea();
        const float cw = (na.getWidth() - 6.0f * (steps - 1)) / steps;
        auto cellOf = [&] (R a) { return juce::jlimit (0, steps - 1, (int) ((p.x - a.getX()) / (cw + 6))); };
        if (na.expanded (0, 3).contains (p)) { t.kind = Target::StepNote; t.b = cellOf (na); return t; }
        for (int l = 0; l < kStepLanes; ++l)
            if (stepLaneArea (l).expanded (0, 3).contains (p)) { t.kind = Target::StepLane; t.a = l; t.b = cellOf (stepLaneArea (l)); return t; }
        return t;
    }
    if (const_cast<SeqView*> (this)->track (ctx.focusTrack()).pianoRoll)
    {
        if (clipLaneArea().expanded (0, 4).contains (p)) { t.kind = Target::ClipVel; t.b = 0; }
        else if (R (M + 16, clipLaneArea().getY(), 64, clipLaneArea().getHeight()).contains (p)) t.kind = Target::ClipLaneMenu;
        return t;
    }
    {
        const auto roll = rollArea();
        const int vis = this->roll.visibleSteps();
        const float cw = roll.getWidth() / vis;
        for (int l = 0; l < 4; ++l)
            if (laneArea (l).expanded (0, 3).contains (p))
            {
                t.kind = l == 0 ? Target::LaneVel : (l == 1 ? Target::LaneA : (l == 2 ? Target::LaneB : Target::LaneCond));
                t.b = juce::jlimit (0, vis - 1, (int) ((p.x - roll.getX()) / cw));
                if (this->roll.firstStep() + t.b >= ctx.engine.project().patternSteps (ctx.focusPattern())) t.kind = Target::None;
            }
        return t;
    }
}

void SeqView::applyDrag (const Target& t, juce::Point<float> p, bool right)
{
    const int pat = ctx.focusPattern(), sel = ctx.focusTrack();
    auto* dev = ctx.engine.device (pat, sel);
    if (dev == nullptr || t.b < 0) return;
    // the note lanes follow the piano roll's view; the other editors show the bar in view
    const bool rollLane = t.kind == Target::LaneVel || t.kind == Target::LaneA || t.kind == Target::LaneB;
    if (t.kind == Target::ClipVel)
    {
        if (clipLane >= 0) clipLaneDraw (p, right, false);
        else clipVelocityAt (p);
        ctx.markDirty(); repaint(); return;
    }
    const int si = (rollLane ? roll.firstStep() : ctx.viewBar * spb()) + t.b;
    auto& tr = track (sel);
    const Engine::ScopedEdit edit (ctx.engine);
    auto valueIn = [&p] (R area) { return juce::jlimit (0.0f, 1.0f, (area.getBottom() - p.y) / area.getHeight()); };
    switch (t.kind)
    {
        case Target::LaneVel:
        {
            // the selected notes on this step, or the whole step
            auto& st = tr.steps[(size_t) si];
            if (st.noteCount == 0) break;
            const int v = juce::jlimit (1, 127, juce::roundToInt (valueIn (laneArea (0)) * 127));
            bool any = false;
            for (auto& r : roll.selection()) if (r.step == si) { const int k = st.indexOf (r.note); if (k >= 0) { st.vel[k] = (uint8_t) v; any = true; } }
            if (! any) st.setVelocity (v);
            ctx.lastVel[(size_t) sel] = v;
            break;
        }
        case Target::LaneA:
            if (dev->type() != DeviceType::Acid && tr.steps[(size_t) si].noteCount > 0) tr.steps[(size_t) si].probability = (uint8_t) juce::roundToInt (valueIn (laneArea (1)) * 100);
            break;
        case Target::LaneB:
            if (dev->type() != DeviceType::Acid && tr.steps[(size_t) si].noteCount > 0) tr.steps[(size_t) si].ratchet = (uint8_t) juce::jlimit (1, 4, 1 + (int) (valueIn (laneArea (2)) * 3.99f));
            break;
        case Target::StepLane:
        {
            // the Steps view's lanes (velocity, gate, probability, ratchet, lock); accent / slide and conditions are clicks
            const float v = valueIn (stepLaneArea (t.a));
            auto& st = tr.steps[(size_t) si];
            const bool acid = dev->type() == DeviceType::Acid;
            if (t.a == 0 && st.noteCount > 0) { const int vel = juce::jlimit (1, 127, juce::roundToInt (v * 127)); st.setVelocity (vel); ctx.lastVel[(size_t) sel] = vel; }
            else if (t.a == 1 && st.noteCount > 0)
            {
                // gate: across the cell (a full cell = 2 steps)
                const auto c = stepCell (stepLaneArea (1), t.b, spb(), 6);
                const int len = juce::jlimit (8, 256, juce::roundToInt ((p.x - c.getX()) / c.getWidth() * 256));
                st.setLength (len);
                ctx.lastGate[(size_t) sel] = len;
            }
            else if (t.a == 2 && ! acid && st.noteCount > 0) st.probability = (uint8_t) juce::roundToInt (v * 100);
            else if (t.a == 3 && ! acid && st.noteCount > 0) st.ratchet = (uint8_t) juce::jlimit (1, 4, 1 + (int) (v * 3.99f));
            else if (t.a == 5 && lockParam >= 0)
                if (auto* lane = tr.lockFor (lockParam, true)) lane->values[(size_t) si] = right ? std::numeric_limits<float>::quiet_NaN() : dev->params()[lockParam].fromNorm (v);
            break;
        }
        case Target::ClipVel: clipVelocityAt (p); break;
        case Target::DrumLane:
        {
            auto& d = tr.lanes[(size_t) ctx.selection (dev)][(size_t) si];
            if (! d.on || drumLaneView == 4) break;   // conditions are chosen from a menu
            const float detH = (float) getHeight() - kDetailY - M;
            const R lane (M + 16 + 72, kDetailY + detH - 96, W - 2 * M - 32 - 72, 80);
            const float v = valueIn (lane);
            if (drumLaneView == 0) d.velocity = (uint8_t) juce::jlimit (1, 127, juce::roundToInt (v * 127));
            else if (drumLaneView == 1) d.probability = (uint8_t) juce::roundToInt (v * 100);
            else if (drumLaneView == 2) d.ratchet = (uint8_t) juce::jlimit (1, 4, 1 + (int) (v * 3.99f));
            else d.microTiming = (int8_t) juce::jlimit (-24, 24, juce::roundToInt ((v - 0.5f) * 48));
            break;
        }
        default: return;
    }
    ctx.markDirty();
    repaint();
}

void SeqView::mouseDown (const juce::MouseEvent& e)
{
    const auto t = locate (e.position);
    dragTarget = t;
    auto& proj = ctx.engine.project();
    const int pat = ctx.focusPattern();
    auto& pattern = proj.patterns[(size_t) pat];
    const int steps = spb();
    const bool right = e.mods.isRightButtonDown();
    // every edit is one undo step (a drag included)
    switch (t.kind)
    {
        case Target::BarLess: case Target::BarMore: case Target::Res: case Target::RowStep: case Target::LaneVel: case Target::LaneA:
        case Target::LaneB: case Target::StepLane: case Target::StepNote: case Target::ClipVel: case Target::DrumLane: ctx.snapshot (pat); break;
        case Target::DrumCell: if (t.b >= 0) ctx.snapshot (pat); break;
        default: break;
    }
    switch (t.kind)
    {
        // the length doubles and halves: 1 → 2 → 4 → 8 → 16 bars (growing repeats the bars into the new ones)
        case Target::BarLess: { const Engine::ScopedEdit ed (ctx.engine); pattern.lengthBars = std::max (1, pattern.lengthBars / 2); ctx.markDirty(); break; }
        case Target::BarMore: { const Engine::ScopedEdit ed (ctx.engine); proj.setLengthBars (pat, std::min (proj.maxBarsFor (pat), pattern.lengthBars * 2)); ctx.markDirty(); break; }
        case Target::Res:
        {
            const Engine::ScopedEdit ed (ctx.engine);
            pattern.stepRes = t.a;
            pattern.lengthBars = std::min (pattern.lengthBars, proj.maxBarsFor (pat));
            ctx.markDirty();
            break;
        }
        case Target::Bar:
            if (t.a >= pattern.lengthBars) break;
            if (right) { showBarMenu (t.a); break; }
            ctx.viewBar = t.a; ctx.lockStep = -1; ctx.followPlayhead = false;
            break;
        case Target::Tool: toolAction (t.a); break;
        case Target::LaneCond: showConditionMenu (ctx.focusTrack(), -1, roll.firstStep() + t.b); break;
        case Target::Follow: ctx.followPlayhead = ! ctx.followPlayhead; break;
        case Target::RowSelect: ctx.engine.focusTrack = t.a; break;
        case Target::RowMute: pattern.slots[(size_t) t.a].mixer.mute = ! pattern.slots[(size_t) t.a].mixer.mute; ctx.markDirty(); break;
        case Target::RowSolo: pattern.slots[(size_t) t.a].mixer.solo = ! pattern.slots[(size_t) t.a].mixer.solo; ctx.markDirty(); break;
        case Target::RowStep:
        {
            auto* dev = ctx.engine.device (pat, t.a);
            if (dev == nullptr) break;
            ctx.engine.focusTrack = t.a;
            const int si = ctx.viewBar * steps + t.b;
            auto& tr = track (t.a);
            if (tr.pianoRoll) { dragTarget = {}; break; }   // a piano-roll track: the row only selects it
            // the first cell decides: a drag across the row switches the others the same way
            paintOn = dev->laneCount() > 0 ? ! tr.lanes[(size_t) ctx.selection (dev)][(size_t) si].on : tr.steps[(size_t) si].noteCount == 0;
            paintStep (t.a, si);
            break;
        }
        case Target::ViewSwitch: stepsView = t.a == 0; roll.clearSelection(); break;
        case Target::ClipLaneMenu: showLaneMenu(); break;
        case Target::ClipVel:
            if (clipLane >= 0) clipLaneDraw (e.position, right, true);
            else clipVelocityAt (e.position);
            ctx.markDirty();
            break;
        case Target::Convert: convertToPianoRoll(); break;
        case Target::StepNote:
        {
            // a press on a note cell: on with the last note; on an on cell, drag for pitch, release to switch off
            auto& st = track (ctx.focusTrack()).steps[(size_t) (ctx.viewBar * steps + t.b)];
            if (right) { const Engine::ScopedEdit ed (ctx.engine); st.clear(); ctx.markDirty(); break; }
            noteWasOn = st.noteCount > 0;
            noteMoved = false;
            noteDownY = e.position.y;
            if (! noteWasOn)
            {
                const Engine::ScopedEdit ed (ctx.engine);
                const int tr = ctx.focusTrack();
                st.addNote (ctx.lastNote[(size_t) tr], ctx.lastVel[(size_t) tr], ctx.lastGate[(size_t) tr]);
                ctx.markDirty();
            }
            noteStepAtDown = st;
            previewNote = st.notes[0];
            ctx.engine.injectNote (ctx.focusTrack(), previewNote, 0.8f, true, pat, true);
            break;
        }
        case Target::StepLane:
        {
            auto* dev = ctx.focusDevice();
            const int si = ctx.viewBar * steps + t.b;
            auto& s = track (ctx.focusTrack()).steps[(size_t) si];
            if (t.a == 4) { if (s.noteCount > 0) showConditionMenu (ctx.focusTrack(), -1, si); break; }
            if (t.a == 5 && lockParam < 0) { dragTarget = {}; mouseDown (e.withNewPosition (lockPickR.getCentre())); return; }
            if ((t.a == 2 || t.a == 3) && dev != nullptr && dev->type() == DeviceType::Acid)
            {
                const Engine::ScopedEdit ed (ctx.engine);
                if (s.noteCount > 0) s.flags ^= (t.a == 2 ? kAccent : kSlide);
                ctx.markDirty();
                break;
            }
            applyDrag (t, e.position, right);
            break;
        }
        case Target::Quantize: proj.recordQuantize = proj.recordQuantize > 0.9f ? 0.75f : (proj.recordQuantize > 0.6f ? 0.5f : (proj.recordQuantize > 0.4f ? 0.0f : 1.0f)); ctx.markDirty(); break;
        case Target::Overdub: proj.overdub = ! proj.overdub; ctx.markDirty(); break;
        case Target::DrumLaneView: drumLaneView = t.a; break;
        case Target::DrumLane:
            if (drumLaneView == 4) { showConditionMenu (ctx.focusTrack(), ctx.selection (ctx.focusDevice()), ctx.viewBar * steps + t.b); break; }
            applyDrag (t, e.position, right);
            break;
        case Target::LockParam:
        {
            auto* dev = ctx.focusDevice();
            juce::PopupMenu m;
            auto& tr = track (ctx.focusTrack());
            for (auto& lk : tr.locks) if (lk.any()) m.addItem (1 + lk.param, dev->params()[lk.param].name + "  (locked)", true, lk.param == lockParam);
            m.addSeparator();
            for (int i = 0; i < dev->params().size() && i < 400; ++i)
            {
                const auto& info = dev->params()[i];
                m.addItem (1 + i, info.name + "  " + info.id, true, i == lockParam);
            }
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMaximumNumColumns (4), [this] (int id) { if (id > 0) { lockParam = id - 1; repaint(); } });
            break;
        }
        case Target::LaneA:
        case Target::LaneB:
        {
            auto* dev = ctx.focusDevice();
            if (dev != nullptr && dev->type() == DeviceType::Acid)
            {
                const Engine::ScopedEdit ed (ctx.engine);
                auto& s = track (ctx.focusTrack()).steps[(size_t) (roll.firstStep() + t.b)];
                if (s.noteCount > 0) s.flags ^= (t.kind == Target::LaneA ? kAccent : kSlide);
                ctx.markDirty();
            }
            else applyDrag (t, e.position, right);
            break;
        }
        case Target::DrumCell:
        {
            auto* dev = ctx.focusDevice();
            ctx.select (dev, t.a);
            if (t.b < 0) { ctx.engine.injectNote (ctx.focusTrack(), dev->laneNote (t.a), 0.85f, true, pat, true); previewNote = dev->laneNote (t.a); break; }
            paintOn = ! track (ctx.focusTrack()).lanes[(size_t) t.a][(size_t) (ctx.viewBar * steps + t.b)].on;
            paintDrum (t.a, ctx.viewBar * steps + t.b);
            break;
        }
        default: applyDrag (t, e.position, right); break;
    }
    repaint();
}

void SeqView::paintStep (int trackIndex, int si)
{
    auto* dev = ctx.engine.device (ctx.focusPattern(), trackIndex);
    if (dev == nullptr) return;
    const Engine::ScopedEdit ed (ctx.engine);
    auto& tr = track (trackIndex);
    if (dev->laneCount() > 0)
    {
        auto& d = tr.lanes[(size_t) ctx.selection (dev)][(size_t) si];
        if (d.on != paintOn) { d = DrumStep(); d.on = paintOn; }
    }
    else
    {
        auto& s = tr.steps[(size_t) si];
        if (! paintOn) s.clear();
        else if (s.noteCount == 0) s.addNote (ctx.lastNote[(size_t) trackIndex], ctx.lastVel[(size_t) trackIndex], ctx.lastGate[(size_t) trackIndex]);
    }
    ctx.markDirty();
}

void SeqView::paintDrum (int lane, int si)
{
    const Engine::ScopedEdit ed (ctx.engine);
    auto& d = track (ctx.focusTrack()).lanes[(size_t) lane][(size_t) si];
    if (d.on != paintOn) { d = DrumStep(); d.on = paintOn; }
    ctx.markDirty();
}

void SeqView::mouseDrag (const juce::MouseEvent& e)
{
    auto t = locate (e.position);
    // painting steps: the same row (or drum lane) as the press, across the cells the mouse passes
    if (dragTarget.kind == Target::RowStep && t.kind == Target::RowStep && t.a == dragTarget.a && t.b != dragTarget.b)
    {
        paintStep (t.a, ctx.viewBar * spb() + t.b); dragTarget.b = t.b; repaint(); return;
    }
    if (dragTarget.kind == Target::DrumCell && dragTarget.b >= 0 && t.kind == Target::DrumCell && t.a == dragTarget.a && t.b >= 0 && t.b != dragTarget.b)
    {
        paintDrum (t.a, ctx.viewBar * spb() + t.b); dragTarget.b = t.b; repaint(); return;
    }
    if (t.kind != dragTarget.kind) t = dragTarget;
    if (dragTarget.kind == Target::StepNote)
    {
        // pitch: a semitone every 10 px up / down (Shift: an octave every 30 px); every note of the step together
        const int semis = e.mods.isShiftDown() ? 12 * juce::roundToInt ((noteDownY - e.position.y) / 30.0f) : juce::roundToInt ((noteDownY - e.position.y) / 10.0f);
        if (semis != 0) noteMoved = true;
        auto& st = track (ctx.focusTrack()).steps[(size_t) (ctx.viewBar * spb() + dragTarget.b)];
        auto moved = noteStepAtDown;
        for (int i = 0; i < moved.noteCount; ++i) moved.notes[i] = (uint8_t) juce::jlimit (0, 127, moved.notes[i] + semis);
        if (moved.noteCount > 0 && moved.notes[0] != st.notes[0])
        {
            { const Engine::ScopedEdit ed (ctx.engine); st = moved; }
            ctx.lastNote[(size_t) ctx.focusTrack()] = moved.notes[0];
            if (previewNote >= 0) ctx.engine.injectNote (ctx.focusTrack(), previewNote, 0.0f, false, ctx.focusPattern(), true);
            previewNote = moved.notes[0];
            ctx.engine.injectNote (ctx.focusTrack(), previewNote, 0.8f, true, ctx.focusPattern(), true);
            ctx.markDirty();
            repaint();
        }
        return;
    }
    if (t.kind == Target::LaneVel || t.kind == Target::DrumLane || t.kind == Target::ClipVel
        || (t.kind == Target::StepLane && t.a != 4 && ! ((t.a == 2 || t.a == 3) && ctx.focusDevice() != nullptr && ctx.focusDevice()->type() == DeviceType::Acid))
        || ((t.kind == Target::LaneA || t.kind == Target::LaneB) && ctx.focusDevice() != nullptr && ctx.focusDevice()->type() != DeviceType::Acid))
    {
        if (dragTarget.kind == Target::StepLane)
        {
            // drawing across the cells of the lane the drag started in
            const auto la = stepLaneArea (dragTarget.a);
            const float cw = (la.getWidth() - 6.0f * (spb() - 1)) / spb();
            t = dragTarget;
            t.b = juce::jlimit (0, spb() - 1, (int) ((e.position.x - la.getX()) / (cw + 6)));
        }
        applyDrag (t, e.position, e.mods.isRightButtonDown());
    }
}

void SeqView::mouseUp (const juce::MouseEvent&)
{
    if (dragTarget.kind == Target::StepNote && noteWasOn && ! noteMoved)
    {
        // a click on an on step switches it off
        { const Engine::ScopedEdit ed (ctx.engine); track (ctx.focusTrack()).steps[(size_t) (ctx.viewBar * spb() + dragTarget.b)].clear(); }
        ctx.markDirty();
    }
    if (previewNote >= 0) { ctx.engine.injectNote (ctx.focusTrack(), previewNote, 0.0f, false, ctx.focusPattern(), true); previewNote = -1; }
    repaint();
    dragTarget = {};
}

// ------------------------------------------------------------------------------------------- toolbar
void SeqView::paintToolbar (juce::Graphics& g, juce::Colour colour)
{
    // the piano-roll toolbar (docs/sequencer.md, mockup 02_seq)
    const auto& rs = ctx.rollSettings;
    const float y = kDetailY;
    float tx = M + 428;
    auto zone = [this] (R r, int id) { toolZones.push_back ({ r, id }); fb.hot (r, std::min (13.0f, r.getHeight() / 2)); };
    auto divider = [&g] (float x) { g.setColour (col::divider); g.drawLine (x, kDetailY + 16, x, kDetailY + 38, 1.5f); };
    divider (tx - 9);
    // the tools: select, draw, paint, erase (svgkit.tool_switch)
    g.setColour (col::control); g.fillRoundedRectangle (tx, y + 12, 128, 30, 15);
    for (int i = 0; i < 4; ++i)
    {
        const R seg (tx + 2 + i * 31, y + 14, 31, 26);
        if (i == rs.tool) { g.setColour (colour); g.fillRoundedRectangle (seg, 13); }
        paint::toolIcon (g, seg.getCentreX(), seg.getCentreY(), i, i == rs.tool ? col::bg : col::quiet);
        zone (seg, TSelect + i);
    }
    tx += 136;
    auto menuPill = [&] (const juce::String& label, int id)
    {
        const float pw = paint::pill (g, tx, y + 14, label, 11, col::control, col::text, 26, 9, Weight::Bold, true);
        zone ({ tx, y + 14, pw, 26 }, id);
        tx += pw + 4;
    };
    menuPill (roll.gridLabel(), TGrid);
    const auto lengths = PianoRoll::lengthChoiceNames (ctx.engine.project().stepQuarters (ctx.focusPattern()));
    menuPill ("Length: " + (rs.lengthMode == 0 ? juce::String ("Last") : lengths[rs.lengthMode]), TLength);
    menuPill ("Quantize", TQuantize);
    menuPill ("Functions", TFunctions);
    tx += 8;
    divider (tx - 8);
    auto toggle = [&] (const juce::String& label, bool on, int id)
    {
        const float pw = paint::pill (g, tx, y + 14, label, 11, on ? colour : col::control, on ? col::bg : col::quiet, 26, 9, on ? Weight::Black : Weight::Bold);
        zone ({ tx, y + 14, pw, 26 }, id);
        tx += pw + 4;
    };
    toggle ("Scale", rs.scale, TScale);
    toggle ("Fold", rs.fold, TFold);
    toggle ("Ghosts", rs.ghosts, TGhosts);
    // zoom: out, in, the whole pattern
    for (int k = 0; k < 2; ++k)
    {
        const R b (tx + k * 32, y + 14, 26, 26);
        g.setColour (col::control); g.fillEllipse (b);
        if (k == 0) paint::minus (g, b.getCentreX(), b.getCentreY(), 10, col::text, 1.8f);
        else paint::plus (g, b.getCentreX(), b.getCentreY(), 10, col::text, 1.8f);
        zone (b, k == 0 ? TZoomOut : TZoomIn);
    }
    tx += 64;
    toggle ("Fit", false, TFit);
    toggle ("Loop", ctx.engine.project().patterns[(size_t) ctx.focusPattern()].loopOn, TLoop);
    tx += 10;
    divider (tx - 8);
    for (int k = 0; k < 2; ++k)
    {
        const R b (tx + k * 32, y + 14, 26, 26);
        const bool can = k == 0 ? ! ctx.undoStack.empty() : ! ctx.redoStack.empty();
        g.setColour (col::control); g.fillEllipse (b);
        paint::undoIcon (g, b.getCentreX(), b.getCentreY() - 1, 12, can ? col::text : col::faint, k == 1);
        zone (b, k == 0 ? TUndo : TRedo);
    }
    // transpose the selection (or every note): an octave, or a step (a scale step under scale lock)
    tx += 72;
    divider (tx - 8);
    g.setColour (col::control); g.fillRoundedRectangle (tx, y + 14, 100, 26, 13);
    const char* labels[4] = { "-12", "-1", "+1", "+12" };
    for (int i = 0; i < 4; ++i)
    {
        const R seg (tx + i * 25, y + 14, 25, 26);
        paint::textCentred (g, labels[i], seg, 10, Weight::Black, col::text);
        zone (seg.reduced (1), TTransDownOct + i);
    }
}

void SeqView::toolAction (int id)
{
    auto& rs = ctx.rollSettings;
    switch (id)
    {
        case TSelect: rs.tool = 0; break;
        case TDraw: rs.tool = 1; break;
        case TPaint: rs.tool = 2; break;
        case TErase: rs.tool = 3; break;
        case TLoop: roll.toggleLoop(); break;
        case TTransDownOct: roll.transposeBy (-1, true); break;
        case TTransDown: roll.transposeBy (-1, false); break;
        case TTransUp: roll.transposeBy (1, false); break;
        case TTransUpOct: roll.transposeBy (1, true); break;
        case TScale: rs.scale = ! rs.scale; break;
        case TFold: rs.fold = ! rs.fold; break;
        case TGhosts: rs.ghosts = ! rs.ghosts; break;
        case TZoomOut: roll.zoomTime (2.0); break;
        case TZoomIn: roll.zoomTime (0.5); break;
        case TFit:
        {
            juce::PopupMenu m;
            m.addItem (1, "The whole pattern\tCtrl+0");
            m.addItem (2, "The notes\tCtrl+Shift+0");
            m.addItem (3, "The selection\tCtrl+Shift+0", roll.hasSelection());
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
            {
                if (r == 1) roll.zoomToFit();
                if (r == 2 || r == 3) roll.fitNotes (r == 3);
                repaint();
            });
            return;
        }
        case TGrid:
        {
            // the snap value, triplets and snap on / off
            juce::PopupMenu m;
            const auto names = PianoRoll::gridNames();
            m.addSectionHeader ("Notes snap to");
            for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i] + (i == 0 ? " (follows the zoom)" : juce::String()), true, rs.snap && i == rs.grid);
            m.addSeparator();
            m.addItem (20, "Triplet", true, rs.triplet);
            m.addItem (22, "Dotted", true, rs.dotted);
            m.addItem (21, "Snap to grid\tCtrl+4", true, rs.snap);
            m.addSeparator();
            m.addSectionHeader ("Alt while dragging: off the grid");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
            {
                auto& s = ctx.rollSettings;
                if (r >= 1 && r <= 8) { s.grid = r - 1; s.snap = true; }
                if (r == 20) { s.triplet = ! s.triplet; if (s.triplet) s.dotted = false; }
                if (r == 22) { s.dotted = ! s.dotted; if (s.dotted) s.triplet = false; }
                if (r == 21) s.snap = ! s.snap;
                repaint(); roll.repaint();
            });
            return;
        }
        case TUndo: ctx.undo(); break;
        case TRedo: ctx.redo(); break;
        case TLength:
        {
            juce::PopupMenu m;
            const auto names = PianoRoll::lengthChoiceNames (ctx.engine.project().stepQuarters (ctx.focusPattern()));
            for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i], true, i == rs.lengthMode);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r) { if (r > 0) { ctx.rollSettings.lengthMode = r - 1; repaint(); roll.repaint(); } });
            return;
        }
        case TQuantize:
        {
            juce::PopupMenu m;
            const juce::String what = roll.hasSelection() ? " the selected notes" : " every note";
            m.addSectionHeader ("Quantize" + what + " to the grid (" + roll.gridLabel().fromFirstOccurrenceOf ("Grid: ", false, false) + ")");
            m.addItem (1, "Quantize 100%\tQ");
            m.addItem (2, "Quantize 75%");
            m.addItem (3, "Quantize 50%");
            m.addItem (4, "Quantize starts and lengths");
            m.addSeparator();
            m.addItem (5, "Humanize (timing and velocity)");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
            {
                if (r == 1) roll.quantize (1.0f, false);
                if (r == 2) roll.quantize (0.75f, false);
                if (r == 3) roll.quantize (0.5f, false);
                if (r == 4) roll.quantize (1.0f, true);
                if (r == 5) roll.humanize();
                repaint();
            });
            return;
        }
        case TFunctions:
        {
            juce::PopupMenu m, sel, notes, timing, vel, pattern;
            const bool has = roll.hasSelection();
            m.addSectionHeader (has ? "Selected notes" : "Every note (nothing selected)");
            m.addItem (1, "Cut\tCtrl+X", has); m.addItem (2, "Copy\tCtrl+C", has); m.addItem (3, "Paste\tCtrl+V");
            m.addItem (4, "Duplicate\tCtrl+D", has); m.addItem (5, "Delete\tDel", has);
            sel.addItem (10, "Select all\tCtrl+A"); sel.addItem (11, "Select none\tEsc"); sel.addItem (12, "Invert selection\tCtrl+Shift+A");
            m.addSubMenu ("Select", sel);
            notes.addItem (20, "Transpose up a semitone"); notes.addItem (21, "Transpose down a semitone");
            notes.addItem (22, "Transpose up an octave"); notes.addItem (23, "Transpose down an octave");
            notes.addSeparator();
            notes.addItem (24, "Fit to scale"); notes.addItem (25, "Invert (flip pitches)"); notes.addItem (26, "Mute / unmute\t0");
            notes.addItem (27, "Lock / unlock (no edits)\tCtrl+L");
            m.addSubMenu ("Pitch, mute and lock", notes);
            juce::PopupMenu chords;
            chords.addItem (60, "Select the chords of the selection", has);
            chords.addSeparator();
            chords.addItem (61, "Invert up (lowest note up an octave)"); chords.addItem (62, "Invert down (highest note down an octave)");
            chords.addItem (63, "Spread (open voicing)"); chords.addItem (64, "Close voicing"); chords.addItem (65, "Add the bass an octave below");
            m.addSubMenu ("Chords", chords);
            timing.addItem (30, "Legato"); timing.addItem (31, "Set length to the new-note length");
            timing.addItem (32, "Stretch x2"); timing.addItem (33, "Stretch /2"); timing.addItem (34, "Reverse");
            timing.addItem (35, "Chop into steps"); timing.addItem (36, "Glue touching notes"); timing.addItem (37, "Strum chords");
            m.addSubMenu ("Time and length", timing);
            vel.addItem (40, "Randomize"); vel.addItem (41, "Ramp up"); vel.addItem (42, "Ramp down");
            for (int v : { 127, 100, 80, 60, 40 }) vel.addItem (100 + v, "Set to " + juce::String (v));
            m.addSubMenu ("Velocity", vel);
            pattern.addItem (50, "Rotate left"); pattern.addItem (51, "Rotate right");
            pattern.addItem (52, "Insert a step"); pattern.addItem (53, "Delete a step");
            m.addSubMenu ("Track", pattern);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
            {
                switch (r)
                {
                    case 1: roll.copySelection (true); break;   case 2: roll.copySelection (false); break;
                    case 3: roll.paste(); break;                case 4: roll.duplicate(); break;
                    case 5: roll.deleteSelection(); break;
                    case 10: roll.selectAll(); break;           case 11: roll.clearSelection(); break;
                    case 12: roll.invertSelection(); break;
                    case 20: roll.transpose (1); break;         case 21: roll.transpose (-1); break;
                    case 22: roll.transpose (12); break;        case 23: roll.transpose (-12); break;
                    case 24: roll.fitToScale(); break;          case 25: roll.invertPitch(); break;
                    case 26: roll.toggleMute(); break;           case 27: roll.toggleLock(); break;
                    case 60: roll.selectChords(); break;        case 61: roll.chordInvert (true); break;
                    case 62: roll.chordInvert (false); break;   case 63: roll.chordSpread(); break;
                    case 64: roll.chordClose(); break;          case 65: roll.chordAddBass(); break;
                    case 30: roll.legato(); break;              case 31: roll.setLengthToNew(); break;
                    case 32: roll.stretch (2.0f); break;        case 33: roll.stretch (0.5f); break;
                    case 34: roll.reverse(); break;             case 35: roll.chop(); break;
                    case 36: roll.glue(); break;                case 37: roll.strum(); break;
                    case 40: roll.velocityRandomize(); break;   case 41: roll.velocityRamp (true); break;
                    case 42: roll.velocityRamp (false); break;
                    case 50: roll.rotate (-1); break;           case 51: roll.rotate (1); break;
                    case 52: roll.insertStep (true); break;     case 53: roll.insertStep (false); break;
                    default: if (r > 100) roll.velocitySet (r - 100); break;
                }
                repaint();
            });
            return;
        }
        default: break;
    }
    repaint();
    roll.repaint();
}

void SeqView::showLaneMenu()
{
    auto& tr = track (ctx.focusTrack());
    auto used = [&] (int lane) { for (auto& p : tr.cc) if (p.lane == lane) return true; return false; };
    juce::PopupMenu m;
    m.addSectionHeader ("The lane under the notes");
    m.addItem (1000, "Velocity", true, clipLane == -1);
    for (int lane : { (int) DeviceProcessor::kBend, (int) DeviceProcessor::kModWheel, (int) DeviceProcessor::kExpression, (int) DeviceProcessor::kPressure })
        m.addItem (1 + lane, laneName (lane) + (used (lane) ? juce::String::fromUTF8 ("  â¢") : juce::String()), true, clipLane == lane);
    juce::PopupMenu macros;
    for (int k = 0; k < 8; ++k) macros.addItem (1 + 100 + k, laneName (100 + k) + (used (100 + k) ? juce::String::fromUTF8 ("  â¢") : juce::String()), true, clipLane == 100 + k);
    m.addSubMenu ("Macros (automation)", macros);
    m.addSeparator();
    m.addItem (2000, "Clear this lane", clipLane >= 0 && used (clipLane));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
    {
        if (r == 1000) clipLane = -1;
        else if (r == 2000)
        {
            ctx.snapshot (ctx.focusPattern());
            const Engine::ScopedEdit ed (ctx.engine);
            track (ctx.focusTrack()).erasePoints (clipLane, 0, INT_MAX);
            ctx.markDirty();
        }
        else if (r > 0) clipLane = r - 1;
        repaint();
    });
}

void SeqView::clipLaneDraw (juce::Point<float> p, bool erase, bool start)
{
    // drawing a controller lane: a point on every grid line the drag passes (right-drag erases them)
    const auto la = clipLaneArea();
    const bool bipolar = clipLane == DeviceProcessor::kBend;
    float v = juce::jlimit (0.0f, 1.0f, (la.getBottom() - 4 - p.y) / (la.getHeight() - 8));
    if (bipolar) v = v * 2.0f - 1.0f;
    const int tick = std::max (0, roll.gridFloor (roll.tickOfX (p.x - roll.getX())));
    if (start) laneLastTick = tick;
    const int g = std::max (1, roll.gridTicks());
    const int a = std::min (tick, laneLastTick), b = std::max (tick, laneLastTick);
    auto& tr = track (ctx.focusTrack());
    const Engine::ScopedEdit ed (ctx.engine);
    if (erase) tr.erasePoints (clipLane, a, b + g);
    else for (int t = a; t <= b; t += g) tr.setPoint (clipLane, t, v);
    laneLastTick = tick;
}

void SeqView::clipVelocityAt (juce::Point<float> p)
{
    // the stalk nearest the mouse (within 10 px) takes the height; a selected one takes the selection with it
    auto& tr = track (ctx.focusTrack());
    const auto la = clipLaneArea();
    const int vel = juce::jlimit (1, 127, juce::roundToInt ((la.getBottom() - 4 - p.y) / (la.getHeight() - 12) * 127));
    ClipNote* best = nullptr;
    float bd = 10.0f;
    for (auto& n : tr.clip)
    {
        const float d = std::abs (roll.getX() + roll.xOfTick (n.start) + 2 - p.x);
        if (d < bd) { bd = d; best = &n; }
    }
    if (best == nullptr) return;
    const auto& sel = roll.selection();
    const bool inSel = std::find (sel.begin(), sel.end(), PianoRoll::NoteRef { (int) best->id, best->note }) != sel.end();
    const Engine::ScopedEdit ed (ctx.engine);
    if (inSel) { for (auto& r : sel) if (auto* n = tr.clipNote ((uint32_t) r.step)) n->vel = (uint8_t) vel; }
    else best->vel = (uint8_t) vel;
    ctx.lastVel[(size_t) ctx.focusTrack()] = vel;
}

void SeqView::showConditionMenu (int trackIndex, int lane, int step)
{
    // trig conditions: play on the Nth of every M passes, only the first pass, or all but the first
    auto& st = track (trackIndex);
    const int current = lane >= 0 ? st.lanes[(size_t) lane][(size_t) step].cond : st.steps[(size_t) step].cond;
    juce::PopupMenu m;
    m.addSectionHeader ("Step " + juce::String (step + 1) + " plays");
    for (int c = 0; c < kNumTrigConditions; ++c) m.addItem (c + 1, trigConditionName (c), true, c == current);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, trackIndex, lane, step] (int r)
    {
        if (r <= 0) return;
        ctx.snapshot (ctx.focusPattern());
        {
            const Engine::ScopedEdit ed (ctx.engine);
            auto& tr = track (trackIndex);
            if (lane >= 0) tr.lanes[(size_t) lane][(size_t) step].cond = (uint8_t) (r - 1);
            else tr.steps[(size_t) step].cond = (uint8_t) (r - 1);
        }
        ctx.markDirty();
        repaint();
    });
}

void SeqView::showBarMenu (int bar)
{
    // copy / paste / clear a whole bar, for the selected track or all four
    juce::PopupMenu m;
    m.addSectionHeader ("Bar " + juce::String (bar + 1));
    m.addItem (1, "Copy bar (selected track)");
    m.addItem (2, "Copy bar (all tracks)");
    m.addItem (3, "Paste bar", ! barClip.empty());
    m.addSeparator();
    m.addItem (4, "Clear bar (selected track)");
    m.addItem (5, "Clear bar (all tracks)");
    m.addItem (6, "Duplicate bar into the next");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, bar] (int r)
    {
        if (r <= 0) return;
        const int pat = ctx.focusPattern(), spb = this->spb(), sel = ctx.focusTrack();
        auto& pt = ctx.engine.project().patterns[(size_t) pat];
        auto copyBar = [&] (int t, BarClip& c)
        {
            auto& tr = pt.slots[(size_t) t].track;
            c.track = t;
            c.steps.assign (tr.steps.begin() + bar * spb, tr.steps.begin() + (bar + 1) * spb);
            for (int l = 0; l < kLanes; ++l) c.lanes[(size_t) l].assign (tr.lanes[(size_t) l].begin() + bar * spb, tr.lanes[(size_t) l].begin() + (bar + 1) * spb);
        };
        auto pasteBar = [&] (const BarClip& c, int t, int toBar)
        {
            if (toBar >= pt.lengthBars) return;
            auto& tr = pt.slots[(size_t) t].track;
            for (int i = 0; i < spb && i < (int) c.steps.size(); ++i)
            {
                tr.steps[(size_t) (toBar * spb + i)] = c.steps[(size_t) i];
                for (int l = 0; l < kLanes; ++l) tr.lanes[(size_t) l][(size_t) (toBar * spb + i)] = c.lanes[(size_t) l][(size_t) i];
            }
        };
        auto clearBar = [&] (int t)
        {
            auto& tr = pt.slots[(size_t) t].track;
            for (int i = bar * spb; i < (bar + 1) * spb; ++i) { tr.steps[(size_t) i].clear(); for (auto& l : tr.lanes) l[(size_t) i].clear(); }
        };
        if (r == 1 || r == 2)
        {
            barClip.clear();
            for (int t = 0; t < kTracks; ++t) if (r == 2 || t == sel) { barClip.emplace_back(); copyBar (t, barClip.back()); }
            return;
        }
        ctx.snapshot (pat);
        {
            const Engine::ScopedEdit ed (ctx.engine);
            if (r == 3) for (auto& c : barClip) pasteBar (c, barClip.size() == 1 ? sel : c.track, bar);
            if (r == 4) clearBar (sel);
            if (r == 5) for (int t = 0; t < kTracks; ++t) clearBar (t);
            if (r == 6) { BarClip c; copyBar (sel, c); pasteBar (c, sel, bar + 1); }
        }
        ctx.markDirty();
        repaint();
    });
}

} // namespace mb::ui
