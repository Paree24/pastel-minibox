#include "MasterFx.h"

namespace mb
{
namespace
{
struct Pad { const char* name; const char* effect; };
// the eight performance effects, in the pads' order (and the controller's pads 1–8)
const Pad kPads[kPerformFx] = { { "Stutter", "Stutter" }, { "Glitch", "Glitch" }, { "Shuffle", "Buffer Shuffler" }, { "Slicer", "Slicer" },
                                { "Crush", "Bitcrusher" }, { "Filter", "Multimode Filter" }, { "Drive", "Soft Clip Drive" }, { "Reverse", "Reverse" } };

int paramIndex (const efx::EffectDef& d, const char* name)
{
    for (int k = 0; k < d.numParams; ++k) if (juce::String (d.p[k].name) == name) return k;
    return -1;
}

void setPhysical (FxSlot& fx, const efx::EffectDef& d, const char* name, float value)
{
    if (const int k = paramIndex (d, name); k >= 0) fx.norm[(size_t) k] = efx::mapParam (d.p[k], value);
}
} // namespace

MasterFx::MasterFx (Engine& e) : engine (e) {}

MasterFx::~MasterFx()
{
    if (engine.masterInsertIs (this)) engine.setMasterInsert (nullptr);
}

const char* MasterFx::padName (int pad) { return kPads[(size_t) std::clamp (pad, 0, kPerformFx - 1)].name; }

void MasterFx::setEffect (FxSlot& fx, const juce::String& name)
{
    fx = FxSlot();
    const auto* d = efx::defForId (efx::idForName (name));
    if (d == nullptr) return;
    fx.effect = d->name;
    for (int k = 0; k < d->numParams; ++k) fx.norm[(size_t) k] = d->p[k].def;
    fx.mix = d->mixDef;
}

void MasterFx::defaultPad (FxSlot& fx, int pad)
{
    setEffect (fx, kPads[(size_t) std::clamp (pad, 0, kPerformFx - 1)].effect);
    const auto* d = efx::defForId (efx::idForName (fx.effect));
    if (d == nullptr) return;
    // held = the effect's macro turned all the way (Stutter: Chop, Bitcrusher: Crush, Filter: Sweep …), fully wet
    for (int k = 0; k < d->numParams; ++k)
        if (! d->p[k].isChoice()) fx.norm[(size_t) k] = juce::jlimit (0.0f, 1.0f, d->p[k].def + d->macroDepth[k]);
    fx.mix = 1.0f;
    const juce::String e (d->name);
    if (e == "Stutter") setPhysical (fx, *d, "Hold", 1.0f);          // repeats the slice caught at the press for as long as it is held
    if (e == "Soft Clip Drive") setPhysical (fx, *d, "Out", -6.0f);  // as loud as the dry mix, not louder
    if (e == "Glitch") fx.mode = 1;                                  // Wild
}

const std::vector<int>& MasterFx::choosableEffects()
{
    static const std::vector<int> ids = []
    {
        std::vector<int> v;
        for (int id = 1; id <= efx::kNumEffects; ++id)
        {
            const auto* d = efx::defForId (id);
            if (d->cat == efx::CatAnalysis) continue;
            auto probe = efx::makeFxImpl (id);
            if (probe != nullptr && probe->usesData()) continue;   // drawn curves, steps or bands: edited graphically in EFX Grid
            v.push_back (id);
        }
        return v;
    }();
    return ids;
}

void MasterFx::prepare (double sr, int block)
{
    sampleRate = sr;
    maxBlock = block;
    for (auto* arr : { &slots, &pads })
        for (auto& in : *arr) if (in.fx) in.fx->prepare (sr, block);
    for (auto& pt : tracks) for (auto& tr : pt) for (auto& in : tr) if (in.fx) in.fx->prepare (sr, block);
}

void MasterFx::setTrackEffect (int pattern, int track, int slot, const juce::String& effect)
{
    if (pattern < 0 || pattern >= kPatterns || track < 0 || track >= kTracks || slot < 0 || slot >= kTrackFx) return;
    FxSlot fx;
    setEffect (fx, effect);
    auto fresh = build (fx.effect);
    auto& in = tracks[(size_t) pattern][(size_t) track][(size_t) slot];
    {
        const Engine::ScopedEdit edit (engine);
        engine.project().patterns[(size_t) pattern].slots[(size_t) track].fx[(size_t) slot] = fx;
        std::swap (in.fx, fresh);
        in.name = fx.effect;
    }
    // the replaced instance is freed here, outside the lock
}

bool MasterFx::trackActive (int pattern, int track) const
{
    const auto& fx = engine.project().patterns[(size_t) pattern].slots[(size_t) track].fx;
    const auto& in = tracks[(size_t) pattern][(size_t) track];
    for (int i = 0; i < kTrackFx; ++i)
        if (in[(size_t) i].fx && ! fx[(size_t) i].bypass && in[(size_t) i].name == fx[(size_t) i].effect) return true;
    return false;
}

void MasterFx::processTrack (int pattern, int track, float* L, float* R, int n, double bpm, double ppq, bool playing)
{
    const auto& fx = engine.project().patterns[(size_t) pattern].slots[(size_t) track].fx;
    auto& in = tracks[(size_t) pattern][(size_t) track];
    for (int i = 0; i < kTrackFx; ++i)
        if (in[(size_t) i].name == fx[(size_t) i].effect)   // until sync() builds a changed slot, it stays dry
            run (in[(size_t) i], fx[(size_t) i], L, R, n, bpm, ppq, playing, fx[(size_t) i].bypass);
}

std::unique_ptr<efx::GridEffect> MasterFx::build (const juce::String& effect) const
{
    std::unique_ptr<efx::GridEffect> fresh;
    if (const int id = efx::idForName (effect); id > 0)
    {
        fresh = efx::createEffect (id);
        fresh->prepare (sampleRate, maxBlock);
    }
    return fresh;
}

void MasterFx::setSlotEffect (bool pad, int i, const juce::String& effect)
{
    if (i < 0 || i >= (pad ? kPerformFx : kMasterFxSlots)) return;
    FxSlot fx;
    if (pad) defaultPad (fx, i); else setEffect (fx, effect);
    if (pad && effect.isNotEmpty()) { const auto keep = fx; setEffect (fx, effect); fx.mix = keep.mix; }
    auto fresh = build (fx.effect);
    auto& in = (pad ? pads : slots)[(size_t) i];
    {
        const Engine::ScopedEdit edit (engine);
        (pad ? engine.project().performFx : engine.project().masterFx)[(size_t) i] = fx;
        std::swap (in.fx, fresh);
        in.name = fx.effect;
    }
}

void MasterFx::removeSlot (int i)
{
    if (i < 0 || i >= kMasterFxSlots) return;
    std::unique_ptr<efx::GridEffect> gone;
    const Engine::ScopedEdit edit (engine);
    auto& chain = engine.project().masterFx;
    gone = std::move (slots[(size_t) i].fx);
    for (int k = i; k < kMasterFxSlots - 1; ++k)
    {
        chain[(size_t) k] = chain[(size_t) k + 1];
        slots[(size_t) k].fx = std::move (slots[(size_t) k + 1].fx);
        slots[(size_t) k].name = slots[(size_t) k + 1].name;
    }
    chain[(size_t) kMasterFxSlots - 1] = FxSlot();
    slots[(size_t) kMasterFxSlots - 1].name = {};
}

void MasterFx::moveSlot (int from, int to)
{
    if (from < 0 || to < 0 || from >= kMasterFxSlots || to >= kMasterFxSlots || from == to) return;
    const Engine::ScopedEdit edit (engine);
    std::swap (engine.project().masterFx[(size_t) from], engine.project().masterFx[(size_t) to]);
    std::swap (slots[(size_t) from].fx, slots[(size_t) to].fx);
    std::swap (slots[(size_t) from].name, slots[(size_t) to].name);
}

void MasterFx::syncOne (Inst& in, const juce::String& want)
{
    if (in.name == want) return;
    auto fresh = build (want);
    {
        const Engine::ScopedEdit edit (engine);
        std::swap (in.fx, fresh);
        in.name = in.fx ? want : juce::String();
    }
    // the replaced instance is freed here, outside the lock
}

void MasterFx::sync()
{
    auto& p = engine.project();
    for (int i = 0; i < kPerformFx; ++i)
        if (p.performFx[(size_t) i].effect.isEmpty()) defaultPad (p.performFx[(size_t) i], i);
    for (int i = 0; i < kMasterFxSlots; ++i) syncOne (slots[(size_t) i], p.masterFx[(size_t) i].effect);
    for (int i = 0; i < kPerformFx; ++i) syncOne (pads[(size_t) i], p.performFx[(size_t) i].effect);
    for (int pt = 0; pt < kPatterns; ++pt)
        for (int t = 0; t < kTracks; ++t)
            for (int i = 0; i < kTrackFx; ++i)
                syncOne (tracks[(size_t) pt][(size_t) t][(size_t) i], p.patterns[(size_t) pt].slots[(size_t) t].fx[(size_t) i].effect);
    if (syncedGen != engine.projectGeneration())
    {
        const Engine::ScopedEdit edit (engine);
        syncedGen = engine.projectGeneration();
    }
}

void MasterFx::setHeld (int pad, bool h)
{
    if (pad >= 0 && pad < kPerformFx) held[(size_t) pad] = h;
}

const efx::GridEffect* MasterFx::instance (bool perform, int i) const
{
    const auto& arr = perform ? pads : slots;
    return i >= 0 && i < (int) arr.size() ? arr[(size_t) i].fx.get() : nullptr;
}

void MasterFx::run (Inst& in, const FxSlot& fx, float* L, float* R, int n, double bpm, double ppq, bool playing, bool bypass)
{
    if (! in.fx) return;
    efx::SlotInput si;
    si.norm = fx.norm.data();
    si.mix = fx.mix;
    si.mode = fx.mode;
    si.bypass = bypass;
    si.bpm = bpm; si.ppq = ppq; si.playing = playing;
    in.fx->process (L, R, n, si);
}

void MasterFx::process (float* L, float* R, int n, double bpm, double ppq, bool playing)
{
    const auto& p = engine.project();
    if (engine.projectGeneration() != syncedGen) return;   // a project just loaded: its effects are built on the message thread first
    for (int i = 0; i < kPerformFx; ++i)
    {
        const bool h = held[(size_t) i].load();
        // a press starts the effect fresh: Stutter catches the slice under the press, Reverse the bar now …
        if (h && ! wasHeld[(size_t) i] && pads[(size_t) i].fx) pads[(size_t) i].fx->restart();
        wasHeld[(size_t) i] = h;
        run (pads[(size_t) i], p.performFx[(size_t) i], L, R, n, bpm, ppq, playing, ! h);
    }
    for (int i = 0; i < kMasterFxSlots; ++i)
        run (slots[(size_t) i], p.masterFx[(size_t) i], L, R, n, bpm, ppq, playing, p.masterFx[(size_t) i].bypass);
}
} // namespace mb
