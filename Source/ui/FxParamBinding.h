#pragma once
#include "Widgets.h"
#include "efx/EffectRegistry.h"

namespace mb::ui
{
/** A control of an effect (param -1: its mix): a master chain slot, a performance pad, or a track's slot. Its 0–1
    position in the project, looked up on every use, so a loaded project or a replaced effect never leaves a
    dangling reference. */
struct FxParamBinding : Binding
{
    FxParamBinding (AppContext& c, bool p, int s, int k) : ctx (c), pads (p), slot (s), param (k) {}
    /** A track's effect slot. */
    FxParamBinding (AppContext& c, int pat, int tr, int s, int k) : ctx (c), pads (false), slot (s), param (k), pattern (pat), track (tr) {}
    FxSlot& fx() const
    {
        auto& pr = ctx.engine.project();
        if (track >= 0) return pr.patterns[(size_t) pattern].slots[(size_t) track].fx[(size_t) slot];
        return pads ? pr.performFx[(size_t) slot] : pr.masterFx[(size_t) slot];
    }
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
    AppContext& ctx; bool pads; int slot, param, pattern = -1, track = -1;
};
} // namespace mb::ui
