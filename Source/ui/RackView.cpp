#include "RackView.h"
#include "efx/MasterFx.h"
#include "model/MidiMap.h"
#include "app/presets/PresetBank.h"
#include "EditorView.h"

namespace mb::ui
{
using namespace geo;

namespace
{
juce::String pct (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
juce::String panText (float v) { const int p = juce::roundToInt (v * 100); return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p)); }

class Popover : public juce::Component
{
public:
    explicit Popover (std::vector<std::unique_ptr<KnobView>> k) : knobs (std::move (k))
    {
        for (auto& kv : knobs) addAndMakeVisible (*kv);
        setSize (40 + (int) knobs.size() * 86, 110);
    }
    void paint (juce::Graphics& g) override { g.fillAll (col::surface); }
    void resized() override { for (size_t i = 0; i < knobs.size(); ++i) knobs[i]->setBounds (20 + (int) i * 86, 14, 86, 90); }
    std::vector<std::unique_ptr<KnobView>> knobs;
};
} // namespace

void showKnobPopover (juce::Component* anchor, juce::Rectangle<int> area, std::vector<std::unique_ptr<KnobView>> knobs)
{
    auto p = std::make_unique<Popover> (std::move (knobs));
    juce::CallOutBox::launchAsynchronously (std::move (p), area, anchor);
}

// ------------------------------------------------------------------------------------------- slot strip
SlotStrip::SlotStrip (AppContext& c, int t) : ctx (c), track (t) {}

void SlotStrip::rebuild()
{
    macros.clear(); mixKnobs.clear(); fader.reset();
    const int pat = ctx.focusPattern();
    built = ctx.engine.device (pat, track);
    builtPattern = pat;
    const auto colour = col::track (track);
    if (built != nullptr)
        for (int m = 0; m < 8; ++m)
        {
            auto b = std::make_unique<MacroBinding> (&ctx, built, m);
            b->learnId = "macro:" + juce::String (track) + ":" + juce::String (m);
            auto kv = std::make_unique<KnobView> (std::move (b), built->macros()[(size_t) m].name, colour, 22.0f, true);
            addAndMakeVisible (*kv);
            macros.push_back (std::move (kv));
        }
    auto& mixer = ctx.engine.project().patterns[(size_t) pat].slots[(size_t) track].mixer;
    if (built != nullptr)
    {
        auto add = [&] (float* v, float lo, float hi, float def, std::function<juce::String (float)> fmt, bool bi, const juce::String& lab)
        {
            auto b = std::make_unique<FloatBinding> (v, lo, hi, def, fmt, bi, &ctx);
            b->learnId = (lab == "Pan" ? "pan:" : lab == "Delay" ? "delay:" : "reverb:") + juce::String (track);
            auto kv = std::make_unique<KnobView> (std::move (b), lab, colour, 15.0f);
            kv->labelSize = 10.0f;
            addAndMakeVisible (*kv);
            mixKnobs.push_back (std::move (kv));
        };
        add (&mixer.pan, -1, 1, 0, panText, true, "Pan");
        add (&mixer.sendA, 0, 1, 0, pct, false, "Delay");
        add (&mixer.sendB, 0, 1, 0, pct, false, "Reverb");
        auto level = std::make_unique<FloatBinding> (&mixer.level, 0, 1, 0.8f, pct, false, &ctx);
        level->learnId = "level:" + juce::String (track);
        fader = std::make_unique<FaderView> (std::move (level), colour);
        fader->meter = [this]
        {
            const float p = ctx.engine.slotPeak[ctx.focusPattern()][track].load();
            const float v = juce::jlimit (0.0f, 1.0f, (dsp::gainToDb (p) + 48.0f) / 48.0f);
            return std::pair<float, float> { v, v * 0.94f };
        };
        addAndMakeVisible (*fader);
    }
    resized();
    repaint();
}

void SlotStrip::resized()
{
    const float h = (float) getHeight(), w = (float) getWidth();
    // a wider window (16:9 …) spreads the macros and widens the visual; the mixer and the open button stay right
    const float extra = std::max (0.0f, w - 1544.0f);
    const float mx = 300.0f, sp = 86.0f + extra * 0.5f / 8.0f, vw = 280.0f + extra * 0.5f;
    for (size_t i = 0; i < macros.size(); ++i)
        macros[i]->setBounds (juce::Rectangle<float> (mx + i * sp - 43, 70 - 28, 86, 104).toNearestInt());
    const float vx = mx + 8 * sp - 24;
    visR = { vx, 16, vw, h - 32 };
    const float lx = vx + vw + 36;
    if (fader) fader->setBounds (juce::Rectangle<float> (lx - 11, 16, 40, h - 32).toNearestInt());
    const float pos[3][2] = { { lx + 70, 46 }, { lx + 126, 46 }, { lx + 70, 116 } };
    for (size_t i = 0; i < mixKnobs.size(); ++i)
        mixKnobs[i]->setBounds (juce::Rectangle<float> (pos[i][0] - 30, pos[i][1] - 21, 60, 64).toNearestInt());
    openR = { w - 74, h / 2 - 28, 56, 56 };
}

void SlotStrip::paint (juce::Graphics& g)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const auto colour = col::track (track);
    auto* d = built;
    fb.begin();
    if (d == nullptr)
    {
        g.setColour (col::surface.withAlpha (0.5f));
        g.fillRoundedRectangle (0, 0, w, h, 12);
        juce::Path outline;
        outline.addRoundedRectangle (0.75f, 0.75f, w - 1.5f, h - 1.5f, 12.0f);
        const float dash[] = { 6.0f, 6.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (outline, outline, dash, 2);
        g.setColour (col::divider); g.fillPath (outline);
        paint::trackBadge (g, 32, 34, track);
        paint::text (g, "Empty", 56, 41, 20, Weight::Black, col::faint);
        paint::chevron (g, 72 + textWidth ("Empty", 20, Weight::Black), 34, 9, col::faint);
        nameR = { 20, 14, 160, 40 };
        plusR = { w / 2 - 26, h / 2 - 26, 52, 52 };
        g.setColour (col::control); g.fillEllipse (plusR);
        paint::plus (g, w / 2, h / 2, 16, col::quiet, 2.4f);
        fb.hot (nameR); fb.hot (plusR, 26);
        fb.paint (g);
        return;
    }
    paint::card (g, getLocalBounds().toFloat());
    g.setColour (colour); g.fillRoundedRectangle (0, 14, 4, h - 28, 2);
    paint::trackBadge (g, 32, 34, track);
    const auto name = deviceTypeName (d->type());
    paint::text (g, name, 56, 41, 20, Weight::Black, col::text);
    const float nw = textWidth (name, 20, Weight::Black);
    paint::chevron (g, 70 + nw, 34, 9, col::quiet);
    nameR = { 20, 14, 70 + nw, 40 };
    if (d->type() == DeviceType::Poly)
        paint::pill (g, 86 + nw, 23, d->get ("engine") > 0.5f ? "VCO" : "DCO", 11, col::control, col::quiet, 22, 9);
    presetR = { 18, 62, 230, 30 };
    g.setColour (col::control); g.fillRoundedRectangle (presetR, 15);
    prevR = { 18, 62, 32, 30 }; nextR = { 216, 62, 32, 30 };
    paint::chevron (g, 34, 77, 8, col::quiet, 'l');
    paint::chevron (g, 232, 77, 8, col::quiet, 'r');
    g.setColour (col::text); g.setFont (font (12, Weight::Bold));
    g.drawText (d->presetName, juce::Rectangle<float> (50, 62, 166, 30), juce::Justification::centred, true);
    const auto& mixer = ctx.engine.project().patterns[(size_t) ctx.focusPattern()].slots[(size_t) track].mixer;
    paint::msButtons (g, 18, h - 44, mixer.mute, mixer.solo, 26);
    muteR = { 18, h - 44, 26, 26 }; soloR = { 50, h - 44, 26, 26 };
    // the selected instrument (click it, or Up / Down): what the keyboard and MIDI play
    if (ctx.focusTrack() == track)
    {
        g.setColour (colour); g.drawRoundedRectangle (0.75f, 0.75f, w - 1.5f, h - 1.5f, 12, 1.5f);
        g.fillRoundedRectangle (86, h - 44, 26, 26, 7);
        paint::keysIcon (g, 99, h - 31, 13, col::bg);
    }
    VisualView::drawRackVisual (g, visR, *d, colour);
    g.setColour (col::control); g.fillRoundedRectangle (openR, 12);
    paint::openIcon (g, openR.getCentreX(), openR.getCentreY(), 18, col::text);
    for (auto r : { nameR, prevR, nextR, presetR.reduced (32, 0), muteR, soloR }) fb.hot (r);
    fb.hot (openR, 12);
    fb.hot (visR, 10);   // Enter opens the editor, as a click does
    fb.paint (g);
}

void SlotStrip::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.position;
    auto* d = built;
    if (d == nullptr)
    {
        if ((plusR.contains (p) || nameR.contains (p)) && ctx.showDevicePicker) ctx.showDevicePicker (track, this);
        return;
    }
    auto& mixer = ctx.engine.project().patterns[(size_t) ctx.focusPattern()].slots[(size_t) track].mixer;
    if (nameR.contains (p) && ctx.showDevicePicker) ctx.showDevicePicker (track, this);
    else if (prevR.contains (p)) { presetActions::step (ctx, *d, -1); repaint(); }
    else if (nextR.contains (p)) { presetActions::step (ctx, *d, 1); repaint(); }
    else if (presetR.contains (p) && ctx.showPresetBrowser) { ctx.engine.focusTrack = track; ctx.showPresetBrowser (d, this); }
    else if (muteR.contains (p)) { mixer.mute = ! mixer.mute; ctx.markDirty(); repaint(); }
    else if (soloR.contains (p)) { mixer.solo = ! mixer.solo; ctx.markDirty(); repaint(); }
    else if (openR.contains (p) || visR.contains (p)) { if (ctx.openEditor) ctx.openEditor (track); }
}

void SlotStrip::select()
{
    if (ctx.focusTrack() == track || built == nullptr) return;
    ctx.engine.focusTrack = track;
    if (auto* parent = getParentComponent()) parent->repaint();
}

void SlotStrip::refresh()
{
    if (ctx.engine.device (ctx.focusPattern(), track) != built || ctx.focusPattern() != builtPattern) rebuild();
    for (auto& k : macros) k->refresh();
    for (auto& k : mixKnobs) k->refresh();
    if (fader) fader->refresh();
    juce::String s;
    if (built != nullptr)
    {
        s << built->presetName << (int) built->get ("engine");
        for (auto& w : built->heldNotes) s << (int) w.load();
        for (int i = 0; i < std::min (8, built->params().size()); ++i) s << built->get (i);
        if (built->type() == DeviceType::Acid || built->type() == DeviceType::Mono || built->type() == DeviceType::Poly)
            s << built->get ("cutoff") << built->get ("reso") << built->get ("chorusMode");
        if (built->type() == DeviceType::FM) s << built->get ("algorithm");
        const auto& mixer = ctx.engine.project().patterns[(size_t) ctx.focusPattern()].slots[(size_t) track].mixer;
        s << (int) mixer.mute << (int) mixer.solo << (int) (ctx.focusTrack() == track);
    }
    if (s != sig) { sig = s; repaint(); }
}

// ------------------------------------------------------------------------------------------- master row
MasterRow::MasterRow (AppContext& c) : ctx (c)
{
    auto& m = ctx.engine.project().master;
    auto add = [&] (float* v, float lo, float hi, float def, std::function<juce::String (float)> fmt, const juce::String& lab)
    {
        auto b = std::make_unique<FloatBinding> (v, lo, hi, def, fmt, false, &ctx);
        b->learnId = "master:" + lab.toLowerCase();
        auto kv = std::make_unique<KnobView> (std::move (b), lab, col::pattern, 22.0f, true);
        addAndMakeVisible (*kv);
        knobs.push_back (std::move (kv));
    };
    add (&m.delayReturn, 0, 1, 0.8f, pct, "Delay");
    add (&m.reverbReturn, 0, 1, 0.8f, pct, "Reverb");
    add (&m.glue, 0, 1, 0.3f, pct, "Glue");
    add (&m.limitDb, -12, 0, -0.3f, [] (float v) { return juce::String (v, 1) + " dB"; }, "Limit");
    auto vol = std::make_unique<FloatBinding> (&m.volume, 0, 1.2f, 0.8f, pct, false, &ctx);
    vol->learnId = "master:volume";
    volume = std::make_unique<FaderView> (std::move (vol), col::pattern);
    addAndMakeVisible (*volume);
}

void MasterRow::resized()
{
    // mockup 19_master_fx: Effects, the returns and bus, Delay / Reverb settings, the 8 performance pads, meter, volume
    const float h = (float) getHeight();
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (juce::Rectangle<float> (230 + i * 104 - 43, h / 2 - 8 - 28, 86, 104).toNearestInt());
    volume->setBounds (juce::Rectangle<float> ((float) getWidth() - 71, 10, 40, h - 20).toNearestInt());
    effectsR = { 16, h - 46, 150, 30 };
    delayR = { 590, 30, 62, 28 };
    reverbR = { 590, 70, 62, 28 };
    // a wider window spreads the pads and the meter (mockup 01_rack at 16:9)
    extra = std::max (0.0f, (float) getWidth() - 1552.0f);
    const float pw = 112 + extra * 0.5f / 4;
    for (int i = 0; i < kPerformFx; ++i) padR[(size_t) i] = { 664.0f + extra * 0.25f + (i % 4) * (pw + 8), 28.0f + (i / 4) * 46.0f, pw, 38 };
}

void MasterRow::paint (juce::Graphics& g)
{
    paint::card (g, getLocalBounds().toFloat(), "Master");
    fb.begin();
    // the master effects chain: a dot per slot in use (its category's colour)
    g.setColour (col::control); g.fillRoundedRectangle (effectsR, 15);
    paint::text (g, "Effects", effectsR.getX() + 14, effectsR.getY() + 20, 11, Weight::Bold, col::text);
    int dot = 0;
    for (const auto& fx : ctx.engine.project().masterFx)
        if (const auto* d = efx::defForId (efx::idForName (fx.effect)); d != nullptr && dot < 6)
        {
            g.setColour (fx.bypass ? col::control.brighter (0.3f) : efx::catAccent (d->cat));
            g.fillEllipse (effectsR.getX() + 72 + dot++ * 11.0f, effectsR.getCentreY() - 3.5f, 7, 7);
        }
    paint::chevron (g, effectsR.getRight() - 14, effectsR.getCentreY(), 7, col::quiet, 'r');
    fb.hot (effectsR, 15);
    for (auto [r, lab] : { std::pair<juce::Rectangle<float>, const char*> { delayR, "Delay" }, { reverbR, "Reverb" } })
    {
        g.setColour (col::control); g.fillRoundedRectangle (r, 14);
        paint::text (g, lab, r.getX() + 12, r.getY() + 18, 10, Weight::Bold, col::text);
        paint::chevron (g, r.getRight() - 10, r.getCentreY(), 6, col::quiet, 'd');
        fb.hot (r, 14);
    }
    // performance effects: on while a pad is held (mouse, keys 1–8, the controller's pads)
    paint::text (g, "PERFORM", padR[0].getX(), 18, 10, Weight::Black, col::quiet);
    // FX keys: while on, 8 notes from any controller hold the effects (click: on / off; right-click: the first note)
    {
        const auto* map = ctx.midiMap;
        const bool on = map != nullptr && map->fxKeysOn.load(), learning = map != nullptr && map->fxKeysLearning.load();
        const int base = map != nullptr ? map->fxKeysBase.load() : 36;
        const auto range = juce::MidiMessage::getMidiNoteName (base, true, true, 4) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"))
                         + juce::MidiMessage::getMidiNoteName (base + 7, true, true, 4);
        const auto label = learning ? juce::String ("Press a pad or key") : "FX keys  " + range;
        const float right = padR[3].getRight();
        const float pw = paint::pill (g, right, 6, label, 10, on || learning ? col::pattern : col::control, on || learning ? col::bg : col::quiet,
                                      18, 9, on ? Weight::Black : Weight::Bold, false, juce::Justification::right);
        fxKeysR = { right - pw, 6, pw, 18 };
        fb.hot (fxKeysR, 9);
    }
    for (int i = 0; i < kPerformFx; ++i)
    {
        const auto r = padR[(size_t) i];
        const bool on = ctx.masterFx != nullptr && ctx.masterFx->isHeld (i);
        g.setColour (on ? col::pattern : col::control); g.fillRoundedRectangle (r, 10);
        paint::text (g, MasterFx::padName (i), r.getX() + 12, r.getY() + 24, 12, Weight::Black, on ? col::bg : col::text);
        paint::text (g, juce::String (i + 1), r.getRight() - 12, r.getY() + 24, 10, Weight::Black, on ? col::bg : col::faint, juce::Justification::right);
        fb.hot (r, 10);
    }
    const float pl = ctx.engine.master().peakL.load(), pr = ctx.engine.master().peakR.load();
    const float lv[2] = { juce::jlimit (0.0f, 1.0f, (dsp::gainToDb (pl) + 48) / 48), juce::jlimit (0.0f, 1.0f, (dsp::gainToDb (pr) + 48) / 48) };
    const float h = (float) getHeight();
    const float mx0 = 1168 + extra * 0.75f, mw = 200 + extra * 0.25f;
    for (int i = 0; i < 2; ++i)
    {
        const float yy = h / 2 - 9 + i * 12;
        g.setColour (col::control); g.fillRoundedRectangle (mx0, yy, mw, 6, 3);
        g.setColour (col::pattern.withAlpha (0.75f)); g.fillRoundedRectangle (mx0, yy, mw * lv[i], 6, 3);
    }
    const float pk = dsp::gainToDb (std::max (pl, pr));
    paint::text (g, pk < -90.0f ? juce::String ("-inf") : juce::String (pk, 1), mx0, h / 2 + 28, 11, Weight::Bold, col::faint);
    paint::text (g, "CPU " + juce::String (juce::roundToInt (ctx.engine.cpuLoad.load() * 100)) + "%", mx0 + mw, h / 2 + 28, 11, Weight::Bold, col::faint, juce::Justification::right);
    fb.paint (g);
}

void MasterRow::mouseDown (const juce::MouseEvent& e)
{
    // a pad plays while the button is down
    for (int i = 0; i < kPerformFx; ++i)
        if (padR[(size_t) i].contains (e.position) && ctx.masterFx != nullptr && ! e.mods.isRightButtonDown())
        {
            pressedPad = i;
            ctx.masterFx->setHeld (i, true);
            repaint();
        }
}

void MasterRow::mouseUp (const juce::MouseEvent& e)
{
    if (pressedPad >= 0)
    {
        if (ctx.masterFx != nullptr) ctx.masterFx->setHeld (pressedPad, false);
        pressedPad = -1;
        repaint();
        return;
    }
    auto& m = ctx.engine.project().master;
    auto knob = [this] (float* v, float lo, float hi, float def, std::function<juce::String (float)> f, const juce::String& lab)
    {
        return std::make_unique<KnobView> (std::make_unique<FloatBinding> (v, lo, hi, def, f, false, &ctx), lab, col::pattern, 18.0f, true);
    };
    if (fxKeysR.contains (e.position) && ctx.midiMap != nullptr)
    {
        auto* map = ctx.midiMap;
        if (! e.mods.isRightButtonDown())
        {
            if (map->fxKeysLearning.exchange (false)) { repaint(); return; }
            if (ctx.setFxKeys) ctx.setFxKeys (! map->fxKeysOn.load());
            repaint();
            return;
        }
        juce::PopupMenu m;
        m.addSectionHeader ("FX keys: 8 notes from your controller hold the effects");
        m.addItem (1, "FX keys on", true, map->fxKeysOn.load());
        m.addItem (2, "Set the first note: press a pad or key...");
        m.addItem (3, "First note back to " + juce::MidiMessage::getMidiNoteName (36, true, true, 4), map->fxKeysBase.load() != 36);
        m.showMenuAsync (menuAt (*this, fxKeysR), [this, map] (int r)
        {
            if (r == 1 && ctx.setFxKeys) ctx.setFxKeys (! map->fxKeysOn.load());
            if (r == 2) map->fxKeysLearning = true;
            if (r == 3) { map->fxKeysBase = 36; map->changed(); }
            repaint();
        });
        return;
    }
    if (effectsR.contains (e.position)) { if (ctx.onMasterFx) ctx.onMasterFx (false, 0); }
    else if (e.mods.isRightButtonDown())
    {
        // a pad's right-click (Shift+F10 when focused): tune its effect
        for (int i = 0; i < kPerformFx; ++i) if (padR[(size_t) i].contains (e.position) && ctx.onMasterFx) ctx.onMasterFx (true, i);
    }
    else if (delayR.contains (e.position))
    {
        std::vector<std::unique_ptr<KnobView>> k;
        auto divBinding = std::make_unique<FloatBinding> (&divProxy, 0, (float) (dsp::kNumDivisions - 1), 9,
                                                          [] (float v) { return juce::String (dsp::divisions()[std::clamp (juce::roundToInt (v), 0, dsp::kNumDivisions - 1)].name); });
        divProxy = (float) m.delayDiv;
        divBinding->onChange = [this] (float v) { ctx.engine.project().master.delayDiv = juce::roundToInt (v); ctx.markDirty(); };
        k.push_back (std::make_unique<KnobView> (std::move (divBinding), "Time", col::pattern, 18.0f, true));
        k.push_back (knob (&m.delayFeedback, 0, 0.92f, 0.4f, pct, "Feedback"));
        k.push_back (knob (&m.delayTone, 0, 1, 0.5f, pct, "Tone"));
        showKnobPopover (this, delayR.toNearestInt(), std::move (k));
    }
    else if (reverbR.contains (e.position))
    {
        std::vector<std::unique_ptr<KnobView>> k;
        k.push_back (knob (&m.reverbDecay, 0, 1, 0.55f, pct, "Decay"));
        k.push_back (knob (&m.reverbDamp, 0, 1, 0.45f, pct, "Damp"));
        showKnobPopover (this, reverbR.toNearestInt(), std::move (k));
    }
}

void MasterRow::refresh()
{
    for (auto& k : knobs) k->refresh();
    volume->refresh();
    juce::String s;
    for (int i = 0; i < kPerformFx; ++i) s << (ctx.masterFx != nullptr && ctx.masterFx->isHeld (i) ? 1 : 0);
    for (const auto& fx : ctx.engine.project().masterFx) s << fx.effect << (int) fx.bypass;
    if (ctx.midiMap != nullptr) s << (int) ctx.midiMap->fxKeysOn.load() << (int) ctx.midiMap->fxKeysLearning.load() << ctx.midiMap->fxKeysBase.load();
    if (s != shown) { shown = s; repaint(); }
    repaint (juce::Rectangle<int> ((int) (1160 + extra * 0.75f), 0, (int) (230 + extra * 0.25f), getHeight()));
}

// ------------------------------------------------------------------------------------------- rack view
RackView::RackView (AppContext& c) : ctx (c), master (c)
{
    for (int t = 0; t < kTracks; ++t)
    {
        slots.push_back (std::make_unique<SlotStrip> (ctx, t));
        strips.addAndMakeVisible (*slots.back());
    }
    viewport.setViewedComponent (&strips, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    // the arrows keep selecting instruments (the scroll area never takes the keyboard; the wheel scrolls it)
    viewport.setWantsKeyboardFocus (false);
    strips.setWantsKeyboardFocus (false);
    addAndMakeVisible (viewport);
    addAndMakeVisible (master);
}

void RackView::resized()
{
    const int masterH = 124;
    const float y = 16.0f;
    const float viewH = (float) getHeight() - M - masterH - GAP - y;
    const float sh = (viewH - GAP * 3) / 4.0f;   // a strip is as tall as with four tracks; the rest scroll into view
    viewport.setBounds (juce::Rectangle<float> (0, y, (float) W, viewH).toNearestInt());
    strips.setSize (W - 8, juce::roundToInt (kTracks * (sh + GAP) - GAP));
    for (int i = 0; i < kTracks; ++i)
        slots[(size_t) i]->setBounds (juce::Rectangle<float> (M, i * (sh + GAP), W - 2 * M - 8, sh).toNearestInt());
    master.setBounds (M, getHeight() - M - masterH, W - 2 * M, masterH);
}

void RackView::refresh()
{
    // the selected instrument scrolls into view (Up / Down, the editor's track dots, MIDI)
    if (ctx.focusTrack() != shownFocus)
    {
        shownFocus = ctx.focusTrack();
        const auto b = slots[(size_t) shownFocus]->getBounds();
        const int top = viewport.getViewPositionY(), h = viewport.getHeight();
        if (b.getY() < top) viewport.setViewPosition (0, b.getY());
        else if (b.getBottom() > top + h) viewport.setViewPosition (0, b.getBottom() - h);
    }
    for (auto& s : slots) s->refresh();
    master.refresh();
}
} // namespace mb::ui
