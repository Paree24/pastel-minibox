#include "TopBar.h"

namespace mb::ui
{
using namespace geo;

TopBar::TopBar (AppContext& c) : ctx (c) {}

juce::String TopBar::zoneAt (juce::Point<float> p) const
{
    for (auto& z : zones) if (z.r.contains (p)) return z.id;
    return {};
}

void TopBar::mouseMove (const juce::MouseEvent& e)
{
    for (auto& z : zones) if (z.r.contains (e.position)) { setTooltip (z.tip); return; }
    setTooltip ({});
}

void TopBar::refresh()
{
    auto& e = ctx.engine;
    juce::String s;
    s << (int) view << (int) e.isPlaying() << (int) e.isRecording() << (int) e.inCountIn() << e.activePattern() << e.queuedPattern() << ctx.focusPattern()
      << juce::roundToInt (e.patternProgress() * 60) << e.currentStep() << e.project().bpm << e.project().beats << e.project().unit << (int) e.project().metronome
      << projectName << (int) ctx.dirty << (int) playOpen;
    for (int p = 0; p < kPatterns; ++p) for (int t = 0; t < kTracks; ++t) s << (e.device (p, t) != nullptr ? 1 : 0);
    if (s != sig) { sig = s; repaint(); }
}

void TopBar::paint (juce::Graphics& g)
{
    auto& e = ctx.engine;
    auto& proj = e.project();
    zones.clear();
    fb.begin();
    auto zone = [this] (juce::Rectangle<float> r, const juce::String& id, const juce::String& tip = {}) { zones.push_back ({ r, id, tip }); fb.hot (r); };
    g.setColour (col::divider);
    g.drawHorizontalLine (TOP - 1, 0, W);
    // brand
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (col::track (i));
        g.fillRoundedRectangle (M + (i % 2) * 11.0f, 22 + (i / 2) * 11.0f, 9, 9, 2.5f);
    }
    paint::text (g, "Minibox", M + 32, 39, 20, Weight::Black, col::text);
    // Rack / Seq
    const juce::Rectangle<float> rs (148, 16, 150, 32);
    paint::segmented (g, rs, { "Rack", "Seq" }, view == View::Rack ? 0 : (view == View::Seq ? 1 : -1), col::text, 12);
    zone (rs.withWidth (75), "rack"); zone (rs.withTrimmedLeft (75), "seq");
    // transport
    // two record buttons: Live (real time, while playing) and Step (notes and chords at a cursor); the armed one is red
    const bool rec = e.isRecording(), playing = e.isPlaying();
    const bool live = rec && proj.recordMode == 0, step = rec && proj.recordMode == 1;
    g.setColour (live ? col::record : col::control); g.fillEllipse (328 - 17, 30 - 17, 34, 34);
    paint::record (g, 328, 30, 11, live ? col::bg : col::record);
    paint::text (g, "LIVE", 328, 59, 8, Weight::Black, live ? col::record : col::faint, juce::Justification::horizontallyCentred);
    zone ({ 311, 13, 34, 34 }, "rec", "Live record: what you play while the pattern runs (Ctrl+R)");
    g.setColour (step ? col::record : col::control); g.fillEllipse (372 - 17, 30 - 17, 34, 34);
    for (int k = 0; k < 3; ++k)   // a staircase: notes written step by step
    {
        g.setColour (step ? col::bg : col::record);
        g.fillRoundedRectangle (372 - 8 + k * 6.0f, 30 + 2 - k * 4.0f, 4.5f, 4.5f, 1.0f);
    }
    paint::text (g, "STEP", 372, 59, 8, Weight::Black, step ? col::record : col::faint, juce::Justification::horizontallyCentred);
    zone ({ 355, 13, 34, 34 }, "recstep", "Step record: each key or chord is written at the cursor, which then moves on (Ctrl+Shift+R)");
    paint::chevron (g, 395.5f, 31, 7, col::quiet, 'd');
    zone ({ 389, 20, 13, 24 }, "recmenu", "Recording settings: Live / Step, Extend / Overdub, count-in, quantize, step length (Alt+R)");
    g.setColour (col::control); g.fillEllipse (420 - 18, 32 - 18, 36, 36);
    g.setColour (col::divider); g.drawEllipse (420 - 21, 32 - 21, 42, 42, 2.5f);
    if (playing)
    {
        g.setColour (e.inCountIn() ? col::record : col::pattern);
        g.strokePath (paint::arc (420, 32, 21, -90, -90 + 360 * juce::jmax (0.01f, e.patternProgress())), juce::PathStrokeType (2.5f));
    }
    paint::play (g, 421, 32, 13.5f, playing ? col::pattern : col::text);
    zone ({ 400, 12, 40, 40 }, "play", "Play (Space)");
    g.setColour (col::control); g.fillEllipse (468 - 18, 32 - 18, 36, 36);
    paint::stop (g, 468, 32, 11, col::text);
    zone ({ 450, 14, 36, 36 }, "stop", "Stop: back to the start of the pattern (twice: also cuts ringing notes)");
    // the position, beside the transport like the tempo (mockup 01_rack): bar . beat . step of the playing pattern
    {
        const int pat = e.activePattern(), spb = std::max (1, proj.stepsPerBar (pat));
        const int perBeat = std::max (1, spb / std::max (1, proj.beats));
        const bool counting = e.inCountIn();
        const int stepNow = playing && ! counting ? std::max (0, e.currentStep()) : 0;
        const auto pos = juce::String (stepNow / spb + 1) + "." + juce::String ((stepNow % spb) / perBeat + 1) + "." + juce::String (stepNow % perBeat + 1);
        const int bars = proj.patterns[(size_t) pat].lengthBars;
        paint::text (g, pos, 552, 37, 22, Weight::Black, counting || (rec && playing) ? col::record : col::text, juce::Justification::horizontallyCentred);
        paint::text (g, counting ? juce::String ("COUNT-IN") : "OF " + juce::String (bars) + (bars == 1 ? " BAR" : " BARS"), 552, 54, 9, Weight::Bold,
                     counting ? col::record : col::faint, juce::Justification::horizontallyCentred);
    }
    // everything after the position display moves right by dx
    constexpr float dx = 112;
    // tempo
    g.setColour (col::control); g.fillEllipse (522 + dx - 14, 32 - 14, 28, 28);
    paint::minus (g, 522 + dx, 32, 9, col::quiet);
    zone ({ 508 + dx, 18, 28, 28 }, "bpm-", "Tempo down (Shift: 0.1)");
    paint::text (g, juce::String (proj.bpm, 1), 584 + dx, 37, 22, Weight::Black, col::text, juce::Justification::horizontallyCentred);
    paint::text (g, "BPM", 584 + dx, 54, 9, Weight::Bold, col::faint, juce::Justification::horizontallyCentred);
    zone ({ 546 + dx, 10, 76, 46 }, "bpm", "Drag to change, double-click to type");
    g.setColour (col::control); g.fillEllipse (646 + dx - 14, 32 - 14, 28, 28);
    paint::plus (g, 646 + dx, 32, 9, col::quiet);
    zone ({ 632 + dx, 18, 28, 28 }, "bpm+", "Tempo up (Shift: 0.1)");
    const float tapW = paint::pill (g, 670 + dx, 18, "Tap", 12, col::control, col::text, 28);
    zone ({ 670 + dx, 18, tapW, 28 }, "tap", "Tap tempo");
    const auto sigText = juce::String (proj.beats) + "/" + juce::String (proj.unit);
    const float sw = paint::pill (g, 738 + dx, 18, sigText, 13, col::control, col::text, 28, 12, Weight::Bold, true);
    zone ({ 738 + dx, 18, sw, 28 }, "sig", "Time signature, swing and count-in");
    g.setColour (proj.metronome ? col::text : col::control); g.fillEllipse (832 + dx - 16, 32 - 16, 32, 32);
    paint::metronome (g, 832 + dx, 32, 15, proj.metronome ? col::bg : col::text);
    zone ({ 816 + dx, 16, 32, 32 }, "click", "Metronome");
    // the Play drawer: scale lock, chord mode, arp
    g.setColour (playOpen ? col::pattern : col::control); g.fillEllipse (872 + dx - 16, 32 - 16, 32, 32);
    paint::keysIcon (g, 872 + dx, 32, 17, playOpen ? col::bg : col::text);
    zone ({ 856 + dx, 16, 32, 32 }, "playdrawer", "Play: scale lock, chords, arp");
    // performance mode: a picked pattern starts when the playing one ends (mockup 16_perform)
    g.setColour (proj.perform ? col::pattern : col::control); g.fillEllipse (912 + dx - 16, 32 - 16, 32, 32);
    {
        juce::Path tri; tri.addTriangle (906 + dx, 26, 914 + dx, 32, 906 + dx, 38);
        g.setColour (proj.perform ? col::bg : col::text); g.fillPath (tri);
        g.fillRoundedRectangle (915.5f + dx, 26, 2.6f, 12, 1);
    }
    zone ({ 896 + dx, 16, 32, 32 }, "perform", "Performance mode: a pattern you pick starts when the playing one ends (click it again to cancel)");
    // A–H
    const int cur = e.activePattern(), queued = e.queuedPattern(), focus = ctx.focusPattern();
    for (int i = 0; i < kPatterns; ++i)
    {
        const float bx = 940 + dx + i * 28.0f, by = 10, bw = 25, bh = 44;
        const juce::Rectangle<float> r (bx, by, bw, bh);
        const bool isCur = i == cur, isQ = i == queued;
        if (isCur) { g.setColour (col::pattern.withAlpha (0.18f)); g.fillRoundedRectangle (r, 10); g.setColour (col::pattern); g.drawRoundedRectangle (r.reduced (0.75f), 10, 1.5f); }
        else
        {
            g.setColour (col::control); g.fillRoundedRectangle (r, 10);
            if (isQ)
            {
                juce::Path p; p.addRoundedRectangle (r.reduced (0.75f), 10.0f);
                const float dash[] = { 4.0f, 3.0f };
                juce::PathStrokeType (1.5f).createDashedStroke (p, p, dash, 2);
                g.setColour (col::pattern); g.fillPath (p);
            }
        }
        if (i == focus && ! isCur) { g.setColour (col::text.withAlpha (0.35f)); g.drawRoundedRectangle (r.reduced (3), 8, 1.0f); }
        paint::text (g, juce::String::charToString ("ABCDEFGH"[i]), bx + bw / 2, by + 24, 14, Weight::Black, isCur || isQ ? col::pattern : col::text, juce::Justification::horizontallyCentred);
        bool any = false;
        for (int t = 0; t < kTracks; ++t) any = any || e.device (i, t) != nullptr;
        if (any) { g.setColour (isCur || isQ ? col::pattern : col::quiet); g.fillEllipse (bx + bw / 2 - 2.5f, by + 31, 5, 5); }   // the pattern has instruments
        if (isQ && e.isPlaying())   // the countdown: the queued pattern starts when the bar fills
        {
            g.setColour (col::pattern);
            g.fillRoundedRectangle (bx + 4, by + bh - 4.5f, (bw - 8) * juce::jlimit (0.0f, 1.0f, e.patternProgress()), 2.5f, 1.2f);
        }
        zone (r, "pat" + juce::String (i), proj.perform ? "Click: next pattern (starts when this one ends; click again to cancel)   Shift-click: copy   Alt-click: clear"
                                                         : "Click: switch (queued to the bar)   Shift-click: copy the current pattern here   Alt-click: clear");
    }
    // project: name and the file actions
    // at the right edge; a narrow window (16:10) shows the file buttons as icons
    const bool compact = W - 424 < 1290;
    const float px = compact ? W - 302.0f : W - 424.0f;
    auto name = projectName;
    if (textWidth (name, 13, Weight::Black) > 74) { while (name.length() > 2 && textWidth (name + "...", 13, Weight::Black) > 74) name = name.dropLastCharacters (1); name << "..."; }
    const bool untitled = projectName == "Untitled";
    paint::text (g, name, px, 37, 13, untitled ? Weight::Bold : Weight::Black, untitled ? col::quiet : col::text);
    zone ({ px, 18, 82, 28 }, "rename", "Rename the project");
    if (ctx.dirty) { g.setColour (col::mute); g.fillEllipse (px + textWidth (name, 13, Weight::Black) + 5, 29, 6, 6); }
    float bx = px + 86;
    struct B { const char* label; const char* id; const char* tip; void (*icon) (juce::Graphics&, float, float, float, juce::Colour); };
    const B buttons[] = { { "New", "new", "New project (Ctrl+N)", paint::newIcon }, { "Open", "open", "Open a project (Ctrl+O)", paint::folderIcon },
                          { "Save", "save", "Save the project (Ctrl+S)", paint::saveIcon }, { "Export", "export", "Export the current pattern as a WAV (Ctrl+E)", paint::exportIcon } };
    for (auto& b : buttons)
    {
        const float bw = compact ? 34.0f : textWidth (b.label, 11) * 1.12f + 36;
        g.setColour (col::control); g.fillRoundedRectangle (bx, 17, bw, 30, 15);
        b.icon (g, compact ? bx + bw / 2 : bx + 15, 32, 12, col::text);
        if (! compact) paint::text (g, b.label, bx + 27, 36, 11, Weight::Bold, col::text);
        zone ({ bx, 17, bw, 30 }, b.id, b.tip);
        bx += bw + 4;
    }
    g.setColour (col::control); g.fillEllipse (W - 40 - 16, 32 - 16, 32, 32);
    paint::dots (g, W - 40, 32, col::quiet);
    zone ({ W - 56, 16, 32, 32 }, "menu", "Settings");
    for (auto& z : zones)
        if (const auto t = learnTarget (z.id); t.isNotEmpty()) paintLearn (g, z.r.expanded (3), t, std::min (z.r.getHeight() / 2 + 3, 12.0f));
    fb.paint (g);
}

juce::String TopBar::learnTarget (const juce::String& id)
{
    if (id == "bpm") return "tempo";
    static const juce::StringArray learnable { "rack", "seq", "rec", "recstep", "play", "stop", "bpm-", "bpm+", "tap", "click", "playdrawer", "perform",
                                               "pat0", "pat1", "pat2", "pat3", "pat4", "pat5", "pat6", "pat7", "save" };
    return learnable.contains (id) ? "action:" + id : juce::String();
}

void TopBar::toggleRecord (int mode)
{
    // the button of the armed mode turns recording off; the other one switches to its mode
    auto& eng = ctx.engine;
    auto& proj = eng.project();
    if (eng.isRecording() && proj.recordMode == mode) { eng.setRecord (false); repaint(); return; }
    if (proj.recordMode != mode) { proj.recordMode = mode; ctx.markDirty(); }
    if (! eng.isRecording())
    {
        ctx.snapshot (ctx.focusPattern());   // a recording pass is one undo step
        eng.setRecord (true);
    }
    repaint();
}

void TopBar::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps.empty() && now - taps.back() > 2000.0) taps.clear();
    taps.push_back (now);
    if (taps.size() > 5) taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        const double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        ctx.engine.project().bpm = juce::jlimit (30.0, 300.0, std::round (600000.0 / avg) / 10.0);
        ctx.markDirty();
    }
}


namespace
{
// the time signature panel: the common signatures first (one click), then beats and unit for any other
constexpr int kCommon[6][2] = { { 4, 4 }, { 3, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 12, 8 } };
constexpr int kY = 56;   // the rows below the common ones
// the recording popover's quantize strengths
constexpr float kQuant[4] = { 0.0f, 0.5f, 0.75f, 1.0f };
} // namespace

std::unique_ptr<juce::Component> TopBar::createRecordingPanel()
{
    // mockup 18_recording: Live / Step, then how the chosen mode writes notes
    struct Panel : juce::Component
    {
        explicit Panel (AppContext& c) : ctx (c) { setSize (330, 262); setWantsKeyboardFocus (true); }
        AppContext& ctx;
        int row = 0;
        struct Z { juce::Rectangle<float> r; int row, value; };
        std::vector<Z> zones;
        int rows() const { return ctx.engine.project().recordMode == 0 ? 4 : 3; }
        void visibilityChanged() override { if (isShowing()) grabKeyboardFocus(); }
        void parentHierarchyChanged() override { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<Panel> (this)] { if (sp != nullptr && sp->isShowing()) sp->grabKeyboardFocus(); }); }
        int quantIndex() const
        {
            int best = 0;
            for (int i = 1; i < 4; ++i) if (std::abs (kQuant[i] - ctx.engine.project().recordQuantize) < std::abs (kQuant[best] - ctx.engine.project().recordQuantize)) best = i;
            return best;
        }
        // row 0 mode; Live: 1 takes, 2 count-in, 3 quantize; Step: 1 length, 2 Rest / Back. value −1: the to-start button
        void apply (int r, int v)
        {
            auto& p = ctx.engine.project();
            if (v < 0) { ctx.engine.toStart(); return; }
            if (r == 0) p.recordMode = v;
            else if (p.recordMode == 0 && r == 1) p.overdub = v == 1;
            else if (p.recordMode == 0 && r == 2) p.countIn = v == 1;
            else if (p.recordMode == 0 && r == 3) p.recordQuantize = kQuant[juce::jlimit (0, 3, v)];
            else if (r == 1) p.stepLength = juce::jlimit (0, 3, v);
            else if (r == 2) { if (v == 0) ctx.engine.stepRest(); else { ctx.snapshot (ctx.focusPattern()); ctx.engine.stepBack(); } }
            ctx.markDirty();
            repaint();
        }
        int valueOf (int r) const
        {
            const auto& p = ctx.engine.project();
            if (r == 0) return p.recordMode;
            if (p.recordMode == 0) return r == 1 ? (int) p.overdub : r == 2 ? (int) p.countIn : quantIndex();
            return r == 1 ? p.stepLength : 0;
        }
        bool keyPressed (const juce::KeyPress& k) override
        {
            const int code = k.getKeyCode();
            if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) { row = (row + (code == juce::KeyPress::upKey ? rows() - 1 : 1)) % rows(); repaint(); return true; }
            const int d = code == juce::KeyPress::leftKey ? -1 : (code == juce::KeyPress::rightKey ? 1 : 0);
            const bool step = ctx.engine.project().recordMode == 1;
            if (step && row == 2 && (d != 0 || code == juce::KeyPress::returnKey)) { apply (2, d < 0 ? 1 : 0); return true; }   // ← Back, → / Enter Rest
            if (d == 0) return false;
            const int count = row == 0 ? 2 : (! step && row == 3) || (step && row == 1) ? 4 : 2;
            apply (row, juce::jlimit (0, count - 1, valueOf (row) + d));
            return true;
        }
        void paint (juce::Graphics& g) override
        {
            const auto& p = ctx.engine.project();
            zones.clear();
            g.fillAll (col::surface);
            paint::text (g, "RECORDING", 16, 26, 11, Weight::Black, col::quiet);
            const paint::R ts (getWidth() - 46.0f, 10, 32, 22);
            g.setColour (col::control); g.fillRoundedRectangle (ts, 11);
            paint::toStartIcon (g, ts.getCentreX(), ts.getCentreY(), 10, col::text);
            zones.push_back ({ ts, 0, -1 });
            auto seg = [&] (int r, paint::R area, const juce::StringArray& opts, int sel, float size)
            {
                const auto rs = paint::segmented (g, area, opts, sel, col::record, size);
                for (size_t i = 0; i < rs.size(); ++i) zones.push_back ({ rs[i], r, (int) i });
            };
            seg (0, { 16, 42, getWidth() - 32.0f, 30 }, { "Live", "Step" }, p.recordMode, 12);
            const float w = getWidth() - 126.0f;
            std::vector<float> ys { 42 };
            if (p.recordMode == 0)
            {
                const char* labels[3] = { "Takes", "Count-in", "Quantize" };
                for (int i = 0; i < 3; ++i) { paint::text (g, labels[i], 16, 88 + i * 44 + 19, 12, Weight::Bold, col::quiet); ys.push_back (88 + i * 44.0f); }
                seg (1, { 110, 88, w, 28 }, { "Extend", "Overdub" }, (int) p.overdub, 11);
                seg (2, { 110, 132, w, 28 }, { "Off", "1 bar" }, (int) p.countIn, 11);
                seg (3, { 110, 176, w, 28 }, { "Off", "50%", "75%", "100%" }, quantIndex(), 11);
                paint::text (g, "Extend: the pattern grows while you record (1, 2, 4 ... bars)", 16, getHeight() - 40.0f, 10, Weight::Bold, col::faint);
                paint::text (g, "Overdub: the pattern loops and layers", 16, getHeight() - 22.0f, 10, Weight::Bold, col::faint);
            }
            else
            {
                paint::text (g, "Length", 16, 88 + 19, 12, Weight::Bold, col::quiet);
                seg (1, { 110, 88, w, 28 }, { "1/16", "1/8", "1/4", "1/2" }, p.stepLength, 11);
                float rx = 110;
                for (int i = 0; i < 2; ++i)
                {
                    const auto label = i == 0 ? "Rest" : "Back";
                    const float bw = paint::pill (g, rx, 132, label, 11, col::control, col::text, 28, 14);
                    zones.push_back ({ { rx, 132, bw, 28 }, 2, i });
                    rx += bw + 6;
                }
                ys.push_back (88); ys.push_back (132);
                const int total = std::max (1, p.patternSteps (ctx.focusPattern()));
                paint::text (g, "Cursor", 16, 190, 12, Weight::Bold, col::quiet);
                paint::text (g, "step " + juce::String (ctx.engine.stepRecordCursor.load() % total + 1) + " of " + juce::String (total), 110, 190, 12, Weight::Black, col::record);
                paint::text (g, "Hold keys together for a chord", 16, getHeight() - 22.0f, 10, Weight::Bold, col::faint);
            }
            if (hasKeyboardFocus (false) && row < (int) ys.size())
            {
                g.setColour (col::text.withAlpha (0.8f));
                g.drawRoundedRectangle (8, ys[(size_t) row] - 5, getWidth() - 16.0f, 38, 8, 1.5f);
            }
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            for (auto& z : zones) if (z.r.contains (e.position)) { row = std::max (0, z.row); apply (z.row, z.value); return; }
        }
    };
    return std::make_unique<Panel> (ctx);
}

void TopBar::showRecording()
{
    juce::CallOutBox::launchAsynchronously (createRecordingPanel(), { 389, 20, 13, 24 }, this);
}

void TopBar::showTimeSignature()
{
    struct Panel : juce::Component
    {
        explicit Panel (AppContext& c) : ctx (c) { setSize (300, 210 + kY); setWantsKeyboardFocus (true); }
        AppContext& ctx;
        int row = 0;   // keys: ↑ ↓ a row (common, beats, unit, swing, count-in), ← → its value
        void visibilityChanged() override { if (isShowing()) grabKeyboardFocus(); }
        void parentHierarchyChanged() override { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<Panel> (this)] { if (sp != nullptr && sp->isShowing()) sp->grabKeyboardFocus(); }); }
        int commonIndex() const
        {
            auto& p = ctx.engine.project();
            for (int i = 0; i < 6; ++i) if (kCommon[i][0] == p.beats && kCommon[i][1] == p.unit) return i;
            return -1;
        }
        static juce::Rectangle<float> commonRect (int i) { return { 16.0f + i * 45.0f, 14.0f, 41.0f, 30.0f }; }
        bool keyPressed (const juce::KeyPress& k) override
        {
            const int code = k.getKeyCode();
            if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) { row = (row + (code == juce::KeyPress::upKey ? 4 : 1)) % 5; repaint(); return true; }
            const int d = code == juce::KeyPress::leftKey ? -1 : (code == juce::KeyPress::rightKey ? 1 : 0);
            if (d == 0) return false;
            auto& p = ctx.engine.project();
            int beats = p.beats, unit = p.unit;
            if (row == 0) { const int i = juce::jlimit (0, 5, commonIndex() < 0 ? (d > 0 ? 0 : 5) : commonIndex() + d); beats = kCommon[i][0]; unit = kCommon[i][1]; }
            else if (row == 1) beats = juce::jlimit (1, 16, beats + d);
            else if (row == 2) unit = d > 0 ? (unit == 4 ? 8 : 16) : (unit == 16 ? 8 : 4);
            else if (row == 3) p.swing = juce::jlimit (0.5f, 0.75f, p.swing + d * 0.01f);
            else p.countIn = d > 0;
            if (beats != p.beats || unit != p.unit) ctx.engine.setTimeSignature (beats, unit);
            ctx.markDirty();
            repaint();
            return true;
        }
        void paint (juce::Graphics& g) override
        {
            auto& p = ctx.engine.project();
            g.fillAll (col::surface);
            const int ci = commonIndex();
            for (int i = 0; i < 6; ++i)
            {
                const auto r = commonRect (i);
                g.setColour (i == ci ? col::text : col::control); g.fillRoundedRectangle (r, 15);
                paint::text (g, juce::String (kCommon[i][0]) + "/" + juce::String (kCommon[i][1]), r.getCentreX(), r.getY() + 20, 12, Weight::Black,
                             i == ci ? col::bg : col::text, juce::Justification::horizontallyCentred);
            }
            auto row = [&] (int y, const juce::String& label) { paint::text (g, label, 16, (float) y + 19, 12, Weight::Bold, col::quiet); };
            row (kY + 14, "Beats");
            g.setColour (col::control); g.fillRoundedRectangle (140, kY + 14, 144, 30, 15);
            paint::chevron (g, 156, kY + 29, 8, col::quiet, 'l'); paint::chevron (g, 268, kY + 29, 8, col::quiet, 'r');
            paint::text (g, juce::String (p.beats), 212, kY + 34, 14, Weight::Black, col::text, juce::Justification::horizontallyCentred);
            row (kY + 60, "Unit");
            paint::segmented (g, { 140, kY + 60, 144, 30 }, { "4", "8", "16" }, p.unit == 4 ? 0 : (p.unit == 8 ? 1 : 2), col::text, 12);
            row (kY + 106, "Swing");
            const float v = (p.swing - 0.5f) / 0.25f;
            g.setColour (col::control); g.fillRoundedRectangle (140, kY + 118, 144, 6, 3);
            g.setColour (col::text); g.fillRoundedRectangle (140, kY + 118, 144 * v, 6, 3); g.fillEllipse (140 + 144 * v - 8, kY + 113, 16, 16);
            paint::text (g, juce::String (juce::roundToInt (p.swing * 100)) + "%", 284, kY + 103, 11, Weight::Bold, col::quiet, juce::Justification::right);
            row (kY + 152, "Count-in");
            paint::segmented (g, { 140, kY + 152, 144, 30 }, { "Off", "1 bar" }, p.countIn ? 1 : 0, col::text, 12);
            if (hasKeyboardFocus (false))
            {
                const int ys[5] = { 14, kY + 14, kY + 60, kY + 106, kY + 152 };
                g.setColour (col::text.withAlpha (0.8f)); g.drawRoundedRectangle (8, (float) ys[this->row] - 4, 284, 38, 8, 1.5f);
            }
        }
        void mouseDown (const juce::MouseEvent& e) override { handle (e); }
        void mouseDrag (const juce::MouseEvent& e) override { if (e.position.y > kY + 100 && e.position.y < kY + 140) handle (e); }
        void handle (const juce::MouseEvent& e)
        {
            auto& p = ctx.engine.project();
            const auto pt = e.position;
            int beats = p.beats, unit = p.unit;
            for (int i = 0; i < 6; ++i) if (commonRect (i).contains (pt)) { beats = kCommon[i][0]; unit = kCommon[i][1]; }
            const float y = pt.y - kY;
            if (y >= 14 && y < 44) { if (pt.x > 140 && pt.x < 180) beats = std::max (1, beats - 1); if (pt.x > 250 && pt.x < 284) beats = std::min (16, beats + 1); }
            else if (y >= 60 && y < 90 && pt.x > 140) unit = pt.x < 188 ? 4 : (pt.x < 236 ? 8 : 16);
            else if (y >= 100 && y < 140 && pt.x > 132) p.swing = 0.5f + 0.25f * juce::jlimit (0.0f, 1.0f, (pt.x - 140) / 144.0f);
            else if (y >= 152 && y < 182 && pt.x > 140) p.countIn = pt.x > 212;
            if (beats != p.beats || unit != p.unit) ctx.engine.setTimeSignature (beats, unit);
            ctx.markDirty();
            repaint();
        }
    };
    juce::CallOutBox::launchAsynchronously (std::make_unique<Panel> (ctx), zoneRect ("sig").toNearestInt(), this);
}

void TopBar::mouseDown (const juce::MouseEvent& e)
{
    learnPress = learnHooks().active;
    if (learnPress) { learnHooks().press (learnTarget (zoneAt (e.position)), e); return; }
    if (zoneAt (e.position) == "bpm") { draggingBpm = true; bpmAtDown = ctx.engine.project().bpm; }
}

void TopBar::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingBpm) return;
    const double step = e.mods.isShiftDown() ? 0.02 : 0.25;
    ctx.engine.project().bpm = juce::jlimit (30.0, 300.0, std::round ((bpmAtDown - e.getDistanceFromDragStartY() * step) * 10.0) / 10.0);
    ctx.markDirty();
    repaint();
}

void TopBar::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (learnHooks().active) return;
    if (zoneAt (e.position) != "bpm") return;
    askForText ("Tempo (BPM)", juce::String (ctx.engine.project().bpm, 1), [this] (juce::String s)
    {
        const double v = s.getDoubleValue();
        if (v >= 30.0 && v <= 300.0) { ctx.engine.project().bpm = std::round (v * 10.0) / 10.0; ctx.markDirty(); repaint(); }
    });
}

void TopBar::mouseUp (const juce::MouseEvent& e)
{
    const bool wasDragging = draggingBpm && e.getDistanceFromDragStart() > 2;
    draggingBpm = false;
    if (wasDragging || std::exchange (learnPress, false)) return;
    trigger (zoneAt (e.position), e.mods);
}

void TopBar::trigger (const juce::String& id, juce::ModifierKeys mods)
{
    auto& eng = ctx.engine;
    auto& proj = eng.project();
    const double inc = mods.isShiftDown() ? 0.1 : 1.0;
    if (id == "rack" && onView) onView (View::Rack);
    else if (id == "seq" && onView) onView (View::Seq);
    else if ((id == "rec" || id == "recstep") && mods.isRightButtonDown()) showRecording();
    else if (id == "rec" || id == "recstep") toggleRecord (id == "recstep" ? 1 : 0);
    else if (id == "recmenu") showRecording();
    else if (id == "play") { if (eng.isPlaying()) eng.stop(); else eng.play(); }
    else if (id == "stop") eng.stop();
    else if (id == "bpm-") { proj.bpm = juce::jlimit (30.0, 300.0, std::round ((proj.bpm - inc) * 10.0) / 10.0); ctx.markDirty(); }
    else if (id == "bpm+") { proj.bpm = juce::jlimit (30.0, 300.0, std::round ((proj.bpm + inc) * 10.0) / 10.0); ctx.markDirty(); }
    else if (id == "tap") tap();
    else if (id == "bpm")
        askForText ("Tempo (BPM)", juce::String (proj.bpm, 1), [this] (juce::String s)
        {
            const double v = s.getDoubleValue();
            if (v >= 30.0 && v <= 300.0) { ctx.engine.project().bpm = std::round (v * 10.0) / 10.0; ctx.markDirty(); repaint(); }
        });
    else if (id == "sig") showTimeSignature();
    else if (id == "click") { proj.metronome = ! proj.metronome; ctx.markDirty(); }
    else if (id == "playdrawer" && onPlayDrawer) onPlayDrawer();
    else if (id == "perform") { proj.perform = ! proj.perform; ctx.markDirty(); }
    else if (id.startsWith ("pat"))
    {
        const int p = id.getLastCharacters (1).getIntValue();
        if (mods.isRightButtonDown())
        {
            // the pad's menu (a right-click, or Shift+F10 / Delete on the focused pad): what Shift- and Alt-click do
            juce::PopupMenu m;
            m.addSectionHeader ("Pattern " + juce::String::charToString ("ABCDEFGH"[p]));
            m.addItem (1, "Switch to it");
            m.addItem (2, "Copy the current pattern here", p != eng.activePattern());
            m.addItem (3, "Clear it");
            juce::Rectangle<float> padR;
            for (auto& z : zones) if (z.id == id) padR = z.r;
            m.showMenuAsync (menuAt (*this, padR), [this, p] (int r)
            {
                if (r == 1) trigger ("pat" + juce::String (p));
                if (r == 2) trigger ("pat" + juce::String (p), juce::ModifierKeys (juce::ModifierKeys::shiftModifier));
                if (r == 3) trigger ("pat" + juce::String (p), juce::ModifierKeys (juce::ModifierKeys::altModifier));
            });
        }
        else if (mods.isShiftDown()) { eng.copyPattern (eng.activePattern(), p); ctx.markDirty(); if (ctx.onDevicesChanged) ctx.onDevicesChanged(); }
        else if (mods.isAltDown())
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Clear pattern " + juce::String::charToString ("ABCDEFGH"[p]) + "?",
                                                "Its steps and devices are removed.", "Clear", "Cancel", nullptr,
                                                juce::ModalCallbackFunction::create ([this, p] (int r)
                                                {
                                                    if (r != 1) return;
                                                    ctx.engine.clearPattern (p); ctx.markDirty();
                                                    if (ctx.onDevicesChanged) ctx.onDevicesChanged();
                                                }));
        else if (proj.perform && eng.isPlaying())
        {
            // performance: the pick waits for the playing pattern's end (again: cancel); the view stays on what plays
            eng.queuePattern (p == eng.queuedPattern() ? -1 : p);
        }
        else
        {
            eng.queuePattern (p);
            eng.focusPattern = p;   // the rack and editors show the pattern you picked
            if (ctx.onDevicesChanged) ctx.onDevicesChanged();
        }
    }
    else if (id == "rename" && onRename) onRename();
    else if (id == "new" && onNew) onNew();
    else if (id == "open" && onOpen) onOpen();
    else if (id == "save" && onSave) onSave();
    else if (id == "export" && onExport) onExport();
    else if (id == "menu" && onMenu) onMenu();
    repaint();
}
} // namespace mb::ui
