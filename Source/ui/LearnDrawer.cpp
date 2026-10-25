#include "LearnDrawer.h"
#include "efx/MasterFx.h"

namespace mb::ui
{
using R = juce::Rectangle<float>;

namespace
{
juce::String pct (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
juce::String panText (float v) { const int p = juce::roundToInt (v * 100); return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p)); }
const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");

/** A binding onto values read and written through functions (tempo). */
struct FnBinding : Binding
{
    std::function<float()> get; std::function<void (float)> set; float lo, hi;
    FnBinding (std::function<float()> g, std::function<void (float)> s, float l, float h) : get (std::move (g)), set (std::move (s)), lo (l), hi (h) {}
    float getNorm() const override { return (get() - lo) / (hi - lo); }
    void setNorm (float n) override { set (lo + (hi - lo) * juce::jlimit (0.0f, 1.0f, n)); }
    float defaultNorm() const override { return 0.5f; }
    juce::String text() const override { return juce::String (get(), 1); }
};

/** `kind:track:rest` → track index (the selected track for `sel`), or -1. */
int trackOf (AppContext& ctx, const juce::String& target)
{
    const auto t = target.fromFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf (":", false, false);
    if (t == "sel") return ctx.focusTrack();
    const int i = t.getIntValue();
    return t.containsOnly ("0123456789") && t.isNotEmpty() && i >= 0 && i < kTracks ? i : -1;
}

juce::String lastWord (const juce::String& s) { return s.trim().fromLastOccurrenceOf (" ", false, false); }
} // namespace

const std::vector<std::pair<juce::String, juce::String>>& learnActions()
{
    static const std::vector<std::pair<juce::String, juce::String>> a {
        { "play", "Play / stop" }, { "stop", "Stop" }, { "rec", "Live record" }, { "recstep", "Step record" }, { "tostart", "Back to start" }, { "click", "Metronome" },
        { "tap", "Tap tempo" }, { "bpm-", "Tempo -1" }, { "bpm+", "Tempo +1" },
        { "perform", "Performance mode" }, { "pat0", "Pattern A" }, { "pat1", "Pattern B" }, { "pat2", "Pattern C" }, { "pat3", "Pattern D" },
        { "pat4", "Pattern E" }, { "pat5", "Pattern F" }, { "pat6", "Pattern G" }, { "pat7", "Pattern H" },
        { "track-prev", "Previous instrument" }, { "track-next", "Next instrument" },
        { "bar-prev", "Previous bar" }, { "bar-next", "Next bar" },
        { "preset-prev", "Previous preset" }, { "preset-next", "Next preset" },
        { "undo", "Undo" }, { "redo", "Redo" }, { "save", "Save project" },
        { "rack", "Rack page" }, { "seq", "Seq page" }, { "editor", "Editor of the selected instrument" }, { "playdrawer", "Play drawer" },
        { "step-rest", "Step record: rest" }, { "step-back", "Step record: back" } };
    return a;
}

juce::String learnTargetName (AppContext& ctx, const juce::String& target)
{
    if (target.startsWith ("action:"))
    {
        const auto id = target.fromFirstOccurrenceOf (":", false, false);
        for (auto& [k, n] : learnActions()) if (k == id) return n;
        return id;
    }
    if (target == "tempo") return "Tempo";
    if (target == "swing") return "Swing";
    if (target.startsWith ("perform:")) return juce::String ("Perform") + dot + MasterFx::padName (target.getTrailingIntValue());
    const auto kind = target.upToFirstOccurrenceOf (":", false, false);
    const auto rest = target.fromLastOccurrenceOf (":", false, false);
    if (kind == "master") return "Master" + dot + rest.substring (0, 1).toUpperCase() + rest.substring (1);
    const bool sel = target.contains (":sel");
    const int t = trackOf (ctx, target);
    const juce::String who = sel ? juce::String ("Selected") : "Track " + juce::String (t + 1);
    auto* dev = t >= 0 ? ctx.engine.device (ctx.focusPattern(), t) : nullptr;
    if (kind == "macro")
    {
        const int m = rest.getIntValue();
        if (sel || dev == nullptr) return who + dot + "Macro " + juce::String (m + 1);
        return deviceTypeName (dev->type()) + dot + dev->macros()[(size_t) juce::jlimit (0, 7, m)].name + " (macro " + juce::String (m + 1) + ")";
    }
    if (kind == "param")
    {
        const auto id = target.fromFirstOccurrenceOf (":", false, false).fromFirstOccurrenceOf (":", false, false);
        const int idx = dev != nullptr ? dev->params().indexOf (id) : -1;
        return who + dot + (idx >= 0 ? dev->params()[idx].name : id);
    }
    const auto name = kind == "level" ? "Level" : kind == "pan" ? "Pan" : kind == "delay" ? "Delay send" : kind == "reverb" ? "Reverb send" : kind;
    return who + dot + name;
}

std::unique_ptr<Binding> learnBinding (AppContext& ctx, const juce::String& target)
{
    auto& proj = ctx.engine.project();
    if (target == "tempo")
        return std::make_unique<FnBinding> ([&proj] { return (float) proj.bpm; },
                                            [&ctx, &proj] (float v) { proj.bpm = std::round (v * 10.0) / 10.0; ctx.markDirty(); }, 40.0f, 240.0f);
    if (target.startsWith ("perform:"))
    {
        // a performance pad: on while the mapped pad / key / button is held (Momentary)
        struct PerformBinding : Binding
        {
            AppContext& c; int pad;
            PerformBinding (AppContext& cx, int p) : c (cx), pad (p) {}
            float getNorm() const override { return c.masterFx != nullptr && c.masterFx->isHeld (pad) ? 1.0f : 0.0f; }
            void setNorm (float v) override { if (c.masterFx != nullptr) c.masterFx->setHeld (pad, v > 0.5f); }
            float defaultNorm() const override { return 0.0f; }
            juce::String text() const override { return getNorm() > 0.5f ? "On" : "Off"; }
            int steps() const override { return 2; }
        };
        return std::make_unique<PerformBinding> (ctx, juce::jlimit (0, kPerformFx - 1, target.getTrailingIntValue()));
    }
    if (target == "swing")
        return std::make_unique<FloatBinding> (&proj.swing, 0.5f, 0.75f, 0.5f, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }, false, &ctx);
    const auto kind = target.upToFirstOccurrenceOf (":", false, false);
    const auto rest = target.fromLastOccurrenceOf (":", false, false);
    if (kind == "master")
    {
        auto& m = proj.master;
        if (rest == "volume") return std::make_unique<FloatBinding> (&m.volume, 0, 1.2f, 0.8f, pct, false, &ctx);
        if (rest == "delay") return std::make_unique<FloatBinding> (&m.delayReturn, 0, 1, 0.8f, pct, false, &ctx);
        if (rest == "reverb") return std::make_unique<FloatBinding> (&m.reverbReturn, 0, 1, 0.8f, pct, false, &ctx);
        if (rest == "glue") return std::make_unique<FloatBinding> (&m.glue, 0, 1, 0.3f, pct, false, &ctx);
        if (rest == "limit") return std::make_unique<FloatBinding> (&m.limitDb, -12, 0, -0.3f, pct, false, &ctx);
        return {};
    }
    const int t = trackOf (ctx, target);
    if (t < 0) return {};
    const int pat = ctx.focusPattern();
    auto* dev = ctx.engine.device (pat, t);
    if (dev == nullptr) return {};
    auto& mixer = proj.patterns[(size_t) pat].slots[(size_t) t].mixer;
    if (kind == "macro") return std::make_unique<MacroBinding> (&ctx, dev, juce::jlimit (0, 7, rest.getIntValue()));
    if (kind == "level") return std::make_unique<FloatBinding> (&mixer.level, 0, 1, 0.8f, pct, false, &ctx);
    if (kind == "pan") return std::make_unique<FloatBinding> (&mixer.pan, -1, 1, 0, panText, true, &ctx);
    if (kind == "delay") return std::make_unique<FloatBinding> (&mixer.sendA, 0, 1, 0, pct, false, &ctx);
    if (kind == "reverb") return std::make_unique<FloatBinding> (&mixer.sendB, 0, 1, 0, pct, false, &ctx);
    if (kind == "param")
    {
        const auto id = target.fromFirstOccurrenceOf (":", false, false).fromFirstOccurrenceOf (":", false, false);
        const int idx = dev->params().indexOf (id);
        if (idx >= 0) return std::make_unique<ParamBinding> (&ctx, dev, idx, pat, t);
    }
    return {};
}

void applyLearnControl (Binding& b, const MidiControl& c, MidiMapping& mp)
{
    using T = MidiControl::Type;
    const float cur = b.getNorm();
    float v = cur;
    switch (c.type)
    {
        case T::Set:
            if (mp.pickup)
            {
                // take over once the control reaches (or passes) the current value
                const bool reached = std::abs (c.value - cur) < 0.03f || (mp.lastIn >= 0.0f && (mp.lastIn - cur) * (c.value - cur) <= 0.0f);
                mp.lastIn = c.value;
                if (! reached) return;
            }
            v = c.value;
            break;
        case T::Delta: v = cur + c.value; break;
        case T::Press:
            v = mp.mode == MidiMapping::Mode::Toggle ? (cur > (mp.lo + mp.hi) * 0.5f ? mp.lo : mp.hi) : mp.hi;
            break;
        case T::Release:
            if (mp.mode != MidiMapping::Mode::Momentary) return;
            v = mp.lo;
            break;
    }
    v = juce::jlimit (0.0f, 1.0f, v);
    if (const int s = b.steps(); s > 1) v = std::round (v * (s - 1)) / (float) (s - 1);
    if (std::abs (v - cur) < 1.0e-6f) return;
    b.gestureBegin();
    b.setNorm (v);
    b.gestureEnd();
}

// ------------------------------------------------------------------------------------------- drawer
LearnDrawer::LearnDrawer (AppContext& c, MidiMap& m) : ctx (c), map (m)
{
    // the mapping list scrolls by keys too: Page Up / Page Down
    fb.keyHandler = [this] (int, const juce::KeyPress& k)
    {
        if (k.getKeyCode() != juce::KeyPress::pageUpKey && k.getKeyCode() != juce::KeyPress::pageDownKey) return false;
        scroll += k.getKeyCode() == juce::KeyPress::pageDownKey ? 7 : -7;
        repaint();
        return true;
    };
}

juce::String LearnDrawer::armTarget (const juce::String& target) const
{
    const auto kind = target.upToFirstOccurrenceOf (":", false, false);
    if (! followSelected || ! juce::StringArray { "macro", "level", "pan", "delay", "reverb", "param" }.contains (kind)) return target;
    const auto afterTrack = target.fromFirstOccurrenceOf (":", false, false).fromFirstOccurrenceOf (":", true, false);
    return kind + ":sel" + afterTrack;
}

juce::String LearnDrawer::labelFor (const juce::String& target) const
{
    const auto own = map.sourceFor (target);
    if (own.isNotEmpty()) return own;
    const auto t = target.fromFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf (":", false, false);
    if (t.isNotEmpty() && t.containsOnly ("0123456789") && t.getIntValue() == ctx.focusTrack())
    {
        const auto kind = target.upToFirstOccurrenceOf (":", false, false);
        return map.sourceFor (kind + ":sel" + target.fromFirstOccurrenceOf (":", false, false).fromFirstOccurrenceOf (":", true, false));
    }
    return {};
}

void LearnDrawer::paint (juce::Graphics& g)
{
    zones.clear();
    fb.begin();
    auto zone = [this] (R r, const juce::String& id, int a = 0, float radius = -1.0f) { zones.push_back ({ r, id, a }); fb.hot (r, radius); };
    const float w = (float) getWidth(), h = (float) getHeight();
    g.setColour (col::surface); g.fillRoundedRectangle (0, 0, w, h, 14);
    g.setColour (col::divider); g.drawRoundedRectangle (0.75f, 0.75f, w - 1.5f, h - 1.5f, 14, 1.5f);
    paint::text (g, "MIDI LEARN", 18, 28, 11, Weight::Black, col::quiet);
    paint::text (g, "Click a knob, fader or button, then move a control on your MIDI controller", 120, 28, 12, Weight::Bold, col::quiet);
    g.setColour (col::control); g.fillEllipse (w - 40, 9, 28, 28);
    g.setColour (col::quiet); g.drawLine (w - 31, 18, w - 21, 28, 1.8f); g.drawLine (w - 21, 18, w - 31, 28, 1.8f);
    zone ({ w - 40, 9, 28, 28 }, "close", 0, 14);

    // ---- left: armed control, last message, actions, map files
    const float lx = 14, ly = 44, lw = 420, ph = h - 58;
    g.setColour (juce::Colour (0xff1B1D22)); g.fillRoundedRectangle (lx, ly, lw, ph, 10);
    paint::text (g, "Armed", lx + 14, ly + 26, 11, Weight::Bold, col::quiet);
    const auto armed = map.armedTarget();
    if (armed.isNotEmpty())
    {
        const float pw = paint::pill (g, lx + 70, ly + 10, learnTargetName (ctx, armed), 11, col::pattern, col::bg, 24, 12, Weight::Black);
        zone ({ lx + 70, ly + 10, pw, 24 }, "disarm", 0, 12);
    }
    else paint::text (g, "Nothing: click a control", lx + 70, ly + 26, 11, Weight::Bold, col::faint);
    paint::text (g, "Last received", lx + 14, ly + 62, 11, Weight::Bold, col::quiet);
    auto last = map.lastReceived.isEmpty() ? juce::String ("Waiting for a message") : map.lastReceived;
    if (textWidth (last, 12, Weight::Black) > lw - 134)
    {
        while (textWidth (last + "...", 12, Weight::Black) > lw - 134 && last.length() > 4) last = last.dropLastCharacters (1);
        last = last.trimEnd() + "...";
    }
    paint::text (g, last, lx + 120, ly + 62, 12, Weight::Black, map.lastReceived.isEmpty() ? col::faint : col::text);
    float bx = lx + 14;
    auto pillAt = [&] (float x, float y, const juce::String& label, bool caret, const juce::String& id, juce::Colour text = col::quiet)
    {
        const float pw = paint::pill (g, x, y, label, 11, col::control, text, 28, 12, Weight::Bold, caret);
        zone ({ x, y, pw, 28 }, id, 0, 14);
        return pw;
    };
    bx += pillAt (bx, ly + 86, "Action", true, "actions") + 6;
    pillAt (bx, ly + 86, followSelected ? "Selected instrument" : "This track", true, "follow");
    paint::text (g, "Map", lx + 14, ly + 142, 11, Weight::Bold, col::quiet);
    pillAt (lx + 56, ly + 126, map.name, true, "maps", col::text);
    bx = lx + 14;
    bx += pillAt (bx, ly + 168, juce::String::fromUTF8 ("Load map\xe2\x80\xa6"), false, "load") + 6;
    bx += pillAt (bx, ly + 168, juce::String::fromUTF8 ("Save map\xe2\x80\xa6"), false, "save") + 6;
    pillAt (bx, ly + 168, "Clear all", false, "clear");
    paint::text (g, "Right-click a mapped control (in learn mode) to remove it", lx + 14, ly + ph - 14, 10, Weight::Bold, col::faint);

    // ---- right: the mappings
    const float rx = lx + lw + 12, rw = w - lw - 40;
    g.setColour (juce::Colour (0xff1B1D22)); g.fillRoundedRectangle (rx, ly, rw, ph, 10);
    const std::pair<const char*, float> cols[] = { { "MIDI", 0 }, { "PORT", 150 }, { "CONTROL", 330 }, { "MODE", rw - 260 } };
    for (auto& [lab, cx] : cols) paint::text (g, lab, rx + 14 + cx, ly + 24, 10, Weight::Black, col::quiet);
    const int n = (int) map.mappings.size(), rows = 7;
    scroll = juce::jlimit (0, std::max (0, n - rows), scroll);
    if (n == 0)
        paint::text (g, "No mappings yet: click a control, then move a knob on your controller (or pick a map)", rx + 14, ly + 60, 11, Weight::Bold, col::faint);
    for (int i = 0; i < std::min (rows, n - scroll); ++i)
    {
        const int k = scroll + i;
        const auto& mp = map.mappings[(size_t) k];
        const float ry = ly + 36 + i * 28;
        if (i % 2 == 0) { g.setColour (col::control.withAlpha (0.5f)); g.fillRoundedRectangle (rx + 6, ry, rw - 12, 26, 6); }
        paint::text (g, mp.messageName(), rx + 14, ry + 17, 11, Weight::Black, col::text);
        paint::text (g, mp.port.isEmpty() ? juce::String ("Any") : lastWord (mp.port), rx + 164, ry + 17, 11, Weight::Bold, col::quiet);
        auto name = learnTargetName (ctx, mp.target);
        if (mp.lo != 0.0f || mp.hi != 1.0f) name << "  (" << pct (mp.lo) << juce::String::fromUTF8 ("\xe2\x80\x93") << pct (mp.hi) << ")";
        if (mp.pickup) name << "  pickup";
        paint::text (g, name, rx + 344, ry + 17, 11, Weight::Bold, col::text);
        const float mx = rx + 14 + rw - 260;
        const float pw = paint::pill (g, mx, ry + 2, MidiMapping::modeName (mp.mode), 10, col::control, col::quiet, 22, 9, Weight::Bold, true);
        zone ({ mx, ry + 2, pw, 22 }, "mode", k, 11);
        paint::text (g, juce::String::fromUTF8 ("\xc3\x97"), rx + rw - 22, ry + 18, 14, Weight::Bold, col::quiet, juce::Justification::horizontallyCentred);
        zone ({ rx + rw - 34, ry + 2, 24, 22 }, "remove", k, 11);
    }
    if (n > rows)
    {
        // scroll bar
        const float th = ph - 48, top = ly + 36;
        g.setColour (col::control); g.fillRoundedRectangle (rx + rw - 6, top, 3, th, 1.5f);
        g.setColour (col::quiet); g.fillRoundedRectangle (rx + rw - 6, top + th * scroll / n, 3, th * rows / n, 1.5f);
    }
    fb.paint (g);
}

void LearnDrawer::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    scroll += w.deltaY < 0 ? 1 : -1;
    repaint();
}

void LearnDrawer::mouseUp (const juce::MouseEvent& e)
{
    menuArea = fb.hotAt (e.position);
    const Zone* z = nullptr;
    for (auto& zz : zones) if (zz.r.contains (e.position)) z = &zz;
    if (z == nullptr) return;
    const auto id = z->id;
    if (id == "close") { if (onClose) onClose(); return; }
    if (id == "disarm") map.arm ({});
    else if (id == "actions") showActions();
    else if (id == "follow") { followSelected = ! followSelected; }
    else if (id == "maps") showMaps();
    else if (id == "clear")
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Clear all mappings?", "Every mapping of the current map is removed.",
                                            "Clear", "Cancel", nullptr, juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) map.clear(); }));
    else if (id == "remove") map.remove (z->a);
    else if (id == "mode") showMode (z->a);
    else if (id == "save" || id == "load") fileAction (id == "save");
    repaint();
}

void LearnDrawer::fileAction (bool save)
{
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        chooser = std::make_unique<juce::FileChooser> (save ? "Save MIDI map" : "Load MIDI map",
                                                       save ? dir.getChildFile (juce::File::createLegalFileName (map.name) + ".mbmidi") : dir, "*.mbmidi");
        const int flags = save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                               : juce::FileBrowserComponent::openMode;
        chooser->launchAsync (flags | juce::FileBrowserComponent::canSelectFiles, [this, save] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            if (save)
            {
                f = f.withFileExtension (".mbmidi");
                map.name = f.getFileNameWithoutExtension();
                if (! map.save (f)) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI map", "Can't write " + f.getFullPathName());
                map.changed();
            }
            else if (! map.load (f)) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI map", "Not a MIDI map: " + f.getFileName());
            repaint();
        });
    }
}

void LearnDrawer::showActions()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Arm an action, then press a button");
    const auto& acts = learnActions();
    for (size_t i = 0; i < acts.size(); ++i)
    {
        const auto src = map.sourceFor ("action:" + acts[i].first);
        m.addItem ((int) i + 1, acts[i].second + (src.isNotEmpty() ? "   (" + src + ")" : juce::String()), true, map.armedTarget() == "action:" + acts[i].first);
    }
    m.addSeparator();
    m.addItem (1000, "Tempo (a knob or fader)" + (map.sourceFor ("tempo").isNotEmpty() ? "   (" + map.sourceFor ("tempo") + ")" : juce::String()));
    m.showMenuAsync (menuAt (*this, menuArea), [this] (int r)
    {
        if (r == 1000) map.arm ("tempo");
        else if (r > 0) map.arm ("action:" + learnActions()[(size_t) r - 1].first);
        repaint();
    });
}

void LearnDrawer::showMaps()
{
    juce::PopupMenu m;
    const auto files = MidiMap::factoryMaps();
    m.addSectionHeader ("Factory maps");
    for (int i = 0; i < files.size(); ++i) m.addItem (i + 1, files[i].getFileNameWithoutExtension(), true, files[i].getFileNameWithoutExtension() == map.name);
    if (files.isEmpty()) m.addItem (-1, "(none found)", false);
    m.addSeparator();
    m.addItem (500, juce::String::fromUTF8 ("Load map\xe2\x80\xa6"));
    m.addItem (501, juce::String::fromUTF8 ("Save map\xe2\x80\xa6"));
    m.addItem (502, "New empty map");
    m.showMenuAsync (menuAt (*this, menuArea), [this, files] (int r)
    {
        if (r >= 1 && r <= files.size()) map.load (files[r - 1]);
        if (r == 500 || r == 501) fileAction (r == 501);
        if (r == 502) map.clear();
        repaint();
    });
}

void LearnDrawer::showMode (int index) { showMappingModeMenu (map, index, [this] { repaint(); }, this, menuArea); }

void showMappingModeMenu (MidiMap& map, int index, std::function<void()> after, juce::Component* at, juce::Rectangle<float> area)
{
    if (index < 0 || index >= (int) map.mappings.size()) return;
    const auto& mp = map.mappings[(size_t) index];
    using M = MidiMapping::Mode;
    juce::PopupMenu m;
    const bool action = mp.target.startsWith ("action:");
    std::vector<M> modes;
    if (action) modes = { M::Trigger };
    else if (mp.kind == MidiMapping::Kind::CC) modes = { M::Absolute, M::RelativeTwos, M::RelativeOffset, M::RelativeSign, M::Toggle, M::Momentary, M::Trigger };
    else if (mp.kind == MidiMapping::Kind::Note) modes = { M::Toggle, M::Momentary, M::Trigger, M::Absolute };
    else modes = { M::Absolute };
    for (auto md : modes)
        m.addItem (1 + (int) md, MidiMapping::modeName (md) + (md == M::Absolute && mp.kind == MidiMapping::Kind::Note ? " (velocity)" : juce::String()),
                   true, mp.mode == md);
    if (! action)
    {
        m.addSeparator();
        m.addItem (100, "Pickup (wait for the control to reach the value)", mp.mode == M::Absolute, mp.pickup);
        juce::PopupMenu range;
        const std::pair<float, float> ranges[] = { { 0, 1 }, { 0, 0.5f }, { 0.5f, 1 }, { 0.25f, 0.75f }, { 1, 0 } };
        const char* names[] = { "Full", "Lower half", "Upper half", "Middle", "Inverted" };
        for (int i = 0; i < 5; ++i) range.addItem (200 + i, names[i], true, mp.lo == ranges[i].first && mp.hi == ranges[i].second);
        m.addSubMenu ("Range", range);
    }
    m.showMenuAsync (at != nullptr ? menuAt (*at, area) : juce::PopupMenu::Options(), [&map, index, after] (int r)
    {
        if (index >= (int) map.mappings.size() || r <= 0) return;
        auto& mp = map.mappings[(size_t) index];
        if (r <= 7) mp.mode = (M) (r - 1);
        else if (r == 100) mp.pickup = ! mp.pickup;
        else if (r >= 200 && r < 205)
        {
            const std::pair<float, float> ranges[] = { { 0, 1 }, { 0, 0.5f }, { 0.5f, 1 }, { 0.25f, 0.75f }, { 1, 0 } };
            mp.lo = ranges[r - 200].first; mp.hi = ranges[r - 200].second;
        }
        mp.held = false; mp.lastIn = -1.0f;
        map.changed();
        if (after) after();
    });
}

juce::PopupMenu learnTargetMenu (AppContext& ctx, std::vector<juce::String>& ids)
{
    // every target a controller can drive: the selected instrument, each track, the master, tempo, actions
    ids.clear();
    auto item = [&] (juce::PopupMenu& m, const juce::String& id, const juce::String& name) { ids.push_back (id); m.addItem ((int) ids.size(), name); };
    juce::PopupMenu m, sel, master, actions;
    for (int k = 0; k < 8; ++k) item (sel, "macro:sel:" + juce::String (k), "Macro " + juce::String (k + 1));
    for (auto [kind, name] : { std::pair<const char*, const char*> { "level", "Level" }, { "pan", "Pan" }, { "delay", "Delay send" }, { "reverb", "Reverb send" } })
        item (sel, juce::String (kind) + ":sel", name);
    m.addSubMenu ("Selected instrument", sel);
    for (int t = 0; t < kTracks; ++t)
    {
        juce::PopupMenu tm;
        for (auto [kind, name] : { std::pair<const char*, const char*> { "level", "Level" }, { "pan", "Pan" }, { "delay", "Delay send" }, { "reverb", "Reverb send" } })
            item (tm, juce::String (kind) + ":" + juce::String (t), name);
        for (int k = 0; k < 8; ++k) item (tm, "macro:" + juce::String (t) + ":" + juce::String (k), "Macro " + juce::String (k + 1));
        m.addSubMenu ("Track " + juce::String (t + 1), tm);
    }
    for (auto name : { "volume", "delay", "reverb", "glue", "limit" }) item (master, "master:" + juce::String (name), learnTargetName (ctx, "master:" + juce::String (name)));
    m.addSubMenu ("Master", master);
    item (m, "tempo", "Tempo");
    item (m, "swing", "Swing");
    juce::PopupMenu perform;
    for (int i = 0; i < kPerformFx; ++i) item (perform, "perform:" + juce::String (i), juce::String (i + 1) + "  " + MasterFx::padName (i) + " (hold)");
    m.addSubMenu ("Performance effects", perform);
    for (auto& [id, name] : learnActions()) item (actions, "action:" + id, name);
    m.addSubMenu ("Actions", actions);
    return m;
}
} // namespace mb::ui
