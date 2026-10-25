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
      << juce::roundToInt (e.patternProgress() * 60) << e.project().bpm << e.project().beats << e.project().unit << (int) e.project().metronome
      << projectName << (int) ctx.dirty << (int) playOpen;
    for (int p = 0; p < 4; ++p) for (int t = 0; t < 4; ++t) s << (e.device (p, t) != nullptr ? 1 : 0);
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
    // tempo
    g.setColour (col::control); g.fillEllipse (522 - 14, 32 - 14, 28, 28);
    paint::minus (g, 522, 32, 9, col::quiet);
    zone ({ 508, 18, 28, 28 }, "bpm-", "Tempo down (Shift: 0.1)");
    paint::text (g, juce::String (proj.bpm, 1), 584, 37, 22, Weight::Black, col::text, juce::Justification::horizontallyCentred);
    paint::text (g, "BPM", 584, 54, 9, Weight::Bold, col::faint, juce::Justification::horizontallyCentred);
    zone ({ 546, 10, 76, 46 }, "bpm", "Drag to change, double-click to type");
    g.setColour (col::control); g.fillEllipse (646 - 14, 32 - 14, 28, 28);
    paint::plus (g, 646, 32, 9, col::quiet);
    zone ({ 632, 18, 28, 28 }, "bpm+", "Tempo up (Shift: 0.1)");
    const float tapW = paint::pill (g, 670, 18, "Tap", 12, col::control, col::text, 28);
    zone ({ 670, 18, tapW, 28 }, "tap", "Tap tempo");
    const auto sigText = juce::String (proj.beats) + "/" + juce::String (proj.unit);
    const float sw = paint::pill (g, 738, 18, sigText, 13, col::control, col::text, 28, 12, Weight::Bold, true);
    zone ({ 738, 18, sw, 28 }, "sig", "Time signature, swing and count-in");
    g.setColour (proj.metronome ? col::text : col::control); g.fillEllipse (832 - 16, 32 - 16, 32, 32);
    paint::metronome (g, 832, 32, 15, proj.metronome ? col::bg : col::text);
    zone ({ 816, 16, 32, 32 }, "click", "Metronome");
    // the Play drawer: scale lock, chord mode, arp and how recording works
    g.setColour (playOpen ? col::pattern : col::control); g.fillEllipse (872 - 16, 32 - 16, 32, 32);
    paint::keysIcon (g, 872, 32, 17, playOpen ? col::bg : col::text);
    zone ({ 856, 16, 32, 32 }, "playdrawer", "Play: scale lock, chords, arp, Live / Step recording");
    // performance mode: a picked pattern starts when the playing one ends (mockup 16_perform)
    g.setColour (proj.perform ? col::pattern : col::control); g.fillEllipse (912 - 16, 32 - 16, 32, 32);
    {
        juce::Path tri; tri.addTriangle (906, 26, 914, 32, 906, 38);
        g.setColour (proj.perform ? col::bg : col::text); g.fillPath (tri);
        g.fillRoundedRectangle (915.5f, 26, 2.6f, 12, 1);
    }
    zone ({ 896, 16, 32, 32 }, "perform", "Performance mode: a pattern you pick starts when the playing one ends (click it again to cancel)");
    // A–D
    const int cur = e.activePattern(), queued = e.queuedPattern(), focus = ctx.focusPattern();
    for (int i = 0; i < 4; ++i)
    {
        const float bx = 944 + i * 56.0f, by = 10, bw = 50, bh = 44;
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
        paint::text (g, juce::String::charToString ("ABCD"[i]), bx + bw / 2, by + 22, 16, Weight::Black, isCur || isQ ? col::pattern : col::text, juce::Justification::horizontallyCentred);
        std::array<bool, 4> on {};
        for (int t = 0; t < 4; ++t) on[(size_t) t] = e.device (i, t) != nullptr;
        paint::deviceDots (g, bx + bw / 2, by + 33, on, 2.6f, 8);
        if (isQ && e.isPlaying())   // the countdown: the queued pattern starts when the bar fills
        {
            g.setColour (col::pattern);
            g.fillRoundedRectangle (bx + 6, by + bh - 4.5f, (bw - 12) * juce::jlimit (0.0f, 1.0f, e.patternProgress()), 2.5f, 1.2f);
        }
        zone (r, "pat" + juce::String (i), proj.perform ? "Click: next pattern (starts when this one ends; click again to cancel)   Shift-click: copy   Alt-click: clear"
                                                         : "Click: switch (queued to the bar)   Shift-click: copy the current pattern here   Alt-click: clear");
    }
    // project: name and the file actions
    const float px = 1176;
    auto name = projectName;
    if (textWidth (name, 13, Weight::Black) > 74) { while (name.length() > 2 && textWidth (name + "...", 13, Weight::Black) > 74) name = name.dropLastCharacters (1); name << "..."; }
    const bool untitled = projectName == "Untitled";
    paint::text (g, name, px, 37, 13, untitled ? Weight::Bold : Weight::Black, untitled ? col::quiet : col::text);
    zone ({ px, 18, 82, 28 }, "rename", "Rename the project");
    if (ctx.dirty) { g.setColour (col::mute); g.fillEllipse (px + textWidth (name, 13, Weight::Black) + 5, 29, 6, 6); }
    float bx = 1262;
    struct B { const char* label; const char* id; const char* tip; void (*icon) (juce::Graphics&, float, float, float, juce::Colour); };
    const B buttons[] = { { "New", "new", "New project (Ctrl+N)", paint::newIcon }, { "Open", "open", "Open a project (Ctrl+O)", paint::folderIcon },
                          { "Save", "save", "Save the project (Ctrl+S)", paint::saveIcon }, { "Export", "export", "Export the current pattern as a WAV (Ctrl+E)", paint::exportIcon } };
    for (auto& b : buttons)
    {
        const float bw = textWidth (b.label, 11) * 1.12f + 36;
        g.setColour (col::control); g.fillRoundedRectangle (bx, 17, bw, 30, 15);
        b.icon (g, bx + 15, 32, 12, col::text);
        paint::text (g, b.label, bx + 27, 36, 11, Weight::Bold, col::text);
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
                                               "pat0", "pat1", "pat2", "pat3", "save" };
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

void TopBar::showTimeSignature()
{
    struct Panel : juce::Component
    {
        explicit Panel (AppContext& c) : ctx (c) { setSize (300, 210); }
        AppContext& ctx;
        void paint (juce::Graphics& g) override
        {
            auto& p = ctx.engine.project();
            g.fillAll (col::surface);
            auto row = [&] (int y, const juce::String& label) { paint::text (g, label, 16, (float) y + 19, 12, Weight::Bold, col::quiet); };
            row (14, "Beats");
            g.setColour (col::control); g.fillRoundedRectangle (140, 14, 144, 30, 15);
            paint::chevron (g, 156, 29, 8, col::quiet, 'l'); paint::chevron (g, 268, 29, 8, col::quiet, 'r');
            paint::text (g, juce::String (p.beats), 212, 34, 14, Weight::Black, col::text, juce::Justification::horizontallyCentred);
            row (60, "Unit");
            paint::segmented (g, { 140, 60, 144, 30 }, { "4", "8", "16" }, p.unit == 4 ? 0 : (p.unit == 8 ? 1 : 2), col::text, 12);
            row (106, "Swing");
            const float v = (p.swing - 0.5f) / 0.25f;
            g.setColour (col::control); g.fillRoundedRectangle (140, 118, 144, 6, 3);
            g.setColour (col::text); g.fillRoundedRectangle (140, 118, 144 * v, 6, 3); g.fillEllipse (140 + 144 * v - 8, 113, 16, 16);
            paint::text (g, juce::String (juce::roundToInt (p.swing * 100)) + "%", 284, 103, 11, Weight::Bold, col::quiet, juce::Justification::right);
            row (152, "Count-in");
            paint::segmented (g, { 140, 152, 144, 30 }, { "Off", "1 bar" }, p.countIn ? 1 : 0, col::text, 12);
        }
        void mouseDown (const juce::MouseEvent& e) override { handle (e); }
        void mouseDrag (const juce::MouseEvent& e) override { if (e.position.y > 100 && e.position.y < 140) handle (e); }
        void handle (const juce::MouseEvent& e)
        {
            auto& p = ctx.engine.project();
            const auto pt = e.position;
            int beats = p.beats, unit = p.unit;
            if (pt.y >= 14 && pt.y < 44) { if (pt.x > 140 && pt.x < 180) beats = std::max (1, beats - 1); if (pt.x > 250 && pt.x < 284) beats = std::min (16, beats + 1); }
            else if (pt.y >= 60 && pt.y < 90 && pt.x > 140) unit = pt.x < 188 ? 4 : (pt.x < 236 ? 8 : 16);
            else if (pt.y >= 100 && pt.y < 140 && pt.x > 132) p.swing = 0.5f + 0.25f * juce::jlimit (0.0f, 1.0f, (pt.x - 140) / 144.0f);
            else if (pt.y >= 152 && pt.y < 182 && pt.x > 140) p.countIn = pt.x > 212;
            if (beats != p.beats || unit != p.unit) ctx.engine.setTimeSignature (beats, unit);
            ctx.markDirty();
            repaint();
        }
    };
    juce::CallOutBox::launchAsynchronously (std::make_unique<Panel> (ctx), { 738, 18, 70, 28 }, this);
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
    else if (id == "rec" || id == "recstep") toggleRecord (id == "recstep" ? 1 : 0);
    else if (id == "play") { if (eng.isPlaying()) eng.stop(); else eng.play(); }
    else if (id == "stop") eng.stop();
    else if (id == "bpm-") { proj.bpm = juce::jlimit (30.0, 300.0, std::round ((proj.bpm - inc) * 10.0) / 10.0); ctx.markDirty(); }
    else if (id == "bpm+") { proj.bpm = juce::jlimit (30.0, 300.0, std::round ((proj.bpm + inc) * 10.0) / 10.0); ctx.markDirty(); }
    else if (id == "tap") tap();
    else if (id == "sig") showTimeSignature();
    else if (id == "click") { proj.metronome = ! proj.metronome; ctx.markDirty(); }
    else if (id == "playdrawer" && onPlayDrawer) onPlayDrawer();
    else if (id == "perform") { proj.perform = ! proj.perform; ctx.markDirty(); }
    else if (id.startsWith ("pat"))
    {
        const int p = id.getLastCharacters (1).getIntValue();
        if (mods.isShiftDown()) { eng.copyPattern (eng.activePattern(), p); ctx.markDirty(); if (ctx.onDevicesChanged) ctx.onDevicesChanged(); }
        else if (mods.isAltDown())
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Clear pattern " + juce::String::charToString ("ABCD"[p]) + "?",
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
