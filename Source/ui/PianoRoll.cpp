#include "PianoRoll.h"
#include "seq/Scales.h"
#include "Shortcuts.h"
#include <climits>
#include <map>

namespace mb::ui
{
using R = juce::Rectangle<float>;
std::vector<PianoRoll::NoteData> PianoRoll::clipboard;

namespace
{
constexpr int kLengths[8] = { 0, 32, 64, 128, 256, 512, 1024, -1 };   // last used, ¼ … 8 steps, a bar (-1)
// grid values in quarter notes (0: adaptive, -1: a bar)
constexpr double kGridQuarters[8] = { 0.0, 1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2, 1.0, 2.0, -1.0 };
} // namespace

PianoRoll::PianoRoll (AppContext& c) : ctx (c)
{
    fb.keyHandler = [this] (int, const juce::KeyPress& k) { return cursorKey (k); };
    fb.cursorFor = [this] (juce::Point<float> p)
    {
        if (p.y < kRuler && p.x >= grid().getX()) return juce::MouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);
        if (p.y >= grid().getBottom()) return juce::MouseCursor (juce::MouseCursor::PointingHandCursor);
        if (p.x < kKeysW) return juce::MouseCursor (juce::MouseCursor::PointingHandCursor);
        const auto h = noteHit (p);
        if (h.zone == Zone::Start || h.zone == Zone::End) return juce::MouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        if (h.zone == Zone::Body) return juce::MouseCursor (juce::MouseCursor::DraggingHandCursor);
        return juce::MouseCursor (ctx.rollSettings.tool != 0 ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    };
}

Track& PianoRoll::track() { return ctx.engine.project().patterns[(size_t) pattern()].slots[(size_t) ctx.focusTrack()].track; }
int PianoRoll::stepsPerBar() const { return ctx.engine.project().stepsPerBar (pattern()); }
int PianoRoll::totalSteps() const { return ctx.engine.project().patternSteps (pattern()); }

// ------------------------------------------------------------------------------------------- view
int PianoRoll::visibleSteps() const
{
    const int spb = stepsPerBar();
    const int v = ctx.rollSettings.viewSteps > 0 ? ctx.rollSettings.viewSteps : spb;
    return std::clamp (v, 4, std::max (spb, totalSteps()));
}

int PianoRoll::firstStep() const { return std::clamp (ctx.rollSettings.start, 0, std::max (0, totalSteps() - visibleSteps())); }

void PianoRoll::syncView()
{
    if (ctx.viewBar == lastViewBar) return;
    // a bar picked elsewhere (bar strip, ← / →, follow): scroll to it unless it is in view already
    const int spb = stepsPerBar(), b0 = ctx.viewBar * spb, f = firstStep(), v = visibleSteps();
    if (b0 < f || b0 + std::min (spb, v) > f + v) ctx.rollSettings.start = b0;
    ctx.rollSettings.start = firstStep();
    lastViewBar = ctx.viewBar;
}

void PianoRoll::setFirstStep (int step)
{
    ctx.rollSettings.start = std::clamp (step, 0, std::max (0, totalSteps() - visibleSteps()));
    const int bars = std::max (1, ctx.engine.project().patterns[(size_t) pattern()].lengthBars);
    ctx.viewBar = std::clamp (firstStep() / std::max (1, stepsPerBar()), 0, bars - 1);
    lastViewBar = ctx.viewBar;
    repaint();
}

void PianoRoll::zoomTime (double factor, float aroundX)
{
    const auto g = grid();
    const int vis = visibleSteps();
    const float frac = aroundX < 0.0f ? 0.5f : juce::jlimit (0.0f, 1.0f, (aroundX - g.getX()) / g.getWidth());
    const double around = firstStep() + frac * vis;
    int nv = std::clamp ((int) std::lround (vis * factor), 4, std::max (stepsPerBar(), totalSteps()));
    if (nv == vis && factor != 1.0) nv = std::clamp (vis + (factor > 1.0 ? 1 : -1), 4, std::max (stepsPerBar(), totalSteps()));
    ctx.rollSettings.viewSteps = nv;
    setFirstStep ((int) std::lround (around - frac * nv));
}

void PianoRoll::zoomToFit()
{
    ctx.rollSettings.viewSteps = std::max (stepsPerBar(), totalSteps());
    setFirstStep (0);
}

void PianoRoll::zoomRows (int delta)
{
    ctx.rollSettings.rows = std::clamp (rows() + delta, 8, 48);
    topNote = std::clamp (topNote, rows() - 1, 127);
    repaint();
}

juce::Point<float> PianoRoll::pointFor (double step, int note) const
{
    const_cast<PianoRoll*> (this)->buildRows();
    const auto g = grid();
    const int row = ctx.rollSettings.fold || ctx.engine.project().scale.lock ? rowForNote (note) : topNote - note;
    return { g.getX() + (float) ((step - firstStep()) * cellW()), g.getY() + (row + 0.5f) * rowH() };
}

// ------------------------------------------------------------------------------------------- grid
juce::StringArray PianoRoll::gridNames() { return { "Adaptive", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar" }; }

int PianoRoll::gridTicks() const
{
    const auto& rs = ctx.rollSettings;
    const double tpq = kPPQ;   // ticks in a quarter note
    auto ticksFor = [&] (int i)
    {
        double t = kGridQuarters[i] < 0.0 ? (double) stepsPerBar() * stepT() : tpq * kGridQuarters[i];
        if (rs.triplet) t *= 2.0 / 3.0;
        else if (rs.dotted) t *= 1.5;
        return std::max (1, (int) std::lround (t));
    };
    if (rs.grid <= 0 || rs.grid > 7)
    {
        // adaptive: the finest value whose lines are at least 16 px apart
        for (int i = 1; i <= 7; ++i) if (ticksFor (i) / (float) stepT() * cellW() >= 16.0f) return ticksFor (i);
        return ticksFor (7);
    }
    return ticksFor (rs.grid);
}

juce::String PianoRoll::gridLabel() const
{
    const auto& rs = ctx.rollSettings;
    if (! rs.snap) return "Grid: off";
    auto name = gridNames()[std::clamp (rs.grid, 0, 7)];
    if (rs.triplet) name << (rs.grid == 0 ? " T" : "T");
    else if (rs.dotted) name << (rs.grid == 0 ? " D" : "D");
    return "Grid: " + name;
}

int PianoRoll::snapNearest (int tick, bool free) const
{
    if (free || ! ctx.rollSettings.snap) return tick;
    const int g = gridTicks();
    return (int) std::lround (tick / (double) g) * g;
}

int PianoRoll::snapFloor (int tick, bool free) const
{
    if (free || ! ctx.rollSettings.snap) return tick;
    const int g = gridTicks();
    return (int) std::floor (tick / (double) g) * g;
}

juce::StringArray PianoRoll::lengthChoiceNames (double stepQuarters)
{
    // musical names for the step-based lengths at this resolution (a quarter note = 1/4)
    auto name = [] (double quarters)
    {
        const double whole = quarters / 4.0;
        if (whole >= 1.0 - 1e-6) return juce::String (juce::roundToInt (whole)) + (whole > 1.5 ? " bars" : " bar");
        return "1/" + juce::String (juce::roundToInt (1.0 / whole));
    };
    juce::StringArray s { "Last used" };
    for (int i = 1; i < 7; ++i) s.add (name (stepQuarters * kLengths[i] / 128.0));
    s.add ("1 bar");
    return s;
}

int PianoRoll::newNoteLength() const
{
    const int m = ctx.rollSettings.lengthMode;
    if (m <= 0 || m > 7) return ctx.lastGate[(size_t) ctx.focusTrack()];
    return kLengths[m] < 0 ? stepsPerBar() * 128 : kLengths[m];
}

// ------------------------------------------------------------------------------------------- rows
void PianoRoll::buildRows()
{
    // while a note is dragged the rows stay as they were (a note passing a pitch must not add or remove rows)
    if (! rowNotes.empty() && (drag == Drag::Move || drag == Drag::ResizeEnd || drag == Drag::ResizeStart || drag == Drag::Create || drag == Drag::Paint)) return;
    rowNotes.clear();
    const auto& sc = ctx.engine.project().scale;
    const auto& facts = Scales::factoryScales();
    const auto pcs = Scales::pcSet (sc.root, facts[(size_t) std::clamp (sc.scale, 0, (int) facts.size() - 1)].intervals);
    if (ctx.rollSettings.fold != wasFolded) { rowOffset = 0; wasFolded = ctx.rollSettings.fold; }
    if (ctx.rollSettings.fold)
    {
        // only the pitches in use (plus, with Scale on, the scale's notes between them)
        std::array<bool, 128> used {};
        const auto& tr = track();
        int lo = 127, hi = 0;
        forEachNote (tr, [&] (const NoteRef&, const NoteData& d) { used[(size_t) d.note] = true; lo = std::min (lo, d.note); hi = std::max (hi, d.note); });
        if (lo > hi) { lo = std::max (0, ctx.lastNote[(size_t) ctx.focusTrack()] - 6); hi = std::min (127, lo + 12); for (int n = lo; n <= hi; ++n) used[(size_t) n] = true; }
        const int pad = ctx.rollSettings.scale ? 2 : 0;
        for (int n = std::min (127, hi + pad); n >= std::max (0, lo - pad); --n)
            if (used[(size_t) n] || (ctx.rollSettings.scale && pcs[(size_t) (n % 12)])) rowNotes.push_back (n);
        rowOffset = std::clamp (rowOffset, 0, std::max (0, (int) rowNotes.size() - 1));
        return;
    }
    topNote = std::clamp (topNote, rows() - 1, 127);
    if (sc.lock)
    {
        // scale lock: only the scale's notes are rows (plus any note already in the track that is off the scale)
        std::array<bool, 128> used {};
        const auto& tr = track();
        forEachNote (tr, [&] (const NoteRef&, const NoteData& d) { used[(size_t) d.note] = true; });
        for (int n = 127; n >= 0; --n) if (pcs[(size_t) (n % 12)] || used[(size_t) n]) rowNotes.push_back (n);
        rowOffset = 0;
        while (rowOffset < (int) rowNotes.size() - 1 && rowNotes[(size_t) rowOffset] > topNote) ++rowOffset;
        rowOffset = std::clamp (rowOffset, 0, std::max (0, (int) rowNotes.size() - rows()));
        return;
    }
    for (int n = 127; n >= 0; --n) rowNotes.push_back (n);
    rowOffset = 127 - topNote;
}

int PianoRoll::rowForNote (int note) const
{
    for (int r = 0; r < visibleRows(); ++r) if (rowNotes[(size_t) (rowOffset + r)] == note) return r;
    return -1;
}

int PianoRoll::noteForRow (int row) const
{
    if (rowNotes.empty()) return 60;
    row = std::clamp (row, 0, visibleRows() - 1);
    return rowNotes[(size_t) (rowOffset + row)];
}

R PianoRoll::noteRect (const NoteData& d) const
{
    const auto g = grid();
    const float cw = cellW(), rh = rowH();
    const int row = rowForNote (d.note);
    if (row < 0) return {};
    const float len = d.tie ? 1.0f : d.len / (float) stepT();
    const R r (xForTick (d.tick) + 1, g.getY() + row * rh + 2, std::max (6.0f, cw * len - 2), rh - 4);
    if (r.getRight() < g.getX() || r.getX() > g.getRight()) return {};
    return r;
}

template <class Fn> void PianoRoll::forEachNote (const Track& tr, Fn&& fn) const
{
    if (tr.pianoRoll)
    {
        for (auto& n : tr.clip)
        {
            NoteData d { n.start, n.note, n.vel, n.length, n.muted, n.flags };
            d.locked = n.locked;
            fn (NoteRef { (int) n.id, n.note }, d);
        }
        return;
    }
    for (int s = 0; s < totalSteps(); ++s)
        for (int i = 0; i < tr.steps[(size_t) s].noteCount; ++i)
        {
            NoteData d;
            if (noteData (tr, { s, tr.steps[(size_t) s].notes[i] }, d)) fn (NoteRef { s, d.note }, d);
        }
}

template <class Fn> void PianoRoll::forEachVisible (const Track& tr, Fn&& fn) const
{
    // every note that reaches into the view (a note can ring for many bars)
    const int endT = (firstStep() + visibleSteps() + 1) * stepT();
    forEachNote (tr, [&] (const NoteRef& ref, const NoteData& d)
    {
        if (d.tick >= endT) return;
        const auto r = noteRect (d);
        if (! r.isEmpty()) fn (ref, d, r);
    });
}

void PianoRoll::removeRef (Track& tr, const NoteRef& r)
{
    if (tr.pianoRoll) tr.removeClipNote ((uint32_t) r.step);
    else if (r.step >= 0 && r.step < kMaxSteps) tr.steps[(size_t) r.step].removeNote (r.note);
}

PianoRoll::Hit PianoRoll::noteHit (juce::Point<float> p) const
{
    Hit h;
    if (! grid().contains (p)) return h;
    forEachVisible (track(), [&] (const NoteRef& ref, const NoteData&, R r)
    {
        if (! r.expanded (2, 0).contains (p)) return;
        h.ref = ref;
        const float edge = std::min (8.0f, r.getWidth() * 0.3f);
        h.zone = p.x >= r.getRight() - edge ? Zone::End : (p.x <= r.getX() + edge && r.getWidth() > 18 ? Zone::Start : Zone::Body);
    });
    return h;   // the last drawn (topmost) note wins
}

int PianoRoll::stepAt (float x) const { return firstStep() + juce::jlimit (0, visibleSteps() - 1, (int) ((x - grid().getX()) / cellW())); }
int PianoRoll::pitchAt (float y) const { return noteForRow ((int) ((y - grid().getY()) / rowH())); }
bool PianoRoll::isSelected (int step, int note) const { return std::find (sel.begin(), sel.end(), NoteRef { step, note }) != sel.end(); }

void PianoRoll::validateSelection()
{
    auto& tr = track();
    sel.erase (std::remove_if (sel.begin(), sel.end(), [&] (const NoteRef& r)
    {
        if (tr.pianoRoll) return tr.clipNote ((uint32_t) r.step) == nullptr;
        return r.step < 0 || r.step >= kMaxSteps || ! tr.steps[(size_t) r.step].hasNote (r.note);
    }), sel.end());
}

void PianoRoll::appendSignature (juce::String& s) const
{
    const auto& rs = ctx.rollSettings;
    s << topNote << rowOffset << (int) drag << (int) sel.size() << rs.tool << rs.lengthMode << (int) rs.scale << (int) rs.fold << (int) rs.ghosts
      << visibleSteps() << ":" << firstStep() << ":" << rs.rows << rs.grid << (int) rs.triplet << (int) rs.dotted << (int) rs.snap
      << (int) ctx.engine.project().patterns[(size_t) pattern()].loopOn << ctx.engine.project().patterns[(size_t) pattern()].loopStart
      << ":" << ctx.engine.project().patterns[(size_t) pattern()].loopEnd << ":" << ctx.engine.currentStep();
    for (auto& r : sel) s << r.step << ":" << r.note;
}

void PianoRoll::preview (int note)
{
    stopPreview();
    previewNote = note;
    ctx.engine.injectNote (ctx.focusTrack(), note, 0.8f, true, pattern(), true);   // an audition: never recorded
}

void PianoRoll::stopPreview()
{
    if (previewNote >= 0) ctx.engine.injectNote (ctx.focusTrack(), previewNote, 0.0f, false, pattern(), true);
    previewNote = -1;
}

void PianoRoll::rememberFrom (const NoteRef& r)
{
    // the note last selected, drawn or resized sets what new notes get
    NoteData d;
    if (! noteData (track(), r, d)) return;
    const int t = ctx.focusTrack();
    ctx.lastGate[(size_t) t] = lenToSteps128 (d.len);
    ctx.lastVel[(size_t) t] = d.vel;
    ctx.lastNote[(size_t) t] = r.note;
}

// ------------------------------------------------------------------------------------------- notes
bool PianoRoll::noteData (const Track& tr, const NoteRef& r, NoteData& out) const
{
    if (tr.pianoRoll)
    {
        for (auto& n : tr.clip)
            if (n.id == (uint32_t) r.step) { out = { n.start, n.note, n.vel, n.length, n.muted, n.flags }; out.locked = n.locked; return true; }
        return false;
    }
    if (r.step < 0 || r.step >= kMaxSteps) return false;
    const auto& st = tr.steps[(size_t) r.step];
    const int i = st.indexOf (r.note);
    if (i < 0) return false;
    const int sT = stepT();
    out = { r.step * sT + (st.microTiming + st.off[i]) * sT / kTicksPerStep, r.note, st.vel[i], (int) std::lround (st.len[i] * sT / 128.0),
            st.isMuted (i), st.flags, st.probability, st.ratchet, st.cond, (st.flags & (kSlideFlag | kTieFlag)) != 0, st.isLocked (i) };
    return true;
}

PianoRoll::NoteRef PianoRoll::place (Track& tr, const NoteData& n)
{
    const int total = totalSteps(), sT = stepT(), unit = std::max (1, sT / kTicksPerStep);
    const int tick = std::clamp (n.tick, 0, total * sT - 1);
    if (tr.pianoRoll)
    {
        // a clip takes the note as it is
        ClipNote c;
        c.start = tick; c.length = std::clamp (n.len, 1, total * sT * 4); c.note = (uint8_t) std::clamp (n.note, 0, 127);
        c.vel = (uint8_t) std::clamp (n.vel, 1, 127); c.muted = n.muted; c.locked = n.locked; c.flags = (uint8_t) (n.flags & (kAccentFlag | kSlideFlag));
        return { (int) tr.addClipNote (c), c.note };
    }
    const int s0 = std::clamp ((int) std::floor (tick / (double) sT + 0.5), 0, total - 1);
    // the nearest step that can take the note within its ±½-step offset (a step holds each pitch once)
    int best = -1;
    for (int s : { s0, s0 - 1, s0 + 1 })
    {
        if (s < 0 || s >= total) continue;
        const auto& d = tr.steps[(size_t) s];
        if (d.hasNote (n.note) || d.noteCount >= 6) continue;
        const int off = (int) std::lround ((tick - s * sT) / (double) unit) - (d.noteCount > 0 ? d.microTiming : 0);
        if (off >= -24 && off <= 24) { best = s; break; }
    }
    if (best < 0) best = s0;
    auto& d = tr.steps[(size_t) best];
    if (d.noteCount == 0)
    {
        // a note landing on an empty step brings its step's accent, slide, probability and condition
        d.microTiming = 0; d.flags = n.flags; d.probability = n.prob; d.ratchet = n.ratchet; d.cond = n.cond;
    }
    d.addNote (n.note, std::clamp (n.vel, 1, 127), std::clamp (lenToSteps128 (n.len), 1, std::min (kMaxNoteLen, (total - best) * 128)));
    const int i = d.indexOf (n.note);
    if (i >= 0)
    {
        d.off[i] = (int8_t) std::clamp ((int) std::lround ((tick - best * sT) / (double) unit) - d.microTiming, -24, 24);
        d.setMuted (i, n.muted);
        d.setLocked (i, n.locked);
    }
    return { best, n.note };
}

void PianoRoll::addNoteAt (int tick, int note)
{
    const int t = ctx.focusTrack();
    ctx.snapshot (pattern());
    {
        const Engine::ScopedEdit ed (ctx.engine);
        createdRef = place (track(), { tick, note, ctx.lastVel[(size_t) t], (int) std::lround (newNoteLength() * stepT() / 128.0), false });
    }
    refStart = tick;
    sel = { createdRef };
    ctx.lastNote[(size_t) t] = note;
    preview (note);
}

// ------------------------------------------------------------------------------------------- paint
void PianoRoll::paintRuler (juce::Graphics& g)
{
    const auto gr = grid();
    const auto& proj = ctx.engine.project();
    const int spb = stepsPerBar(), vis = visibleSteps(), first = firstStep();
    const float cw = cellW();
    g.setColour (juce::Colour (0xff1B1D22)); g.fillRoundedRectangle (gr.getX(), 0, gr.getWidth(), kRuler - 2, 5);
    // the zoom, in bars
    const double bars = vis / (double) spb;
    const juce::String zoom = bars >= 1.0 ? juce::String (bars, std::abs (bars - std::round (bars)) < 0.01 ? 0 : 1) + (bars < 1.5 ? " bar" : " bars")
                                          : "1/" + juce::String (juce::roundToInt (1.0 / bars)) + " bar";
    paint::text (g, zoom, kKeysW - 8, 13, 10, Weight::Bold, col::faint, juce::Justification::right);
    const double beatSteps = spb / (double) std::max (1, proj.beats);
    const float beatPx = (float) (beatSteps * cw);
    g.saveState();
    g.reduceClipRegion (R (gr.getX(), 0, gr.getWidth(), kRuler).toNearestInt());
    for (int b = first / spb; b * spb < first + vis + spb; ++b)
        for (int k = 0; k < proj.beats; ++k)
        {
            const double st = b * spb + k * beatSteps;
            if (st < first - spb || st > first + vis) continue;
            const float x = gr.getX() + (float) ((st - first) * cw);
            const bool bar = k == 0;
            if (! bar && beatPx < 8) continue;
            g.setColour (bar ? col::quiet : col::divider);
            g.drawLine (x, bar ? 2.0f : 9.0f, x, kRuler - 2, bar ? 1.5f : 1.0f);
            if (bar || beatPx >= 34)
                paint::text (g, bar ? juce::String (b + 1) : juce::String (b + 1) + "." + juce::String (k + 1), x + 5, 13, 10,
                             bar ? Weight::Black : Weight::Bold, bar ? col::text : col::quiet);
        }
    // the loop brace and the play position
    const auto& pt = proj.patterns[(size_t) pattern()];
    if (pt.loopEnd > pt.loopStart)
    {
        const float x0 = juce::jlimit (gr.getX(), gr.getRight(), xForTick ((double) pt.loopStart * stepT()));
        const float x1 = juce::jlimit (gr.getX(), gr.getRight(), xForTick ((double) pt.loopEnd * stepT()));
        if (x1 > x0) { g.setColour (col::track (ctx.focusTrack()).withAlpha (pt.loopOn ? 1.0f : 0.3f)); g.fillRoundedRectangle (x0, 1, x1 - x0, 5, 2.5f); }
    }
    const int at = ctx.engine.currentStep();
    if (at >= first && at < first + vis)
    {
        const float px = gr.getX() + (at - first) * cw;
        juce::Path tri;
        tri.addTriangle (px - 5, kRuler - 9, px + 5, kRuler - 9, px, kRuler - 2);
        g.setColour (ctx.engine.isPlaying() ? col::pattern : col::quiet); g.fillPath (tri);
    }
    g.restoreState();
}

void PianoRoll::toggleLoop()
{
    auto& pt = ctx.engine.project().patterns[(size_t) pattern()];
    const Engine::ScopedEdit ed (ctx.engine);
    if (! pt.loopOn && ! (pt.loopEnd > pt.loopStart && pt.loopStart < totalSteps()))
    {
        // no range yet: the bars in view
        const int spb = stepsPerBar();
        pt.loopStart = firstStep() / spb * spb;
        pt.loopEnd = std::min (totalSteps(), std::max (pt.loopStart + spb, (firstStep() + visibleSteps() + spb - 1) / spb * spb));
    }
    pt.loopOn = ! pt.loopOn;
    ctx.markDirty();
    repaint();
}

void PianoRoll::paint (juce::Graphics& g)
{
    buildRows();
    syncView();
    fb.begin();
    const auto gr = grid();
    const float rh = rowH(), cw = cellW();
    const int steps = visibleSteps(), first = firstStep(), spb = stepsPerBar(), total = totalSteps();
    const auto colour = col::track (ctx.focusTrack());
    const auto& proj = ctx.engine.project();
    const auto& facts = Scales::factoryScales();
    const auto pcs = Scales::pcSet (proj.scale.root, facts[(size_t) std::clamp (proj.scale.scale, 0, (int) facts.size() - 1)].intervals);
    paintRuler (g);
    for (int r = 0; r < visibleRows(); ++r)
    {
        const int note = noteForRow (r);
        const bool black = juce::MidiMessage::isMidiNoteBlack (note);
        const float yy = gr.getY() + r * rh;
        const R key (0, yy + 1, kKeysW, rh - 2);
        g.setColour (note == previewNote ? colour : (black ? col::bg : col::control)); g.fillRoundedRectangle (key, 3);
        // labels: the scale's root when a scale is in use (lock or Scale highlight), else C
        const int labelPc = proj.scale.lock || ctx.rollSettings.scale ? proj.scale.root : 0;
        if ((note % 12 == labelPc || ctx.rollSettings.fold) && rh >= 9)
            paint::text (g, juce::MidiMessage::getMidiNoteName (note, true, true, 4), kKeysW - 8, yy + rh / 2 + 4, std::min (10.0f, rh - 1), Weight::Bold, col::quiet, juce::Justification::right);
        fb.hot (key, 3, Feedback::kHover);
        g.setColour (black ? juce::Colour (0xff1C1E23) : col::bg); g.fillRect (R (gr.getX(), yy, gr.getWidth(), rh));
        if (ctx.rollSettings.scale && pcs[(size_t) (note % 12)])   // the project scale's rows, the root stronger
        {
            g.setColour (colour.withAlpha (note % 12 == proj.scale.root ? 0.16f : 0.07f));
            g.fillRect (R (gr.getX(), yy, gr.getWidth(), rh));
        }
        if (proj.scale.lock && ! pcs[(size_t) (note % 12)])   // locked to a scale: a row kept only for notes off it
        {
            g.setColour (col::record.withAlpha (0.08f));
            g.fillRect (R (gr.getX(), yy, gr.getWidth(), rh));
        }
    }
    // grid lines at the grid value; beats and bars stronger
    {
        const int gt = gridTicks(), sT = stepT(), barT = spb * sT;
        const double beatT = barT / (double) std::max (1, proj.beats);
        const int t0 = first * sT, t1 = (first + steps) * sT;
        if (gt / (float) sT * cw >= 4.0f)
            for (int t = (t0 / gt) * gt; t <= t1; t += gt)
            {
                if (t < t0) continue;
                const float x = xForTick (t);
                g.setColour (col::divider.withAlpha (0.75f));
                g.drawLine (x, gr.getY(), x, gr.getBottom(), 0.8f);
            }
        if (beatT / sT * cw >= 4.0)
            for (double t = std::floor (t0 / beatT) * beatT; t <= t1 + 0.5; t += beatT)
            {
                if (t < t0 - 0.5) continue;
                const bool bar = std::abs (std::fmod (t, (double) barT)) < 0.5;
                const float x = xForTick (t);
                g.setColour (col::divider);
                g.drawLine (x, gr.getY(), x, gr.getBottom(), bar ? 2.0f : 1.4f);
            }
        for (int b = (first + spb - 1) / spb; b * spb <= first + steps; ++b)
        {
            const float x = xForTick ((double) b * barT);
            g.setColour (col::divider); g.drawLine (x, gr.getY(), x, gr.getBottom(), 2.0f);
        }
    }
    if (first + steps > total)   // beyond the pattern's end
    {
        g.setColour (col::bg.withAlpha (0.6f));
        g.fillRect (R (gr.getX() + (total - first) * cw, gr.getY(), (first + steps - total) * cw, gr.getHeight()));
    }
    g.saveState();
    g.reduceClipRegion (gr.toNearestInt());
    if (ctx.rollSettings.ghosts)   // the other melodic tracks of this pattern, faintly
        for (int t = 0; t < kTracks; ++t)
        {
            auto* d = ctx.engine.device (pattern(), t);
            if (t == ctx.focusTrack() || d == nullptr || d->laneCount() > 0) continue;
            g.setColour (col::track (t).withAlpha (0.22f));
            forEachVisible (proj.patterns[(size_t) pattern()].slots[(size_t) t].track, [&] (const NoteRef&, const NoteData&, R nr) { g.fillRoundedRectangle (nr, 4); });
        }
    forEachVisible (track(), [&] (const NoteRef& ref, const NoteData& d, R nr)
    {
        const bool selected = isSelected (ref.step, ref.note), muted = d.muted;
        const float rad = std::min (4.0f, nr.getHeight() / 2);
        g.setColour (muted ? col::faint.withAlpha (0.6f) : colour.withAlpha (0.45f + 0.55f * d.vel / 127.0f));
        g.fillRoundedRectangle (nr, rad);
        if (nr.getWidth() > 10 && nr.getHeight() > 8)
        {
            g.setColour (col::bg.withAlpha (0.55f));
            g.drawLine (nr.getRight() - 4, nr.getY() + 3, nr.getRight() - 4, nr.getBottom() - 3, 1.5f);
        }
        if (selected) { g.setColour (col::text); g.drawRoundedRectangle (nr.expanded (1.5f), rad + 1, 2.0f); }
        if (d.locked && nr.getWidth() > 14 && nr.getHeight() > 9) paint::lockIcon (g, nr.getX() + 7, nr.getCentreY(), std::min (9.0f, nr.getHeight() - 2), col::bg);
        fb.hot (nr, rad, Feedback::kHover);
    });
    // playhead and the step-record cursor
    const bool playingHere = ctx.engine.isPlaying() && ctx.engine.activePattern() == pattern();
    const int play = playingHere ? ctx.engine.currentStep() : -1;
    if (play >= first && play < first + steps)
    {
        const float px = gr.getX() + (play - first) * cw;
        g.setColour (col::pattern); g.drawLine (px, gr.getY(), px, gr.getBottom(), 2.0f);
    }
    const int cursor = ctx.engine.isRecording() && proj.recordMode == 1 ? ctx.engine.stepRecordCursor.load() % std::max (1, total) : -1;
    if (cursor >= first && cursor < first + steps)
    {
        const float cx = gr.getX() + (cursor - first) * cw;
        g.setColour (col::record.withAlpha (0.08f)); g.fillRect (R (cx, gr.getY(), cw, gr.getHeight()));
        g.setColour (col::record); g.drawLine (cx, gr.getY(), cx, gr.getBottom(), 2.0f);
        paint::pill (g, cx + 4, gr.getY() + 4, "REC", 9, col::record, col::bg, 18, 7, Weight::Black);
    }
    if (drag == Drag::Marquee && ! marquee.isEmpty())
    {
        g.setColour (col::text.withAlpha (0.08f)); g.fillRect (marquee);
        g.setColour (col::text.withAlpha (0.6f)); g.drawRect (marquee, 1.0f);
    }
    if (showCursorColumn && keyCursor.step >= first && keyCursor.step < first + steps)   // the cursor's step, for the lanes under the roll
    {
        g.setColour (col::text.withAlpha (0.07f));
        g.fillRect (R (gr.getX() + (keyCursor.step - first) * cw, gr.getY(), cw, gr.getHeight()));
    }
    if (Feedback::navFocus().fb == &fb)   // the keyboard cursor
    {
        const int row = rowForNote (keyCursor.note);
        if (row >= 0 && keyCursor.step >= first && keyCursor.step < first + steps)
        {
            const R c (gr.getX() + (keyCursor.step - first) * cw, gr.getY() + row * rh, cw, rh);
            g.setColour (col::text.withAlpha (0.12f)); g.fillRect (c);
            g.setColour (col::text); g.drawRect (c, 1.5f);
        }
    }
    g.restoreState();
    // the scroll bar: the part of the pattern in view
    const auto tr = scrollTrack();
    g.setColour (col::control); g.fillRoundedRectangle (tr, 3);
    g.setColour (col::quiet); g.fillRoundedRectangle (scrollThumb(), 3);
    fb.hot (gr, 0.0f, Feedback::kNav);   // the keyboard cursor target (docs/keyboard.md)
    fb.paint (g);
}

R PianoRoll::scrollThumb() const
{
    const auto tr = scrollTrack();
    const float span = (float) std::max (totalSteps(), visibleSteps());
    return { tr.getX() + tr.getWidth() * firstStep() / span, tr.getY(), std::max (12.0f, tr.getWidth() * visibleSteps() / span), tr.getHeight() };
}

// ------------------------------------------------------------------------------------------- mouse
void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    buildRows();
    moved = false;
    drag = Drag::None;
    anchor = p;
    const auto gr = grid();
    const bool shift = e.mods.isShiftDown(), cmd = e.mods.isCommandDown() || e.mods.isCtrlDown(), alt = e.mods.isAltDown();
    if (p.y < kRuler && p.x >= gr.getX())
    {
        if (shift)
        {
            // Shift-drag in the ruler: the loop range (whole steps), and loop playback on
            drag = Drag::LoopSet;
            loopAnchor = std::clamp ((int) std::lround (snapNearest (tickAt (p.x), false) / (double) stepT()), 0, totalSteps());
            return;
        }
        // the ruler: drag sideways to scroll, up / down to zoom around the point; a click moves the playhead
        drag = Drag::Ruler;
        visAtDown = visibleSteps();
        stepAtDown = firstStep() + (p.x - gr.getX()) / cellW();
        return;
    }
    if (p.y >= gr.getBottom())
    {
        if (p.x < gr.getX()) return;
        if (scrollThumb().expanded (0, 4).contains (p)) { drag = Drag::Scroll; viewAtDown = firstStep(); return; }
        setFirstStep (firstStep() + (p.x < scrollThumb().getX() ? -visibleSteps() : visibleSteps()));   // a page
        return;
    }
    if (p.x < kKeysW)
    {
        // the keyboard: a click auditions; Shift-click selects every note of that pitch
        const int note = pitchAt (p.y);
        if (shift)
        {
            const auto& tr = track();
            forEachNote (tr, [&] (const NoteRef& ref, const NoteData&) { if (ref.note == note && ! isSelected (ref.step, note)) sel.push_back (ref); });
            repaint();
            return;
        }
        drag = Drag::Keys; preview (note); repaint();
        return;
    }
    const auto h = noteHit (p);
    if (e.mods.isPopupMenu() && ! cmd)
    {
        if (h.zone != Zone::None)
        {
            if (! isSelected (h.ref.step, h.ref.note)) sel = { h.ref };
            deleteSelection();
        }
        return;
    }
    before = track();
    anchorTick = tickAt (p.x);
    const int tool = ctx.rollSettings.tool;
    if (tool == 3 && ! shift && ! cmd)
    {
        // Erase: every note the pointer touches goes (the stroke is one undo step)
        ctx.snapshot (pattern());
        erased = false;
        drag = Drag::Erase;
        lastErase = p;
        eraseAt (p);
        return;
    }
    if (h.zone != Zone::None)
    {
        // Shift: a click toggles the note in the selection (on release), a drag moves with the pitch kept
        shiftToggle = shift && h.zone == Zone::Body;
        toggleRef = h.ref;
        dragRef = h.ref;
        wasSelected = isSelected (h.ref.step, h.ref.note);
        if (shiftToggle) { if (! wasSelected) sel.push_back (h.ref); }
        else if (! wasSelected) sel = { h.ref };
        selBefore = sel;
        NoteData nd;
        if (noteData (before, dragRef, nd)) { refStart = nd.tick; refEnd = nd.tick + nd.len; }
        rememberFrom (h.ref);
        ctx.snapshot (pattern());
        drag = h.zone == Zone::End ? Drag::ResizeEnd : (h.zone == Zone::Start ? Drag::ResizeStart : Drag::Move);
        copyDrag = drag == Drag::Move && cmd;
        juce::ignoreUnused (alt);
        preview (h.ref.note);
        repaint();
        return;
    }
    // empty space: the Select tool draws a selection box (Shift adds); the Draw tool adds a note (Ctrl / Shift: box)
    if (ctx.rollSettings.tool == 0 || shift || cmd)
    {
        drag = Drag::Marquee;
        selBefore = shift ? sel : std::vector<NoteRef>();
        if (! shift) sel.clear();
        marquee = {};
        repaint();
        return;
    }
    const int tick = snapFloor (anchorTick, alt);   // a new note starts on the grid line at or before the click
    if (tick >= totalTicks()) return;
    if (tool == 2)
    {
        // Paint: a run of notes along the drag, one per grid unit (one undo step)
        ctx.snapshot (pattern());
        paintTick = paintPitch = -1;
        drag = Drag::Paint;
        sel.clear();
        paintAt (p, alt);
        return;
    }
    addNoteAt (std::max (0, tick), pitchAt (p.y));
    drag = Drag::Create;
    before = track();
    repaint();
}

void PianoRoll::eraseAt (juce::Point<float> p)
{
    // everything along the path since the last event (fast strokes skip no notes)
    const float dist = lastErase.getDistanceFrom (p);
    const int n = std::max (1, (int) (dist / 3.0f));
    for (int i = 0; i <= n; ++i)
    {
        const auto q = lastErase + (p - lastErase) * (i / (float) n);
        const auto h = noteHit (q);
        NoteData d;
        if (h.zone == Zone::None || ! noteData (track(), h.ref, d) || d.locked) continue;
        {
            const Engine::ScopedEdit ed (ctx.engine);
            removeRef (track(), h.ref);
        }
        sel.erase (std::remove (sel.begin(), sel.end(), h.ref), sel.end());
        erased = true;
    }
    lastErase = p;
    repaint();
}

void PianoRoll::paintAt (juce::Point<float> p, bool free)
{
    const int tick = std::max (0, snapFloor (tickAt (p.x), free)), pitch = pitchAt (p.y);
    if (tick >= totalTicks() || (tick == paintTick && pitch == paintPitch)) return;
    // a fast stroke fills the grid units it jumped over (on the same row)
    if (paintTick >= 0 && pitch == paintPitch && ctx.rollSettings.snap && ! free)
    {
        const int g = gridTicks(), dir = tick > paintTick ? 1 : -1;
        for (int t = paintTick + dir * g; dir * (tick - t) > 0; t += dir * g) { paintTick = t; paintNote (t, pitch, free); }
    }
    paintTick = tick; paintPitch = pitch;
    paintNote (tick, pitch, free);
}

void PianoRoll::paintNote (int tick, int pitch, bool free)
{
    bool there = false;
    forEachNote (track(), [&] (const NoteRef&, const NoteData& d) { there = there || (d.note == pitch && d.tick == tick); });
    if (there) return;
    // as long as the new-note length, at most a grid unit (the run keeps its rhythm)
    int len = (int) std::lround (newNoteLength() * stepT() / 128.0);
    if (ctx.rollSettings.snap && ! free) len = std::min (len, gridTicks());
    const int t = ctx.focusTrack();
    {
        const Engine::ScopedEdit ed (ctx.engine);
        sel.push_back (place (track(), { tick, pitch, ctx.lastVel[(size_t) t], std::max (1, len), false }));
    }
    preview (pitch);
    repaint();
}

void PianoRoll::autoScroll (float x)
{
    // dragging past either edge scrolls the view
    const auto gr = grid();
    const int stepBy = std::max (1, visibleSteps() / 16);
    if (x > gr.getRight() - 4) setFirstStep (firstStep() + stepBy);
    else if (x < gr.getX() + 2) setFirstStep (firstStep() - stepBy);
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (e.getDistanceFromDragStart() > 3) moved = true;
    const bool free = e.mods.isAltDown();
    const auto gr = grid();
    switch (drag)
    {
        case Drag::Ruler:
        {
            const int span = std::max (stepsPerBar(), totalSteps());
            const double nv = juce::jlimit (4.0, (double) span, visAtDown * std::pow (1.012, (double) (anchor.y - p.y)));   // down: zoom in
            ctx.rollSettings.viewSteps = (int) std::lround (nv);
            setFirstStep ((int) std::lround (stepAtDown - (p.x - gr.getX()) / gr.getWidth() * visibleSteps()));
            return;
        }
        case Drag::LoopSet:
        {
            const int s = std::clamp ((int) std::lround (snapNearest (tickAt (p.x), false) / (double) stepT()), 0, totalSteps());
            auto& pt = ctx.engine.project().patterns[(size_t) pattern()];
            const Engine::ScopedEdit ed (ctx.engine);
            pt.loopStart = std::min (loopAnchor, s); pt.loopEnd = std::max (loopAnchor, s);
            if (pt.loopEnd > pt.loopStart) pt.loopOn = true;
            ctx.markDirty();
            repaint();
            return;
        }
        case Drag::Erase: autoScroll (p.x); eraseAt (p); return;
        case Drag::Paint: autoScroll (p.x); paintAt (p, free); return;
        case Drag::Scroll:
        {
            const float span = (float) std::max (totalSteps(), visibleSteps());
            setFirstStep (viewAtDown + (int) std::lround ((p.x - anchor.x) / scrollTrack().getWidth() * span));
            return;
        }
        case Drag::Keys: { const int n = pitchAt (p.y); if (n != previewNote) preview (n); repaint(); return; }
        case Drag::Marquee:
        {
            autoScroll (p.x);
            const float x0 = juce::jlimit (gr.getX(), gr.getRight(), xForTick (anchorTick));
            marquee = R (juce::Point<float> (x0, anchor.y), p).getIntersection (gr);
            sel = selBefore;
            forEachVisible (track(), [&] (const NoteRef& ref, const NoteData&, R r) { if (r.intersects (marquee) && ! isSelected (ref.step, ref.note)) sel.push_back (ref); });
            repaint();
            return;
        }
        case Drag::Create:
        {
            if (! moved) return;
            // the new note's length follows the mouse: its end on the nearest grid line (Alt: free)
            autoScroll (p.x);
            const int minT = free || ! ctx.rollSettings.snap ? 3 : gridTicks();
            const int endT = std::max (refStart + minT, snapNearest (tickAt (p.x), free));
            const int lt = std::max (1, std::min (endT, totalTicks()) - refStart);
            {
                const Engine::ScopedEdit ed (ctx.engine);
                auto& tr = track();
                if (tr.pianoRoll) { if (auto* n = tr.clipNote ((uint32_t) createdRef.step)) n->length = lt; }
                else
                {
                    auto& st = tr.steps[(size_t) createdRef.step];
                    const int i = st.indexOf (createdRef.note);
                    if (i >= 0) st.len[i] = (uint16_t) lenToSteps128 (lt);
                }
            }
            ctx.lastGate[(size_t) ctx.focusTrack()] = lenToSteps128 (lt);
            repaint();
            return;
        }
        case Drag::Move:
        {
            if (! moved) return;
            autoScroll (p.x);
            // the dragged note's start snaps to the nearest grid line; the others keep their distance to it
            const int newStart = snapNearest (refStart + tickAt (p.x) - anchorTick, free);
            const int dPitch = e.mods.isShiftDown() ? 0 : (int) std::round ((anchor.y - p.y) / rowH());
            applyMove (newStart - refStart, dPitch, copyDrag);
            return;
        }
        case Drag::ResizeEnd: case Drag::ResizeStart:
            autoScroll (p.x);
            applyResize (tickAt (p.x) - anchorTick, drag == Drag::ResizeEnd, free);
            return;
        case Drag::None: return;
    }
}

void PianoRoll::mouseUp (const juce::MouseEvent& e)
{
    stopPreview();
    if (drag == Drag::Ruler && ! moved)
        ctx.engine.seek (std::clamp ((int) (tickAt (e.position.x) / stepT()), 0, totalSteps() - 1));   // a click on the ruler: the playhead goes there
    if (drag == Drag::Erase) { if (erased) ctx.markDirty(); else if (! ctx.undoStack.empty()) ctx.undoStack.pop_back(); }
    if (drag == Drag::Paint) ctx.markDirty();
    if ((drag == Drag::ResizeEnd || drag == Drag::ResizeStart || drag == Drag::Move) && ! sel.empty()) rememberFrom (sel.front());
    if (drag == Drag::Create) rememberFrom (createdRef);
    if (drag == Drag::Move && ! moved && shiftToggle)
    {
        if (wasSelected) sel.erase (std::remove (sel.begin(), sel.end(), toggleRef), sel.end());
    }
    else if (drag == Drag::Move && ! moved && ! copyDrag && ! selBefore.empty())
    {
        const auto h = noteHit (anchor);   // a plain click on a note of a multi-selection selects just that note
        if (h.zone != Zone::None) sel = { h.ref };
    }
    shiftToggle = false;
    if (drag == Drag::Create || drag == Drag::Move || drag == Drag::ResizeEnd || drag == Drag::ResizeStart) ctx.markDirty();
    // a gesture that changed nothing leaves no undo step
    if ((drag == Drag::Move || drag == Drag::ResizeEnd || drag == Drag::ResizeStart) && ! moved && ! ctx.undoStack.empty()) ctx.undoStack.pop_back();
    drag = Drag::None;
    marquee = {};
    repaint();
}

void PianoRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto gr = grid();
    if (e.position.y < kRuler) { zoomToFit(); return; }   // double-click the ruler: the whole pattern
    if (! gr.contains (e.position)) return;
    const auto h = noteHit (e.position);
    if (h.zone != Zone::None)
    {
        // the second press already started a move: end that gesture so its release changes (and undoes) nothing
        if (drag == Drag::Move && ! ctx.undoStack.empty()) ctx.undoStack.pop_back();
        drag = Drag::None;
        sel = { h.ref };
        deleteSelection();
        return;
    }
    // the Select tool adds a note with a double-click
    if (ctx.rollSettings.tool == 0)
    {
        const int tick = snapFloor (tickAt (e.position.x), e.mods.isAltDown());
        if (tick < totalTicks()) { addNoteAt (std::max (0, tick), pitchAt (e.position.y)); stopPreview(); ctx.markDirty(); repaint(); }
    }
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : w.deltaX;
    const int dir = d > 0 ? 1 : -1;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { zoomTime (dir > 0 ? 0.8 : 1.25, e.position.x); return; }   // time zoom around the mouse
    if (e.mods.isAltDown()) { zoomRows (dir > 0 ? -2 : 2); return; }                                                 // note height
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        setFirstStep (firstStep() - dir * std::max (1, visibleSteps() / 16));   // through time
        ctx.followPlayhead = false;
        return;
    }
    if (ctx.rollSettings.fold) rowOffset = std::max (0, rowOffset - dir * 2);
    else topNote = juce::jlimit (rows() - 1, 127, topNote + dir * 2);
    repaint();
}

// ------------------------------------------------------------------------------------------- drags
void PianoRoll::applyMove (int dTicks, int dPitch, bool copy, bool semitones)
{
    // every selected note moves by the same amount, kept inside the pattern and the keyboard
    const int totalT = totalTicks();
    std::vector<NoteData> moving;
    std::vector<NoteRef> from, kept;
    for (auto& r : selBefore)
    {
        NoteData nd;
        if (! noteData (before, r, nd)) continue;
        if (nd.locked && ! copy) { kept.push_back (r); continue; }   // locked notes stay (a copy of one is free)
        nd.locked = false;
        moving.push_back (nd);
        from.push_back (r);
        dTicks = std::clamp (dTicks, -nd.tick, totalT - 1 - nd.tick);
        if (semitones || ! rowsSkipPitches()) dPitch = std::clamp (dPitch, -nd.note, 127 - nd.note);
    }
    // by rows (scale lock, fold): each note moves the same number of rows, so it stays on shown pitches
    auto moved = [&] (int note)
    {
        if (semitones || ! rowsSkipPitches() || dPitch == 0) return std::clamp (note + dPitch, 0, 127);
        const auto it = std::find (rowNotes.begin(), rowNotes.end(), note);
        if (it == rowNotes.end()) return std::clamp (note + dPitch, 0, 127);
        const int idx = std::clamp ((int) (it - rowNotes.begin()) - dPitch, 0, (int) rowNotes.size() - 1);   // rows run high to low
        return rowNotes[(size_t) idx];
    };
    const Engine::ScopedEdit ed (ctx.engine);
    auto& tr = track();
    tr = before;
    if (! copy) for (auto& r : from) removeRef (tr, r);
    sel = kept;
    for (auto m : moving)
    {
        m.tick += dTicks;
        m.note = moved (m.note);
        sel.push_back (place (tr, m));
    }
    if (! sel.empty() && sel.front().note != previewNote && drag != Drag::None) preview (sel.front().note);
    repaint();
}

void PianoRoll::applyResize (int dTicks, bool end, bool free)
{
    const int totalT = totalTicks();
    const int minT = free || ! ctx.rollSettings.snap ? std::max (1, stepT() / 16) : gridTicks();
    const Engine::ScopedEdit ed (ctx.engine);
    auto& tr = track();
    tr = before;
    sel.clear();
    std::vector<NoteData> notes;
    for (auto& r : selBefore)
    {
        NoteData nd;
        if (! noteData (before, r, nd)) continue;
        if (nd.locked) { sel.push_back (r); continue; }
        notes.push_back (nd);
        removeRef (tr, r);
    }
    if (end)
    {
        // the dragged note's end goes to the nearest grid line; the others change by the same amount
        const int delta = snapNearest (refEnd + dTicks, free) - refEnd;
        for (auto n : notes)
        {
            n.len = std::clamp (n.len + delta, minT, totalT - n.tick);
            n.flags &= (uint8_t) ~kTieFlag;
            sel.push_back (place (tr, n));
        }
    }
    else
    {
        // the start goes to the nearest grid line and the end stays where it was
        int delta = snapNearest (refStart + dTicks, free) - refStart;
        for (auto& n : notes) delta = std::clamp (delta, -n.tick, n.len - minT);
        for (auto n : notes)
        {
            const int endT = n.tick + n.len;
            n.tick += delta;
            n.len = endT - n.tick;
            sel.push_back (place (tr, n));
        }
    }
    repaint();
}

void PianoRoll::nudge (int dTicks, int dPitch)
{
    ctx.snapshot (pattern());
    before = track();
    selBefore = sel;
    buildRows();
    applyMove (dTicks, dPitch, false, std::abs (dPitch) == 12);   // an octave stays an octave; a step is a row
    ctx.markDirty();
    if (! sel.empty())   // keep the moved notes in view
    {
        const int s = sel.front().step;
        if (s < firstStep() || s >= firstStep() + visibleSteps()) setFirstStep (s - visibleSteps() / 2);
        if (! ctx.rollSettings.fold)
        {
            if (sel.front().note > topNote) topNote = sel.front().note;
            if (sel.front().note < topNote - rows() + 1) topNote = sel.front().note + rows() - 1;
        }
    }
}

// ------------------------------------------------------------------------------------------- functions
std::vector<PianoRoll::NoteRef> PianoRoll::targetRefs() const
{
    if (! sel.empty()) return sel;
    std::vector<NoteRef> all;
    forEachNote (track(), [&] (const NoteRef& r, const NoteData&) { all.push_back (r); });
    return all;
}

std::vector<PianoRoll::NoteData> PianoRoll::takeTargets()
{
    ctx.snapshot (pattern());
    std::vector<NoteData> out;
    const Engine::ScopedEdit ed (ctx.engine);
    auto& tr = track();
    const auto refs = targetRefs();
    for (auto& r : refs)
    {
        NoteData nd;
        if (! noteData (tr, r, nd) || nd.locked) continue;   // locked notes are left alone
        out.push_back (nd);
        removeRef (tr, r);
    }
    std::sort (out.begin(), out.end(), [] (const NoteData& a, const NoteData& b) { return a.tick != b.tick ? a.tick < b.tick : a.note < b.note; });
    return out;
}

void PianoRoll::put (const std::vector<NoteData>& notes)
{
    const int totalT = totalTicks();
    {
        const Engine::ScopedEdit ed (ctx.engine);
        auto& tr = track();
        sel.clear();
        for (auto n : notes)
        {
            if (n.tick < 0 || n.tick >= totalT || n.note < 0 || n.note > 127) continue;
            n.len = std::max (1, n.len);
            sel.push_back (place (tr, n));
        }
    }
    ctx.markDirty();
    repaint();
}

void PianoRoll::selectAll() { sel.clear(); sel = targetRefs(); repaint(); }

void PianoRoll::invertSelection()
{
    const auto old = sel;
    sel.clear();
    for (auto& r : targetRefs()) if (std::find (old.begin(), old.end(), r) == old.end()) sel.push_back (r);
    repaint();
}

void PianoRoll::deleteSelection()
{
    if (sel.empty()) return;
    ctx.snapshot (pattern());
    {
        const Engine::ScopedEdit ed (ctx.engine);
        for (auto& r : sel) { NoteData d; if (noteData (track(), r, d) && ! d.locked) removeRef (track(), r); }
    }
    validateSelection();   // locked notes stay (and stay selected)
    ctx.markDirty();
    repaint();
}

void PianoRoll::copySelection (bool cut)
{
    if (sel.empty()) return;
    clipboard.clear();
    for (auto& r : sel) { NoteData nd; if (noteData (track(), r, nd)) clipboard.push_back (nd); }
    int lo = INT_MAX;
    for (auto& c : clipboard) lo = std::min (lo, c.tick);
    for (auto& c : clipboard) c.tick -= lo;
    if (cut) deleteSelection();
}

void PianoRoll::paste()
{
    if (clipboard.empty()) return;
    // after the selection (its end, up to the next grid line), or at the start of the view
    int at = firstStep() * stepT();
    if (! sel.empty())
    {
        int end = 0;
        for (auto& r : sel) { NoteData nd; if (noteData (track(), r, nd)) end = std::max (end, nd.tick + nd.len); }
        const int g = gridTicks();
        at = (end + g - 1) / g * g;
    }
    ctx.snapshot (pattern());
    std::vector<NoteData> notes;
    for (auto c : clipboard) { c.tick += at; notes.push_back (c); }
    put (notes);
    if (! sel.empty() && (sel.front().step < firstStep() || sel.front().step >= firstStep() + visibleSteps())) setFirstStep (sel.front().step);
}

void PianoRoll::duplicate()
{
    if (sel.empty()) return;
    // the copy follows right after the selection's span (rounded up to the grid)
    int lo = INT_MAX, hi = 0;
    for (auto& r : sel) { NoteData nd; if (noteData (track(), r, nd)) { lo = std::min (lo, nd.tick); hi = std::max (hi, nd.tick + nd.len); } }
    const int g = gridTicks();
    const int span = std::max (g, (hi - lo + g - 1) / g * g);
    copySelection (false);
    ctx.snapshot (pattern());
    std::vector<NoteData> notes;
    for (auto c : clipboard) { c.tick += lo + span; notes.push_back (c); }
    put (notes);
}

void PianoRoll::transpose (int semis)
{
    auto n = takeTargets();
    for (auto& x : n) x.note = std::clamp (x.note + semis, 0, 127);
    put (n);
}

void PianoRoll::transposeBy (int dir, bool octave)
{
    const auto& sc = ctx.engine.project().scale;
    if (octave || ! sc.lock) { transpose (dir * (octave ? 12 : 1)); return; }
    // scale lock: each note to the next note of the scale up / down
    const auto& facts = Scales::factoryScales();
    const auto pcs = Scales::pcSet (sc.root, facts[(size_t) std::clamp (sc.scale, 0, (int) facts.size() - 1)].intervals);
    auto n = takeTargets();
    for (auto& x : n)
    {
        int m = x.note;
        do m += dir; while (m > 0 && m < 127 && ! pcs[(size_t) (m % 12)]);
        x.note = std::clamp (m, 0, 127);
    }
    put (n);
}

void PianoRoll::lengthBy (int dir)
{
    if (sel.empty()) return;
    const int g = gridTicks();
    auto n = takeTargets();
    for (auto& x : n) x.len = std::max (std::max (1, g / 4), x.len + dir * g);
    put (n);
}

void PianoRoll::velocityBy (int dir)
{
    auto n = takeTargets();
    for (auto& x : n) x.vel = std::clamp (x.vel + dir * 8, 1, 127);
    put (n);
}

void PianoRoll::loopSelection()
{
    if (sel.empty()) return;
    int lo = INT_MAX, hi = 0;
    for (auto& r : sel) { NoteData d; if (noteData (track(), r, d)) { lo = std::min (lo, d.tick); hi = std::max (hi, d.tick + d.len); } }
    if (lo > hi) return;
    auto& pt = ctx.engine.project().patterns[(size_t) pattern()];
    const Engine::ScopedEdit ed (ctx.engine);
    pt.loopStart = lo / stepT();
    pt.loopEnd = std::min (totalSteps(), std::max (pt.loopStart + 1, (hi + stepT() - 1) / stepT()));
    pt.loopOn = true;
    ctx.markDirty();
    repaint();
}

void PianoRoll::toggleMute()
{
    auto n = takeTargets();
    bool anyOn = false;
    for (auto& x : n) anyOn = anyOn || ! x.muted;
    for (auto& x : n) x.muted = anyOn;
    put (n);
}

void PianoRoll::toggleLock()
{
    const auto refs = targetRefs();
    bool anyOpen = false;
    for (auto& r : refs) { NoteData d; if (noteData (track(), r, d)) anyOpen = anyOpen || ! d.locked; }
    ctx.snapshot (pattern());
    {
        const Engine::ScopedEdit ed (ctx.engine);
        auto& tr = track();
        for (auto& r : refs)
        {
            if (tr.pianoRoll) { if (auto* n = tr.clipNote ((uint32_t) r.step)) n->locked = anyOpen; }
            else { auto& st = tr.steps[(size_t) r.step]; const int i = st.indexOf (r.note); if (i >= 0) st.setLocked (i, anyOpen); }
        }
    }
    ctx.markDirty();
    repaint();
}

namespace
{
/** Notes grouped by their start (within a 1/16 of a step): the chords of a selection. */
template <class N> std::vector<std::vector<size_t>> chordsOf (const std::vector<N>& notes, int tolerance)
{
    std::vector<std::vector<size_t>> groups;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        bool placed = false;
        for (auto& gr : groups) if (std::abs (notes[gr.front()].tick - notes[i].tick) <= tolerance) { gr.push_back (i); placed = true; break; }
        if (! placed) groups.push_back ({ i });
    }
    for (auto& gr : groups) std::sort (gr.begin(), gr.end(), [&] (size_t a, size_t b) { return notes[a].note < notes[b].note; });
    return groups;
}
} // namespace

void PianoRoll::selectChords()
{
    // every note that starts with a selected note
    std::vector<int> starts;
    for (auto& r : sel) { NoteData d; if (noteData (track(), r, d)) starts.push_back (d.tick); }
    const int tol = std::max (1, stepT() / 16);
    forEachNote (track(), [&] (const NoteRef& ref, const NoteData& d)
    {
        for (int s : starts) if (std::abs (d.tick - s) <= tol && ! isSelected (ref.step, ref.note)) { sel.push_back (ref); break; }
    });
    repaint();
}

void PianoRoll::chordInvert (bool up)
{
    // up: each chord's lowest note goes an octave up; down: its highest an octave down
    auto n = takeTargets();
    for (auto& gr : chordsOf (n, std::max (1, stepT() / 16)))
    {
        if (gr.size() < 2) continue;
        auto& x = n[up ? gr.front() : gr.back()];
        x.note = std::clamp (x.note + (up ? 12 : -12), 0, 127);
    }
    put (n);
}

void PianoRoll::chordSpread()
{
    // open voicing (drop 2): the second-highest note of each chord an octave down
    auto n = takeTargets();
    for (auto& gr : chordsOf (n, std::max (1, stepT() / 16)))
        if (gr.size() >= 3) { auto& x = n[gr[gr.size() - 2]]; x.note = std::max (0, x.note - 12); }
    put (n);
}

void PianoRoll::chordClose()
{
    // close voicing: every note of a chord within the octave above its lowest
    auto n = takeTargets();
    for (auto& gr : chordsOf (n, std::max (1, stepT() / 16)))
    {
        const int root = n[gr.front()].note;
        for (size_t k = 1; k < gr.size(); ++k) { auto& x = n[gr[k]]; while (x.note - root >= 12) x.note -= 12; while (x.note < root) x.note += 12; }
    }
    put (n);
}

void PianoRoll::chordAddBass()
{
    // each chord gets its lowest note an octave below
    auto n = takeTargets();
    std::vector<NoteData> extra;
    for (auto& gr : chordsOf (n, std::max (1, stepT() / 16)))
    {
        auto b = n[gr.front()];
        if (b.note >= 12) { b.note -= 12; extra.push_back (b); }
    }
    n.insert (n.end(), extra.begin(), extra.end());
    put (n);
}

void PianoRoll::fitNotes (bool selectionOnly)
{
    // the notes (or the selection) fill the view: time to whole steps, pitch with a row to spare
    int lo = INT_MAX, hi = 0, plo = 127, phi = 0;
    auto take = [&] (const NoteData& d) { lo = std::min (lo, d.tick); hi = std::max (hi, d.tick + d.len); plo = std::min (plo, d.note); phi = std::max (phi, d.note); };
    if (selectionOnly) { for (auto& r : sel) { NoteData d; if (noteData (track(), r, d)) take (d); } }
    else forEachNote (track(), [&] (const NoteRef&, const NoteData& d) { take (d); });
    if (lo > hi) return;
    const int sT = stepT(), s0 = lo / sT, s1 = std::max (s0 + 4, (hi + sT - 1) / sT);
    ctx.rollSettings.viewSteps = std::clamp (s1 - s0, 4, std::max (stepsPerBar(), totalSteps()));
    setFirstStep (s0);
    if (! ctx.rollSettings.fold)
    {
        ctx.rollSettings.rows = std::clamp (phi - plo + 3, 8, 48);
        topNote = std::clamp (phi + 1, rows() - 1, 127);
    }
    repaint();
}

void PianoRoll::quantize (float amount, bool lengths)
{
    // starts move towards the nearest grid line by `amount`; with lengths, ends go to the grid too
    const int g = gridTicks();
    auto n = takeTargets();
    for (auto& x : n)
    {
        const int target = (int) std::lround (x.tick / (double) g) * g;
        x.tick = (int) std::lround (x.tick + (target - x.tick) * amount);
        if (lengths)
        {
            const int endT = (int) std::lround ((x.tick + x.len) / (double) g) * g;
            x.len = std::max (g, endT - x.tick);
        }
    }
    put (n);
}

void PianoRoll::humanize()
{
    auto n = takeTargets();
    for (auto& x : n) { x.tick = std::max (0, x.tick + rnd.nextInt (7) - 3); x.vel = std::clamp (x.vel + rnd.nextInt (21) - 10, 1, 127); }
    put (n);
}

void PianoRoll::legato()
{
    // each note lasts until the next note (of any pitch) starts
    auto n = takeTargets();
    for (auto& x : n)
    {
        int next = -1;
        for (auto& y : n) if (y.tick > x.tick && (next < 0 || y.tick < next)) next = y.tick;
        if (next > x.tick) x.len = next - x.tick;
    }
    put (n);
}

void PianoRoll::setLengthToNew()
{
    auto n = takeTargets();
    for (auto& x : n) x.len = (int) std::lround (newNoteLength() * stepT() / 128.0);
    put (n);
}

void PianoRoll::stretch (float factor)
{
    auto n = takeTargets();
    if (n.empty()) { put (n); return; }
    const int first = n.front().tick;
    for (auto& x : n) { x.tick = first + (int) std::lround ((x.tick - first) * factor); x.len = (int) std::lround (x.len * factor); }
    put (n);
}

void PianoRoll::reverse()
{
    auto n = takeTargets();
    if (n.empty()) { put (n); return; }
    int lo = INT_MAX, hi = 0;
    for (auto& x : n) { lo = std::min (lo, x.tick); hi = std::max (hi, x.tick + x.len); }
    for (auto& x : n) x.tick = lo + hi - (x.tick + x.len);
    put (n);
}

void PianoRoll::invertPitch()
{
    auto n = takeTargets();
    int lo = 127, hi = 0;
    for (auto& x : n) { lo = std::min (lo, x.note); hi = std::max (hi, x.note); }
    for (auto& x : n) x.note = lo + hi - x.note;
    put (n);
}

void PianoRoll::fitToScale()
{
    const auto& sc = ctx.engine.project().scale;
    const auto& facts = Scales::factoryScales();
    const auto& iv = facts[(size_t) std::clamp (sc.scale, 0, (int) facts.size() - 1)].intervals;
    auto n = takeTargets();
    for (auto& x : n) x.note = Scales::snapNote (x.note, sc.root, iv, 0);
    put (n);
}

void PianoRoll::chop()
{
    // longer notes become a run of grid-length notes
    const int g = gridTicks();
    auto n = takeTargets();
    std::vector<NoteData> out;
    for (auto& x : n)
    {
        const int lt = x.len;
        if (lt <= g) { out.push_back (x); continue; }
        for (int k = 0; k * g < lt; ++k)
        {
            auto piece = x;
            piece.tick = x.tick + k * g;
            piece.len = std::min (g, lt - k * g);
            out.push_back (piece);
        }
    }
    put (out);
}

void PianoRoll::glue()
{
    // a note that ends where the next note of the same pitch starts swallows it
    auto n = takeTargets();
    std::vector<NoteData> out;
    for (auto& x : n)
    {
        bool merged = false;
        for (auto& o : out)
            if (o.note == x.note && std::abs (o.tick + o.len - x.tick) <= 1) { o.len = x.tick + x.len - o.tick; merged = true; break; }
        if (! merged) out.push_back (x);
    }
    put (out);
}

void PianoRoll::strum()
{
    // chord notes come in one after another, lowest first, each a little softer
    auto n = takeTargets();
    std::map<int, int> rank;
    for (auto& x : n)
    {
        const int k = rank[x.tick]++;
        x.tick += k * 3;
        x.vel = std::max (1, x.vel - k * 6);
    }
    put (n);
}

void PianoRoll::velocityRandomize (float amount)
{
    auto n = takeTargets();
    for (auto& x : n) x.vel = std::clamp ((int) std::lround (x.vel * (1.0 + amount * (2.0 * rnd.nextDouble() - 1.0))), 1, 127);
    put (n);
}

void PianoRoll::velocityRamp (bool up)
{
    auto n = takeTargets();
    if (n.empty()) { put (n); return; }
    const int lo = n.front().tick, hi = std::max (lo + 1, n.back().tick);
    for (auto& x : n)
    {
        const float t = (x.tick - lo) / (float) (hi - lo);
        x.vel = juce::roundToInt (40 + 87 * (up ? t : 1.0f - t));
    }
    put (n);
}

void PianoRoll::velocitySet (int v)
{
    auto n = takeTargets();
    for (auto& x : n) x.vel = v;
    put (n);
}

void PianoRoll::rotate (int dir)
{
    // the whole track moves a step, the end wrapping round to the start (locks and lanes with it)
    ctx.snapshot (pattern());
    {
        const Engine::ScopedEdit ed (ctx.engine);
        auto& tr = track();
        const int total = totalSteps();
        if (tr.pianoRoll)
        {
            // a clip: every note a step later / earlier, wrapping round the pattern
            const int totalT = totalTicks();
            for (auto& n : tr.clip) n.start = ((n.start + dir * stepT()) % totalT + totalT) % totalT;
        }
        auto rot = [&] (auto& v)
        {
            if (dir > 0) std::rotate (v.begin(), v.begin() + total - 1, v.begin() + total);
            else std::rotate (v.begin(), v.begin() + 1, v.begin() + total);
        };
        rot (tr.steps);
        for (auto& l : tr.lanes) rot (l);
        for (auto& l : tr.locks) rot (l.values);
        if (! tr.pianoRoll) for (auto& r : sel) r.step = ((r.step + dir) % total + total) % total;
    }
    ctx.markDirty();
    repaint();
}

void PianoRoll::insertStep (bool insert)
{
    // at the first selected step (or the view's first step): later steps move right (insert) or left (delete)
    int at = firstStep();
    if (! sel.empty()) { at = kMaxSteps; for (auto& r : sel) { NoteData d; if (noteData (track(), r, d)) at = std::min (at, d.tick / stepT()); } }
    ctx.snapshot (pattern());
    {
        const Engine::ScopedEdit ed (ctx.engine);
        auto& tr = track();
        const int total = totalSteps();
        if (tr.pianoRoll)
        {
            // a clip: the notes after the step move a step right (insert) or left (the step's own notes go)
            const int a = at * stepT(), sT = stepT();
            if (! insert) tr.clip.erase (std::remove_if (tr.clip.begin(), tr.clip.end(), [&] (const ClipNote& n) { return n.start >= a && n.start < a + sT; }), tr.clip.end());
            for (auto& n : tr.clip) if (n.start >= a) n.start += insert ? sT : -sT;
        }
        auto shift = [&] (auto& v, auto empty)
        {
            if (insert) { for (int s = total - 1; s > at; --s) v[(size_t) s] = v[(size_t) (s - 1)]; v[(size_t) at] = empty; }
            else { for (int s = at; s < total - 1; ++s) v[(size_t) s] = v[(size_t) (s + 1)]; v[(size_t) (total - 1)] = empty; }
        };
        shift (tr.steps, Step());
        for (auto& l : tr.lanes) shift (l, DrumStep());
        for (auto& l : tr.locks) shift (l.values, std::numeric_limits<float>::quiet_NaN());
    }
    sel.clear();
    ctx.markDirty();
    repaint();
}

// ------------------------------------------------------------------------------------------- keys
bool PianoRoll::cursorKey (const juce::KeyPress& k)
{
    // with keyboard focus: arrows move the cursor, Enter adds / removes the note there, Shift+Enter (de)selects it,
    // Alt+arrows nudge the selection; the editing keys (Delete, Ctrl+C …) work as with the mouse
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode(), spb = stepsPerBar();
    const bool arrow = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey;
    if (arrow && mods.isAltDown()) return keyCommand (juce::KeyPress (code, mods.withoutFlags (juce::ModifierKeys::altModifier), 0));
    if (arrow && ! mods.isCommandDown())
    {
        buildRows();
        auto& c = keyCursor;
        if (code == juce::KeyPress::leftKey) c.step = std::max (0, c.step - (mods.isShiftDown() ? spb : 1));
        if (code == juce::KeyPress::rightKey) c.step = std::min (totalSteps() - 1, c.step + (mods.isShiftDown() ? spb : 1));
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            const int dir = code == juce::KeyPress::upKey ? 1 : -1;
            if (mods.isShiftDown()) c.note = std::clamp (c.note + dir * 12, 0, 127);
            else if (ctx.engine.project().scale.lock || ctx.rollSettings.fold)
            {
                // the next row (scale lock / fold show only some pitches)
                auto it = std::find (rowNotes.begin(), rowNotes.end(), c.note);
                if (it == rowNotes.end()) it = std::lower_bound (rowNotes.begin(), rowNotes.end(), c.note, std::greater<int>());
                const auto i = (int) (it - rowNotes.begin()) - dir;
                if (i >= 0 && i < (int) rowNotes.size()) c.note = rowNotes[(size_t) i];
            }
            else c.note = std::clamp (c.note + dir, 0, 127);
        }
        if (c.step < firstStep()) setFirstStep (c.step);
        else if (c.step >= firstStep() + visibleSteps()) setFirstStep (c.step - visibleSteps() + 1);
        if (! ctx.rollSettings.fold)
        {
            if (c.note > topNote) topNote = c.note;
            if (c.note < topNote - rows() + 1) topNote = c.note + rows() - 1;
        }
        preview (c.note); stopPreview();
        repaint();
        return true;
    }
    if (shortcuts().is (cmd::rollPlayFromCursor, k)) { ctx.engine.seek (keyCursor.step); return true; }
    const bool del = code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey;
    if (code == juce::KeyPress::returnKey || (del && sel.empty()))
    {
        // the note under the cursor (a note started earlier still covers the cell); Delete with nothing selected removes it
        NoteRef at;
        const int cT = keyCursor.step * stepT();
        forEachVisible (track(), [&] (const NoteRef& ref, const NoteData& d, R)
        {
            if (ref.note == keyCursor.note && cT + stepT() > d.tick && cT < d.tick + std::max (d.len, 1)) at = ref;
        });
        if (del) { if (at.step >= 0) { sel = { at }; deleteSelection(); } repaint(); return true; }
        if (mods.isShiftDown())
        {
            if (at.step < 0) return true;
            auto it = std::find (sel.begin(), sel.end(), at);
            if (it != sel.end()) sel.erase (it); else sel.push_back (at);
        }
        else if (at.step >= 0) { sel = { at }; deleteSelection(); }
        else { addNoteAt (keyCursor.step * stepT(), keyCursor.note); stopPreview(); ctx.markDirty(); }
        repaint();
        return true;
    }
    return keyCommand (k);
}

bool PianoRoll::keyCommand (const juce::KeyPress& k)
{
    // the piano roll's shortcuts (Settings ▸ Keyboard ▸ Piano roll); arrows nudge, Esc clears the selection
    auto& sc = shortcuts();
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    const bool cmd = mods.isCommandDown();
    if (sc.is (cmd::rollZoomIn, k)) { zoomTime (0.5); return true; }
    if (sc.is (cmd::rollZoomOut, k)) { zoomTime (2.0); return true; }
    if (sc.is (cmd::rollRowsIn, k)) { zoomRows (-2); return true; }
    if (sc.is (cmd::rollRowsOut, k)) { zoomRows (2); return true; }
    if (sc.is (cmd::rollFit, k)) { zoomToFit(); return true; }
    if (sc.is (cmd::rollFitNotes, k)) { fitNotes (! sel.empty()); return true; }
    if (sc.is (cmd::rollLock, k)) { toggleLock(); return true; }
    if (sc.is (cmd::rollSnap, k)) { ctx.rollSettings.snap = ! ctx.rollSettings.snap; repaint(); return true; }
    if (sc.is (cmd::rollSelectAll, k)) { selectAll(); return true; }
    if (sc.is (cmd::rollInvertSelection, k)) { invertSelection(); return true; }
    if (sc.is (cmd::rollPaste, k)) { paste(); return true; }
    if (sc.is (cmd::rollTool, k)) { ctx.rollSettings.tool = ctx.rollSettings.tool == 1 ? 0 : 1; repaint(); return true; }
    if (sc.is (cmd::rollPlayFromCursor, k)) { ctx.engine.seek (keyCursor.step); return true; }
    if (sc.is (cmd::rollLoopToggle, k)) { toggleLoop(); return true; }
    if (sc.is (cmd::rollVelocityUp, k)) { velocityBy (1); return true; }
    if (sc.is (cmd::rollVelocityDown, k)) { velocityBy (-1); return true; }
    if (sc.is (cmd::rollTransposeUp, k)) { transposeBy (1, false); return true; }
    if (sc.is (cmd::rollTransposeDown, k)) { transposeBy (-1, false); return true; }
    if (sc.is (cmd::rollOctaveUp, k)) { transposeBy (1, true); return true; }
    if (sc.is (cmd::rollOctaveDown, k)) { transposeBy (-1, true); return true; }
    if (sel.empty()) return false;
    if (sc.is (cmd::rollDelete, k) || sc.is (cmd::rollDeleteAlt, k)) { deleteSelection(); return true; }
    if (code == juce::KeyPress::escapeKey) { clearSelection(); return true; }
    if (sc.is (cmd::rollCopy, k)) { copySelection (false); return true; }
    if (sc.is (cmd::rollCut, k)) { copySelection (true); return true; }
    if (sc.is (cmd::rollDuplicate, k)) { duplicate(); return true; }
    if (sc.is (cmd::rollQuantize, k)) { quantize (1.0f, false); return true; }
    if (sc.is (cmd::rollMute, k)) { toggleMute(); return true; }
    if (sc.is (cmd::rollShorter, k)) { lengthBy (-1); return true; }
    if (sc.is (cmd::rollLonger, k)) { lengthBy (1); return true; }
    if (sc.is (cmd::rollLoopSelection, k)) { loopSelection(); return true; }
    // nudge: a grid unit (Shift: a bar, Ctrl: 1/48 step), a semitone (Shift: an octave)
    const int byT = mods.isShiftDown() ? stepsPerBar() * stepT() : (cmd ? std::max (1, stepT() / kTicksPerStep) : gridTicks());
    if (code == juce::KeyPress::leftKey) { nudge (-byT, 0); return true; }
    if (code == juce::KeyPress::rightKey) { nudge (byT, 0); return true; }
    if (code == juce::KeyPress::upKey && ! cmd) { nudge (0, mods.isShiftDown() ? 12 : 1); return true; }
    if (code == juce::KeyPress::downKey && ! cmd) { nudge (0, mods.isShiftDown() ? -12 : -1); return true; }
    return false;
}
} // namespace mb::ui
