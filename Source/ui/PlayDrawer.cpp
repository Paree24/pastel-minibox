#include "PlayDrawer.h"
#include "seq/Scales.h"
#include "seq/ScaleTranspose.h"

namespace mb::ui
{
using R = juce::Rectangle<float>;

namespace
{
const char* kChroma[12] = { "C", "C\xe2\x99\xaf", "D", "E\xe2\x99\xad", "E", "F", "F\xe2\x99\xaf", "G", "A\xe2\x99\xad", "A", "B\xe2\x99\xad", "B" };
juce::String chroma (int pc) { return juce::String (juce::CharPointer_UTF8 (kChroma[((pc % 12) + 12) % 12])); }
const char* kRates[15] = { "1/4", "1/4 D", "1/4 T", "1/8", "1/8 D", "1/8 T", "1/16", "1/16 D", "1/16 T", "1/32", "1/32 D", "1/32 T", "1/64", "1/64 D", "1/64 T" };

/** A knob onto a value the drawer reads and writes through functions (the input track can change). */
struct FnBinding : Binding
{
    std::function<float()> get; std::function<void (float)> set;
    float lo, hi, def; std::function<juce::String (float)> fmt; int nSteps;
    FnBinding (std::function<float()> g, std::function<void (float)> s, float l, float h, float d, std::function<juce::String (float)> f, int st = 0)
        : get (std::move (g)), set (std::move (s)), lo (l), hi (h), def (d), fmt (std::move (f)), nSteps (st) {}
    float getNorm() const override { return (get() - lo) / (hi - lo); }
    void setNorm (float n) override
    {
        float v = lo + (hi - lo) * juce::jlimit (0.0f, 1.0f, n);
        if (nSteps > 1) v = std::round (v);
        set (v);
    }
    float defaultNorm() const override { return (def - lo) / (hi - lo); }
    juce::String text() const override { return fmt (get()); }
    int steps() const override { return nSteps; }
};

constexpr float kTop = 44;
const float kWeights[4] = { 1.3f, 1.35f, 1.25f, 1.0f };
} // namespace

PlayDrawer::PlayDrawer (AppContext& c) : ctx (c)
{
    auto& proj = ctx.engine.project();
    auto add = [this] (std::unique_ptr<Binding> b, const juce::String& label, juce::Colour colour)
    {
        auto k = std::make_unique<KnobView> (std::move (b), label, colour, 18.0f, true);
        addAndMakeVisible (*k);
        knobs.push_back (std::move (k));
    };
    const auto colour = col::track (0);
    add (std::make_unique<FnBinding> ([this] { return play().strumMs; }, [this] (float v) { play().strumMs = v; ctx.markDirty(); }, 0.0f, 40.0f, 0.0f,
                                      [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; }), "Strum", colour);
    add (std::make_unique<FnBinding> ([this] { return (float) play().arpOct; }, [this] (float v) { play().arpOct = (int) v; ctx.markDirty(); }, 0.0f, 3.0f, 0.0f,
                                      [] (float v) { return juce::String (juce::roundToInt (v) + 1); }, 4), "Octaves", colour);
    add (std::make_unique<FnBinding> ([this] { return play().arpGate; }, [this] (float v) { play().arpGate = v; ctx.markDirty(); }, 10.0f, 100.0f, 90.0f,
                                      [] (float v) { return juce::String (juce::roundToInt (v)) + "%"; }), "Gate", colour);
    add (std::make_unique<FnBinding> ([this] { return play().arpSwing; }, [this] (float v) { play().arpSwing = v; ctx.markDirty(); }, 0.0f, 60.0f, 0.0f,
                                      [] (float v) { return juce::String (juce::roundToInt (v)) + "%"; }), "Swing", colour);
    add (std::make_unique<FnBinding> ([&proj] { return proj.recordQuantize; }, [this, &proj] (float v) { proj.recordQuantize = v; ctx.markDirty(); }, 0.0f, 1.0f, 1.0f,
                                      [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }), "Quantize", col::record);
}

PlaySettings& PlayDrawer::play()
{
    return ctx.engine.project().patterns[(size_t) ctx.focusPattern()].slots[(size_t) ctx.focusTrack()].play;
}

R PlayDrawer::box (int i) const
{
    const float iw = (float) getWidth() - 28, ih = (float) getHeight() - kTop - 14;
    const float unit = (iw - 12 * 3) / (kWeights[0] + kWeights[1] + kWeights[2] + kWeights[3]);
    float x = 14;
    for (int k = 0; k < i; ++k) x += unit * kWeights[k] + 12;
    return { x, kTop, unit * kWeights[i], ih };
}

void PlayDrawer::resized()
{
    auto place = [] (KnobView& k, float cx, float cy) { k.setBounds (juce::Rectangle<float> (cx - 43, cy - 24, 86, 80).toNearestInt()); };
    const auto chord = box (1), arp = box (2), rec = box (3);
    place (*knobs[0], chord.getX() + 46, chord.getY() + 176);
    for (int i = 0; i < 3; ++i) place (*knobs[(size_t) (1 + i)], arp.getX() + arp.getWidth() / 6 + i * arp.getWidth() / 3, arp.getY() + 174);
    place (*knobs[4], rec.getX() + 46, rec.getY() + 176);
}

juce::String PlayDrawer::chordName (int degree, int& count) const
{
    const auto& proj = ctx.engine.project();
    const auto& sc = proj.scale;
    const auto& facts = Scales::factoryScales();
    const auto& def = facts[(size_t) std::clamp (sc.scale, 0, (int) facts.size() - 1)];
    const auto& iv = def.intervals;
    const auto& ps = proj.patterns[(size_t) ctx.focusPattern()].slots[(size_t) ctx.focusTrack()].play;
    int rel[8] { 0 };
    const int forced = ps.chordType[(size_t) (degree % 12)];
    count = forced == 0 ? 1 : (forced > 0 ? Scales::forcedChordIntervals (forced, rel)
                                         : (def.powerChords ? Scales::forcedChordIntervals (7, rel) : Scales::diatonicTriad (degree, iv, rel)));
    const auto rootName = chroma (sc.root + iv[(size_t) degree]);
    if (count == 1) return rootName;
    juce::String q;
    auto is = [&] (std::initializer_list<int> l) { if ((int) l.size() != count) return false; int i = 0; for (int v : l) if (rel[i++] != v) return false; return true; };
    if (is ({ 0, 4, 7 })) q = "";
    else if (is ({ 0, 3, 7 })) q = "m";
    else if (is ({ 0, 3, 6 })) q = juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
    else if (is ({ 0, 4, 8 })) q = "+";
    else if (is ({ 0, 2, 7 })) q = "sus2";
    else if (is ({ 0, 5, 7 })) q = "sus4";
    else if (is ({ 0, 7 }) || is ({ 0, 7, 12 })) q = "5";
    else if (is ({ 0, 4, 7, 10 })) q = "7";
    else if (is ({ 0, 4, 7, 11 })) q = "maj7";
    else if (is ({ 0, 3, 7, 10 })) q = "m7";
    else if (is ({ 0, 4, 7, 14 })) q = "add9";
    return rootName + q;
}

void PlayDrawer::paint (juce::Graphics& g)
{
    auto& proj = ctx.engine.project();
    const int track = ctx.focusTrack();
    const auto colour = col::track (track);
    auto& ps = play();
    zones.clear();
    fb.begin();
    auto zone = [this] (R r, const juce::String& id, int a = 0, float radius = -1.0f) { zones.push_back ({ r, id, a }); fb.hot (r, radius); };
    const float w = (float) getWidth(), h = (float) getHeight();
    g.setColour (col::surface); g.fillRoundedRectangle (0, 0, w, h, 14);
    g.setColour (col::divider); g.drawRoundedRectangle (0.75f, 0.75f, w - 1.5f, h - 1.5f, 14, 1.5f);
    // who each part belongs to: the whole project (Scale), the selected track (Chord, Arp)
    auto* dev = ctx.focusDevice();
    const auto b0 = box (0), b1 = box (1);
    paint::text (g, "Whole project", b0.getX() + 4, 28, 12, Weight::Bold, col::quiet);
    paint::text (g, "Selected track", b1.getX() + 4, 28, 12, Weight::Bold, col::quiet);
    paint::trackBadge (g, b1.getX() + 108, 23, track, 11);
    paint::text (g, dev ? deviceTypeName (dev->type()) : juce::String ("Empty"), b1.getX() + 126, 28, 13, Weight::Black, col::text);
    if (dev != nullptr && dev->laneCount() > 0)
        paint::text (g, "(drums play their pads directly)", b1.getX() + 140 + textWidth (deviceTypeName (dev->type()), 13, Weight::Black), 28, 11, Weight::Bold, col::faint);
    g.setColour (col::divider); g.drawLine (b1.getX() - 6, 12, b1.getX() - 6, 34, 1.5f);
    g.setColour (col::control); g.fillEllipse (w - 40, 9, 28, 28);
    g.setColour (col::quiet); g.drawLine (w - 31, 18, w - 21, 28, 1.8f); g.drawLine (w - 21, 18, w - 31, 28, 1.8f);
    zone ({ w - 40, 9, 28, 28 }, "close", 0, 14);

    auto panel = [&] (R b, const char* title)
    {
        g.setColour (juce::Colour (0xff1B1D22)); g.fillRoundedRectangle (b, 10);
        paint::text (g, title, b.getX() + 14, b.getY() + 22, 11, Weight::Black, col::quiet);
    };
    auto togglePill = [&] (float right, float y, const juce::String& label, bool on, juce::Colour c, const juce::String& id)
    {
        const float pw = textWidth (label, 10, on ? Weight::Black : Weight::Bold) + 18;
        const R r (right - pw, y, pw, 22);
        g.setColour (on ? c : col::control); g.fillRoundedRectangle (r, 11);
        paint::textCentred (g, label, r, 10, on ? Weight::Black : Weight::Bold, on ? col::bg : col::quiet);
        zone (r, id, 0, 11);
        return pw;
    };
    auto pillAt = [&] (float x, float y, const juce::String& label, bool on, juce::Colour c, const juce::String& id, float fixedW = -1.0f, bool caret = false)
    {
        const float pw = paint::pill (g, x, y, label, caret ? 12.0f : 11.0f, on ? c : col::control, on ? col::bg : (caret ? col::text : col::quiet), 28,
                                      caret ? 12.0f : 14.0f, on ? Weight::Black : Weight::Bold, caret, juce::Justification::left, fixedW);
        zone ({ x, y, pw, 28 }, id, 0, 14);
        return pw;
    };
    auto seg = [&] (R r, const juce::StringArray& opts, int sel, juce::Colour c, const juce::String& id)
    {
        const auto segs = paint::segmented (g, r, opts, sel, c, 11);
        if (segs.empty()) zone (r, id + "menu");
        for (size_t i = 0; i < segs.size(); ++i) zone (segs[i].reduced (2), id, (int) i);
    };

    // ---- Scale (the project's key)
    {
        const auto b = box (0);
        panel (b, "SCALE");
        auto& sc = proj.scale;
        const float lw = togglePill (b.getRight() - 12, b.getY() + 8, "Lock", sc.lock, colour, "lock");
        togglePill (b.getRight() - 18 - lw, b.getY() + 8, "White keys", sc.whiteKeys, colour, "white");
        const auto& facts = Scales::factoryScales();
        const auto& def = facts[(size_t) std::clamp (sc.scale, 0, (int) facts.size() - 1)];
        const auto pcs = Scales::pcSet (sc.root, def.intervals);
        const float cw = (b.getWidth() - 28) / 12;
        for (int i = 0; i < 12; ++i)
        {
            const bool root = i == sc.root, ins = pcs[(size_t) i];
            const R c (b.getX() + 14 + i * cw + 1, b.getY() + 38, cw - 2, 44);
            g.setColour (root ? colour : (ins ? colour.withAlpha (0.28f).overlaidWith (col::control.withAlpha (0.55f)) : juce::Colour (0xff16181C)));
            g.fillRoundedRectangle (c, 6);
            paint::text (g, chroma (i), c.getCentreX(), c.getY() + 28, 10, ins ? Weight::Black : Weight::Bold, root ? col::bg : (ins ? col::text : col::faint), juce::Justification::horizontallyCentred);
            zone (c, "root", i, 6);
        }
        pillAt (b.getX() + 14, b.getY() + 96, def.name, false, colour, "scale", b.getWidth() * 0.5f, true);
        seg ({ b.getX() + 22 + b.getWidth() * 0.5f, b.getY() + 96, b.getWidth() * 0.5f - 36, 28 }, { "Nearest", "Down", "Up" }, sc.snap, colour, "snap");
        paint::text (g, "Lock: off-scale keys snap to the scale", b.getX() + 14, b.getY() + 156, 10, Weight::Bold, col::faint);
        paint::text (g, "White keys: the white keys play the scale", b.getX() + 14, b.getY() + 172, 10, Weight::Bold, col::faint);
        paint::text (g, "Click a note to set the key", b.getX() + 14, b.getBottom() - 16, 10, Weight::Bold, col::faint);
    }

    // ---- Chord (input track)
    {
        const auto b = box (1);
        panel (b, "CHORD");
        togglePill (b.getRight() - 12, b.getY() + 8, "On", ps.chordOn, colour, "chordOn");
        const auto& facts = Scales::factoryScales();
        const auto& iv = facts[(size_t) std::clamp (proj.scale.scale, 0, (int) facts.size() - 1)].intervals;
        const int nDeg = std::min (9, (int) iv.size());
        const float dw = (b.getWidth() - 28) / (float) nDeg;
        static const char* romans[9] = { "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX" };
        for (int d = 0; d < nDeg; ++d)
        {
            int count = 0;
            const auto name = chordName (d, count);
            const bool forced = ps.chordType[(size_t) d] >= 0;
            const R c (b.getX() + 14 + d * dw + 2, b.getY() + 38, dw - 4, 52);
            g.setColour (col::control); g.fillRoundedRectangle (c, 8);
            if (forced) { g.setColour (colour); g.drawRoundedRectangle (c.reduced (0.75f), 8, 1.5f); }
            const bool minorish = name.contains ("m") && ! name.contains ("maj");
            juce::String roman (romans[d]);
            if (minorish || name.contains (juce::CharPointer_UTF8 ("\xc2\xb0"))) roman = roman.toLowerCase();
            paint::text (g, roman, c.getCentreX(), c.getY() + 18, 10, Weight::Bold, col::quiet, juce::Justification::horizontallyCentred);
            paint::text (g, ps.chordOn ? name : chroma (proj.scale.root + iv[(size_t) d]), c.getCentreX(), c.getY() + 40, 13, Weight::Black,
                         forced ? colour : (ps.chordOn ? col::text : col::faint), juce::Justification::horizontallyCentred);
            zone (c, "degree", d, 8);
        }
        seg ({ b.getX() + 14, b.getY() + 104, b.getWidth() * 0.46f, 28 }, { "Root", "1st", "2nd" }, ps.inversion, colour, "inv");
        float px = b.getX() + 24 + b.getWidth() * 0.46f;
        px += pillAt (px, b.getY() + 104, "Lower", ps.invLower, colour, "lower") + 6;
        pillAt (px, b.getY() + 104, "+ Bass", ps.bassBelow, colour, "bass");
        paint::text (g, "Click a degree to pick its chord", b.getX() + 88, b.getY() + 172, 10, Weight::Bold, col::faint);
        paint::text (g, proj.scale.lock ? "(Auto follows the scale)" : "(turn Lock on for in-key chords)", b.getX() + 88, b.getY() + 188, 10, Weight::Bold, col::faint);
    }

    // ---- Arp (input track)
    {
        const auto b = box (2);
        panel (b, "ARP");
        const float onW = togglePill (b.getRight() - 12, b.getY() + 8, "On", ps.arpOn, colour, "arpOn");
        togglePill (b.getRight() - 18 - onW, b.getY() + 8, "Latch", ps.arpLatch, colour, "latch");
        seg ({ b.getX() + 14, b.getY() + 38, b.getWidth() - 28, 28 }, { "Up", "Down", "Up-Dn", "Dn-Up", "Rand", "Played" }, ps.arpMode, colour, "mode");
        pillAt (b.getX() + 14, b.getY() + 78, kRates[std::clamp (ps.arpRate, 0, 14)], false, colour, "rate", 86, true);
        const R well (b.getX() + 110, b.getY() + 76, b.getWidth() - 124, 58);
        paint::well (g, well);
        static const int up[12] = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3 }, upDn[12] = { 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1 };
        static const int dn[12] = { 3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 0 }, dnUp[12] = { 3, 2, 1, 0, 1, 2, 3, 2, 1, 0, 1, 2 };
        static const int rnd[12] = { 2, 0, 3, 1, 1, 3, 0, 2, 3, 0, 2, 1 }, pl[12] = { 1, 3, 0, 2, 1, 3, 0, 2, 1, 3, 0, 2 };
        const int* pts[6] = { up, dn, upDn, dnUp, rnd, pl };
        const float dx = (well.getWidth() - 20) / 11.0f;
        g.setColour (ps.arpOn ? colour : col::faint);
        for (int i = 0; i < 12; ++i)
            g.fillRoundedRectangle (well.getX() + 10 + i * dx - 5, well.getBottom() - 10 - pts[std::clamp (ps.arpMode, 0, 5)][i] * 12.0f - 6, 10, 6, 3);
    }

    // ---- Record
    {
        const auto b = box (3);
        panel (b, "RECORD");
        const R ts (b.getRight() - 44, b.getY() + 8, 32, 22);
        g.setColour (col::control); g.fillRoundedRectangle (ts, 11);
        paint::toStartIcon (g, ts.getCentreX(), ts.getCentreY(), 10, col::text);
        zone (ts, "start", 0, 11);
        seg ({ b.getX() + 14, b.getY() + 38, b.getWidth() - 28, 28 }, { "Live", "Step" }, proj.recordMode, col::record, "recMode");
        if (proj.recordMode == 1)
        {
            paint::text (g, "Length", b.getX() + 14, b.getY() + 92, 11, Weight::Bold, col::quiet);
            seg ({ b.getX() + 70, b.getY() + 76, b.getWidth() - 84, 26 }, { "1/16", "1/8", "1/4", "1/2" }, proj.stepLength, col::record, "len");
            float rx = b.getX() + 14;
            rx += pillAt (rx, b.getY() + 116, "Rest", false, col::record, "rest") + 6;
            pillAt (rx, b.getY() + 116, "Back", false, col::record, "back");
            const int total = std::max (1, proj.patternSteps (ctx.focusPattern()));
            paint::text (g, "Cursor", b.getX() + 14, b.getY() + 172, 11, Weight::Bold, col::quiet);
            paint::text (g, "step " + juce::String (ctx.engine.stepRecordCursor.load() % total + 1) + " of " + juce::String (total), b.getX() + 70, b.getY() + 172, 12, Weight::Black, col::record);
            paint::text (g, "Hold keys together for a chord", b.getX() + 14, b.getBottom() - 16, 10, Weight::Bold, col::faint);
        }
        else
        {
            float rx = b.getX() + 14;
            rx += pillAt (rx, b.getY() + 80, proj.overdub ? "Overdub" : "Extend", proj.overdub, col::record, "overdub") + 6;
            pillAt (rx, b.getY() + 80, "Count-in", proj.countIn, col::record, "countIn");
            paint::text (g, "Plays and records in time with the pattern", b.getX() + 92, b.getY() + 172, 10, Weight::Bold, col::faint);
            paint::text (g, "Chords and arp are recorded as heard", b.getX() + 92, b.getY() + 188, 10, Weight::Bold, col::faint);
        }
        knobs[4]->setVisible (proj.recordMode == 0);
    }
    fb.paint (g);
}

void PlayDrawer::moveNotes (const ScaleSettings& before)
{
    const auto after = ctx.engine.project().scale;
    auto isDrum = [this] (int p, int t) { auto* d = ctx.engine.device (p, t); return d == nullptr || d->laneCount() > 0; };
    const auto& facts = Scales::factoryScales();
    if (before.root == after.root && before.scale == after.scale) return;
    if (facts[(size_t) std::clamp (before.scale, 0, (int) facts.size() - 1)].intervals.size() >= 12
        || facts[(size_t) std::clamp (after.scale, 0, (int) facts.size() - 1)].intervals.size() >= 12) return;   // chromatic: nothing moves
    ctx.snapshotAll();
    const Engine::ScopedEdit ed (ctx.engine);
    scaletranspose::project (ctx.engine.project(), before, after, isDrum);
}

void PlayDrawer::chooseScale()
{
    juce::PopupMenu m;
    const auto& facts = Scales::factoryScales();
    for (int i = 0; i < (int) facts.size(); ++i) m.addItem (i + 1, facts[(size_t) i].name, true, i == ctx.engine.project().scale.scale);
    m.showMenuAsync (menuAt (*this, menuArea).withMaximumNumColumns (3), [this] (int id)
    {
        if (id <= 0) return;
        auto& sc = ctx.engine.project().scale;
        const auto before = sc;
        sc.scale = id - 1;
        moveNotes (before);   // the notes follow the new mode, degree for degree
        ctx.markDirty();
        repaint();
    });
}

void PlayDrawer::chooseChord (int degree)
{
    juce::PopupMenu m;
    auto& ps = play();
    m.addItem (100, "Auto (follows the scale)", true, ps.chordType[(size_t) degree] < 0);
    for (int t = 0; t <= 12; ++t) m.addItem (101 + t, Scales::chordTypeName (t), true, ps.chordType[(size_t) degree] == t);
    m.addSeparator();
    m.addItem (99, "Reset every degree to Auto");
    m.showMenuAsync (menuAt (*this, menuArea), [this, degree] (int id)
    {
        auto& p = play();
        if (id == 99) p.chordType.fill (-1);
        else if (id >= 100) p.chordType[(size_t) degree] = (int8_t) (id - 101);
        else return;
        if (id != 99 && ! p.chordOn) p.chordOn = true;
        ctx.markDirty();
        repaint();
    });
}

void PlayDrawer::chooseRate()
{
    juce::PopupMenu m;
    for (int i = 0; i < 15; ++i) m.addItem (i + 1, kRates[i], true, i == play().arpRate);
    m.showMenuAsync (menuAt (*this, menuArea), [this] (int id) { if (id > 0) { play().arpRate = id - 1; ctx.markDirty(); repaint(); } });
}

void PlayDrawer::mouseUp (const juce::MouseEvent& e)
{
    menuArea = fb.hotAt (e.position);
    const Zone* z = nullptr;
    for (auto it = zones.rbegin(); it != zones.rend(); ++it) if (it->r.contains (e.position)) { z = &*it; break; }
    if (z == nullptr) return;
    auto& proj = ctx.engine.project();
    auto& ps = play();
    auto& sc = proj.scale;
    const auto& id = z->id;
    if (id == "close") { if (onClose) onClose(); return; }
    if (id == "lock") sc.lock = ! sc.lock;
    else if (id == "root")
    {
        // a new root: the notes move with the scale (one undo step)
        const auto before = sc;
        sc.root = z->a; sc.lock = true;
        moveNotes (before);
    }
    else if (id == "scale") { chooseScale(); return; }
    else if (id == "snap") sc.snap = z->a;
    else if (id == "white") sc.whiteKeys = ! sc.whiteKeys;
    else if (id == "chordOn") ps.chordOn = ! ps.chordOn;
    else if (id == "degree") { chooseChord (z->a); return; }
    else if (id == "inv") ps.inversion = z->a;
    else if (id == "lower") ps.invLower = ! ps.invLower;
    else if (id == "bass") ps.bassBelow = ! ps.bassBelow;
    else if (id == "arpOn") ps.arpOn = ! ps.arpOn;
    else if (id == "latch") ps.arpLatch = ! ps.arpLatch;
    else if (id == "mode") ps.arpMode = z->a;
    else if (id == "rate") { chooseRate(); return; }
    else if (id == "start") ctx.engine.toStart();
    else if (id == "recMode") proj.recordMode = z->a;
    else if (id == "len") proj.stepLength = z->a;
    else if (id == "rest") ctx.engine.stepRest();
    else if (id == "back") { ctx.snapshot (ctx.focusPattern()); ctx.engine.stepBack(); }
    else if (id == "overdub") proj.overdub = ! proj.overdub;
    else if (id == "countIn") proj.countIn = ! proj.countIn;
    else if (id.endsWith ("menu"))
    {
        // a segmented control drawn as a dropdown (narrow window): pick from a menu
        juce::PopupMenu m;
        const juce::StringArray modes { "Up", "Down", "Up-Dn", "Dn-Up", "Rand", "Played" };
        if (id == "modemenu") for (int i = 0; i < modes.size(); ++i) m.addItem (i + 1, modes[i], true, i == ps.arpMode);
        m.showMenuAsync (menuAt (*this, menuArea), [this] (int r) { if (r > 0) { play().arpMode = r - 1; ctx.markDirty(); repaint(); } });
        return;
    }
    ctx.markDirty();
    repaint();
}

void PlayDrawer::refresh()
{
    auto& proj = ctx.engine.project();
    const auto& ps = play();
    juce::String s;
    s << ctx.focusTrack() << ctx.focusPattern() << (int) proj.scale.lock << proj.scale.root << proj.scale.scale << proj.scale.snap << (int) proj.scale.whiteKeys
      << (int) ps.chordOn << ps.inversion << (int) ps.invLower << (int) ps.bassBelow << (int) ps.arpOn << (int) ps.arpLatch << ps.arpMode << ps.arpRate
      << proj.recordMode << proj.stepLength << (int) proj.overdub << (int) proj.countIn << ctx.engine.stepRecordCursor.load();
    for (auto t : ps.chordType) s << (int) t;
    if (auto* d = ctx.focusDevice()) s << (int) d->type();
    if (s != sig) { sig = s; repaint(); }
    const auto colour = col::track (ctx.focusTrack());
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        if (i < 4 && knobs[i]->colour != colour) { knobs[i]->colour = colour; knobs[i]->repaint(); }
        knobs[i]->refresh();
    }
}
} // namespace mb::ui
