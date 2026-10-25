#include "MasterFxDrawer.h"
#include "efx/MasterFx.h"

namespace mb::ui
{
namespace
{
using R = juce::Rectangle<float>;
constexpr float kTileY = 44, kTileH = 58, kKnobY = 208;

/** A control of the selected slot's effect: its 0–1 position in the project (looked up on every use, so a
    loaded project or a replaced effect never leaves a dangling reference). */
struct FxParamBinding : Binding
{
    FxParamBinding (AppContext& c, bool p, int s, int k) : ctx (c), pads (p), slot (s), param (k) {}
    FxSlot& fx() const { auto& pr = ctx.engine.project(); return pads ? pr.performFx[(size_t) slot] : pr.masterFx[(size_t) slot]; }
    const efx::ParamSpec* spec() const
    {
        const auto* d = efx::defForId (efx::idForName (fx().effect));
        return d != nullptr && param < d->numParams ? &d->p[param] : nullptr;
    }
    float getNorm() const override { return param < 0 ? fx().mix : fx().norm[(size_t) param]; }
    void setNorm (float v) override
    {
        v = juce::jlimit (0.0f, 1.0f, v);
        if (param < 0) fx().mix = v; else fx().norm[(size_t) param] = v;
        ctx.markDirty();
    }
    float defaultNorm() const override { if (param < 0) return 1.0f; auto* s = spec(); return s != nullptr ? s->def : 0.5f; }
    juce::String text() const override
    {
        if (param < 0) return juce::String (juce::roundToInt (fx().mix * 100)) + "%";
        auto* s = spec();
        return s != nullptr ? efx::formatParam (*s, getNorm()) : juce::String();
    }
    bool bipolar() const override
    {
        auto* s = spec();
        if (s == nullptr) return false;
        using K = efx::K;
        return s->kind == K::BiPct || s->kind == K::Pan || s->kind == K::Semis || s->kind == K::Cents || s->kind == K::BiHz
            || (s->kind == K::Db && s->lo < 0.0f && s->hi > 0.0f && std::abs (s->lo + s->hi) < 1.0e-3f);
    }
    int steps() const override { auto* s = spec(); return s != nullptr && s->isChoice() ? std::max (2, s->numChoices) : 0; }
    AppContext& ctx; bool pads; int slot, param;
};
} // namespace

MasterFxDrawer::MasterFxDrawer (AppContext& c) : ctx (c)
{
    // keys on the focused tile: Delete removes it (the chain), ← / → (with Alt) move it
    fb.keyHandler = [this] (int hot, const juce::KeyPress& k)
    {
        if (hot < 0 || hot >= (int) zones.size() || zones[(size_t) hot].id != "tile") return false;
        sel = zones[(size_t) hot].a;
        if (! padsView && (k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey)) { remove(); return true; }
        if (! padsView && k.getModifiers().isAltDown() && (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey))
        {
            move (k.getKeyCode() == juce::KeyPress::leftKey ? -1 : 1);
            return true;
        }
        return false;
    };
}

FxSlot& MasterFxDrawer::slot (int i) const
{
    auto& p = ctx.engine.project();
    return padsView ? p.performFx[(size_t) juce::jlimit (0, kPerformFx - 1, i)] : p.masterFx[(size_t) juce::jlimit (0, kMasterFxSlots - 1, i)];
}

void MasterFxDrawer::show (bool pads, int index)
{
    padsView = pads;
    sel = juce::jlimit (0, 7, index);
    rebuildKnobs();
    repaint();
}

void MasterFxDrawer::rebuildKnobs()
{
    const auto& fx = slot (sel);
    juce::String key; key << (int) padsView << sel << fx.effect << fx.mode;
    if (key == builtFor) return;
    builtFor = key;
    knobs.clear();
    mixKnob.reset();
    const auto* d = efx::defForId (efx::idForName (fx.effect));
    if (d == nullptr) return;
    const auto colour = efx::catAccent (d->cat);
    for (int k = 0; k < d->numParams; ++k)
    {
        // controls that do nothing in this mode (or with this choice) are left out, as in EFX Grid
        if (! efx::paramVisible (*d, k, fx.mode, fx.norm.data())) continue;
        auto kv = std::make_unique<KnobView> (std::make_unique<FxParamBinding> (ctx, padsView, sel, k), d->p[k].name, colour, 20.0f, true);
        addAndMakeVisible (*kv);
        knobs.push_back (std::move (kv));
    }
    mixKnob = std::make_unique<KnobView> (std::make_unique<FxParamBinding> (ctx, padsView, sel, -1), "Mix", col::text, 20.0f, true);
    addAndMakeVisible (*mixKnob);
    resized();
}

void MasterFxDrawer::resized()
{
    const float w = (float) getWidth();
    const float spacing = knobs.empty() ? 120.0f : std::min (120.0f, (w - 200.0f) / (float) knobs.size());
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (R (60 + i * spacing - 43, kKnobY - 26, 86, 80).toNearestInt());
    if (mixKnob) mixKnob->setBounds (R (w - 70 - 43, kKnobY - 26, 86, 80).toNearestInt());
}

void MasterFxDrawer::paint (juce::Graphics& g)
{
    rebuildKnobs();
    zones.clear();
    fb.begin();
    auto zone = [this] (R r, const juce::String& id, int a = 0, float radius = -1.0f) { zones.push_back ({ r, id, a }); fb.hot (r, radius); };
    const float w = (float) getWidth(), h = (float) getHeight();
    g.setColour (col::surface); g.fillRoundedRectangle (0, 0, w, h, 14);
    g.setColour (col::divider); g.drawRoundedRectangle (0.75f, 0.75f, w - 1.5f, h - 1.5f, 14, 1.5f);
    paint::text (g, "Master effects", 18, 28, 13, Weight::Black, col::text);
    const auto views = paint::segmented (g, { 140, 10, 150, 26 }, { "Chain", "Pads" }, padsView ? 1 : 0, col::text, 11);
    for (size_t i = 0; i < views.size(); ++i) zone (views[i].reduced (2), "view", (int) i);
    paint::text (g, padsView ? "each pad's effect while it is held (pads, keys 1-8, the controller's pads)"
                             : "in series, left to right, before the bus's glue and limiter", 306, 28, 11, Weight::Bold, col::faint);
    g.setColour (col::control); g.fillEllipse (w - 40, 9, 28, 28);
    g.setColour (col::quiet); g.drawLine (w - 31, 18, w - 21, 28, 1.8f); g.drawLine (w - 21, 18, w - 31, 28, 1.8f);
    zone ({ w - 40, 9, 28, 28 }, "close", 0, 14);

    // the tiles: the chain's slots, or the pads
    const float tw = (w - 28 - 7 * 8) / 8;
    for (int i = 0; i < 8; ++i)
    {
        const R t (14 + i * (tw + 8), kTileY, tw, kTileH);
        const auto& fx = slot (i);
        const auto* d = efx::defForId (efx::idForName (fx.effect));
        if (d == nullptr)
        {
            juce::Path p; p.addRoundedRectangle (t.reduced (0.75f), 10.0f);
            const float dash[] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (p, p, dash, 2);
            g.setColour (col::divider); g.fillPath (p);
            paint::text (g, "+ Add effect", t.getCentreX(), t.getY() + 34, 11, Weight::Bold, col::faint, juce::Justification::horizontallyCentred);
            zone (t, "add", i, 10);
            continue;
        }
        const auto accent = efx::catAccent (d->cat);
        const bool held = padsView && ctx.masterFx != nullptr && ctx.masterFx->isHeld (i);
        g.setColour (held ? accent.withAlpha (0.25f) : juce::Colour (0xff1B1D22)); g.fillRoundedRectangle (t, 10);
        if (i == sel) { g.setColour (col::text); g.drawRoundedRectangle (t.reduced (0.75f), 10, 1.5f); }
        g.setColour (accent); g.fillRoundedRectangle (t.withWidth (5), 2.5f);
        auto name = juce::String (padsView ? MasterFx::padName (i) : d->name);
        paint::text (g, name, t.getX() + 16, t.getY() + 24, 12, Weight::Black, col::text);
        paint::text (g, padsView ? juce::String (d->name) + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + juce::String (i + 1) : juce::String (efx::catName (d->cat)),
                     t.getX() + 16, t.getY() + 44, 10, Weight::Bold, accent);
        zone (t, "tile", i, 10);
        if (! padsView)
        {
            // the power dot: bypass
            const R p (t.getRight() - 26, t.getY() + 10, 16, 16);
            g.setColour (fx.bypass ? col::control : accent); g.fillEllipse (p);
            zone (p.expanded (4), "power", i, 12);
        }
    }

    // the selected one
    const auto& fx = slot (sel);
    const auto* d = efx::defForId (efx::idForName (fx.effect));
    const float by = 116;
    if (d == nullptr)
    {
        paint::text (g, padsView ? "" : "Empty slot: add an effect (the chain fills from the left)", 18, by + 22, 12, Weight::Bold, col::faint);
        fb.paint (g);
        return;
    }
    const auto accent = efx::catAccent (d->cat);
    const juce::String padName (padsView ? MasterFx::padName (sel) : "");
    const juce::String title = padsView && padName != d->name ? padName + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + d->name : juce::String (d->name);
    paint::text (g, title, 18, by + 22, 14, Weight::Black, col::text);
    if (d->numModes > 1)
    {
        juce::StringArray modes;
        for (int m = 0; m < d->numModes; ++m) modes.add (d->modes[m]);
        const float mw = std::min (60.0f * d->numModes, 300.0f);
        const auto segs = paint::segmented (g, { 30 + textWidth (title, 14, Weight::Black), by + 4, mw, 26 }, modes, fx.mode, accent, 11);
        for (size_t i = 0; i < segs.size(); ++i) zone (segs[i].reduced (2), "mode", (int) i);
    }
    float bx = w - 14;
    // right to left: Remove, Replace, → , ← (the chain) or Reset (a pad)
    const auto arrow = [] (const char* utf8) { return juce::String (juce::CharPointer_UTF8 (utf8)); };
    const std::vector<std::pair<juce::String, juce::String>> buttons = padsView
        ? std::vector<std::pair<juce::String, juce::String>> { { "Reset", "Reset" } }
        : std::vector<std::pair<juce::String, juce::String>> { { "Remove", "Remove" }, { "Replace", "Replace" }, { arrow ("\xe2\x86\x92"), "right" }, { arrow ("\xe2\x86\x90"), "left" } };
    for (auto& [label, id] : buttons)
    {
        const float bw = paint::pill (g, bx, by + 4, label, 11, col::control, col::text, 26, 12, Weight::Bold, false, juce::Justification::right);
        zone ({ bx - bw, by + 4, bw, 26 }, id, 0, 13);
        bx -= bw + 6;
    }
    fb.paint (g);
}

void MasterFxDrawer::mouseUp (const juce::MouseEvent& e)
{
    menuArea = fb.hotAt (e.position);
    const Zone* z = nullptr;
    for (auto it = zones.rbegin(); it != zones.rend(); ++it) if (it->r.contains (e.position)) { z = &*it; break; }
    if (z == nullptr) return;
    const auto id = z->id;
    if (id == "close") { if (onClose) onClose(); return; }
    if (id == "view") { show (z->a == 1, sel); return; }
    if (id == "tile") { sel = z->a; rebuildKnobs(); repaint(); return; }
    if (id == "power") { slot (z->a).bypass = ! slot (z->a).bypass; changed(); return; }
    if (id == "add") { addEffect (z->a, false); return; }
    if (id == "mode") { slot (sel).mode = z->a; changed(); return; }
    if (id == "Replace") { addEffect (sel, true); return; }
    if (id == "Remove") { remove(); return; }
    if (id == "left" || id == "right") { move (id == "left" ? -1 : 1); return; }
    if (id == "Reset") { if (ctx.masterFx != nullptr) ctx.masterFx->setSlotEffect (true, sel, {}); changed(); return; }
}

void MasterFxDrawer::addEffect (int index, bool replace)
{
    // EFX Grid's effects by category; a new one goes to the first free slot (the chain stays packed)
    juce::PopupMenu m;
    std::array<juce::PopupMenu, efx::NumCats> cats;
    for (int id : MasterFx::choosableEffects())
    {
        const auto* d = efx::defForId (id);
        cats[(size_t) d->cat].addItem (id, d->name, true, replace && slot (index).effect == d->name);
    }
    for (int c = 0; c < efx::NumCats; ++c)
        if (cats[(size_t) c].getNumItems() > 0) m.addSubMenu (efx::catName (c), cats[(size_t) c]);
    m.showMenuAsync (menuAt (*this, menuArea), [this, index, replace] (int id)
    {
        const auto* d = efx::defForId (id);
        if (d == nullptr) return;
        int at = index;
        if (! replace)
            for (int i = 0; i < kMasterFxSlots; ++i)
                if (slot (i).effect.isEmpty()) { at = i; break; }
        if (ctx.masterFx != nullptr) ctx.masterFx->setSlotEffect (padsView, at, d->name);
        sel = at;
        changed();
    });
}

void MasterFxDrawer::remove()
{
    if (padsView || ctx.masterFx == nullptr) return;
    // the later effects shift left: no gaps in the chain
    ctx.masterFx->removeSlot (sel);
    while (sel > 0 && slot (sel).effect.isEmpty()) --sel;
    changed();
}

void MasterFxDrawer::move (int dir)
{
    if (padsView || ctx.masterFx == nullptr) return;
    const int to = sel + dir;
    if (to < 0 || to >= kMasterFxSlots || slot (to).effect.isEmpty() || slot (sel).effect.isEmpty()) return;
    ctx.masterFx->moveSlot (sel, to);
    sel = to;
    changed();
}

void MasterFxDrawer::changed()
{
    if (ctx.masterFx != nullptr) ctx.masterFx->sync();
    ctx.markDirty();
    builtFor.clear();
    rebuildKnobs();
    repaint();
}

void MasterFxDrawer::refresh()
{
    juce::String s;
    s << (int) padsView << sel;
    for (int i = 0; i < 8; ++i) { const auto& fx = slot (i); s << fx.effect << (int) fx.bypass << fx.mode << (ctx.masterFx != nullptr && ctx.masterFx->isHeld (i) ? 1 : 0); }
    if (s != sig) { sig = s; builtFor.clear(); rebuildKnobs(); repaint(); }
    for (auto& k : knobs) k->refresh();
    if (mixKnob) mixKnob->refresh();
}
} // namespace mb::ui
