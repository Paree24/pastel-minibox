#include "Tests.h"
#include "app/presets/PresetBank.h"
#include "devices/sampler/SamplerDevice.h"
#include "Fingerprint.h"
#include <map>

using namespace mb;
using namespace mb::fingerprint;

namespace
{
std::vector<std::pair<juce::String, Fingerprint>>& fingerprints (DeviceType t)
{
    static std::map<int, std::vector<std::pair<juce::String, Fingerprint>>> cache;
    auto& v = cache[(int) t];
    if (v.empty())
        for (auto& p : presets::list (t))
        {
            if (! p.factory) continue;
            auto d = createDevice (t);
            presets::apply (*d, p, true);
            v.push_back ({ p.name, render (*d, isDrumDevice (t)) });
        }
    return v;
}
} // namespace

MB_TEST (presets_counts)
{
    for (int t = 0; t < (int) DeviceType::Count; ++t)
    {
        int n = 0;
        for (auto& p : presets::list ((DeviceType) t)) n += p.factory ? 1 : 0;
        const int want = isDrumDevice ((DeviceType) t) ? 32 : 128;
        ctx.check (n == want, deviceTypeName ((DeviceType) t) + " has " + juce::String (n) + " factory presets");
    }
}

MB_TEST (presets_rules)
{
    // no detune, tune, transpose, octave variants, glide, pitch LFO or pitch EG
    const char* forbidden[] = { "tune", "fine", "detune", "vaFine", "transpose", "glide", "glideTime", "glideMode",
                                "lfoPitch", "drift", "stackDetune", "pegInt", "range", "vaOctave", "pegL1", "pegL2", "pegL3" };
    for (int t = 0; t < (int) DeviceType::Count; ++t)
    {
        auto ref = createDevice ((DeviceType) t);
        for (auto& p : presets::list ((DeviceType) t))
        {
            auto d = createDevice ((DeviceType) t);
            presets::apply (*d, p, false);
            for (auto* id : forbidden)
            {
                const int i = d->params().indexOf (id);
                if (i >= 0 && std::abs (d->get (i) - ref->get (i)) > 1.0e-6f)
                    ctx.check (false, deviceTypeName ((DeviceType) t) + " '" + p.name + "' changes " + id);
            }
            for (int op = 1; op <= 6 && t == (int) DeviceType::FM; ++op)
                if (std::abs (d->get ("op" + juce::String (op) + "Detune")) > 1.0e-6f)
                    ctx.check (false, "FM '" + p.name + "' detunes OP" + juce::String (op));
        }
    }
    ctx.check (true, "rules scanned");
}

MB_TEST (presets_defaults_are_plain)
{
    // a device's defaults (what a reset gives) are dry and still: no FX, no LFO depth, no drive
    const char* fx[] = { "fxChorus", "fxDelay", "fxReverb", "delayMix", "reverb", "lfoCut", "lfoPwm",
                         "lfoAmp", "driveAmt", "drive", "kitDrive", "resMix", "arpMode", "envAmt", "envMod" };
    for (int t = 0; t < (int) DeviceType::Count; ++t)
    {
        auto d = createDevice ((DeviceType) t);
        presets::loadDefault (*d, true);
        ctx.check (presets::findFactory (*d) != nullptr && presets::findFactory (*d)->index == 1,
                   deviceTypeName ((DeviceType) t) + " starts on factory 001");
        d->resetToDefaults();
        for (auto* id : fx)
        {
            const int i = d->params().indexOf (id);
            if (i >= 0) ctx.check (std::abs (d->get (i)) < 1.0e-6f, deviceTypeName ((DeviceType) t) + " default has " + id + " = " + juce::String (d->get (i)));
        }
    }
}

MB_TEST (presets_files_management)
{
    // factory numbering, shadowing by OVER, restore by DEL, user save/delete (in a scratch user folder)
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("minibox_preset_test");
    tmp.deleteRecursively();
    presets::setUserRootForTest (tmp);
    const auto t = DeviceType::Acid;
    const auto& f = presets::factory (t);
    ctx.check (f.size() == 128 && f.front().index == 1 && f.back().index == 128, "factory numbered 001..128");
    ctx.check (presets::families (t).size() == 8, "8 acid banks");
    auto d = createDevice (t);
    presets::apply (*d, f[4], true);
    const auto name = f[4].name;
    d->set ("cutoff", 3333.0f);
    ctx.check (presets::overwriteFactory (*d, presets::factory (t)[4]), "OVER writes");
    const auto shadow = presets::factory (t)[4];
    ctx.check (shadow.shadowed && shadow.name == name, "overwrite shadows the shipped preset");
    auto d2 = createDevice (t);
    presets::apply (*d2, shadow, true);
    ctx.check (std::abs (d2->get ("cutoff") - 3333.0f) < 1.0f, "shadow loads the user's version");
    ctx.check (presets::remove (shadow) && ! presets::factory (t)[4].shadowed, "DEL restores the shipped file");
    ctx.check (! presets::remove (presets::factory (t)[4]), "shipped presets can't be deleted");
    ctx.check (presets::save (*d, "My Line") && presets::userPresetExists (t, "My Line"), "user save");
    auto u = presets::user (t);
    ctx.check (u.size() == 1 && u[0].name == "My Line" && ! u[0].factory, "user list");
    ctx.check (presets::list (t).size() == 129, "list = factory + user");
    ctx.check (presets::remove (u[0]) && presets::user (t).empty(), "user delete");
    presets::setUserRootForTest ({});
    tmp.deleteRecursively();
}

MB_TEST (presets_render_all)
{
    for (int t = 0; t < (int) DeviceType::Count; ++t)
        for (auto& [name, fp] : fingerprints ((DeviceType) t))
        {
            ctx.check (fp.finite, deviceTypeName ((DeviceType) t) + " '" + name + "' finite");
            ctx.check (fp.peak < 1.6f, deviceTypeName ((DeviceType) t) + " '" + name + "' peak " + juce::String (fp.peak, 2));
            const float level = std::max (fp.loudDb, dsp::gainToDb (fp.peak) - 12.0f);   // short sounds count by peak
            ctx.check (level > -24.0f, deviceTypeName ((DeviceType) t) + " '" + name + "' audible, "
                       + juce::String (level, 1) + " dB");
        }
}

MB_TEST (presets_distinct_audio)
{
    for (int t = 0; t < (int) DeviceType::Count; ++t)
    {
        auto& fps = fingerprints ((DeviceType) t);
        float worst = 1.0e9f; juce::String pair;
        int close = 0;
        for (size_t i = 0; i < fps.size(); ++i)
            for (size_t j = i + 1; j < fps.size(); ++j)
            {
                const float d = fpDistance (fps[i].second, fps[j].second);
                if (d < worst) { worst = d; pair = fps[i].first + " / " + fps[j].first; }
                if (d < kMinDistance)
                {
                    ++close;
                    if (close <= 40) std::printf ("    close: %s  %s / %s  %.3f\n", deviceTypeName ((DeviceType) t).toRawUTF8(),
                                                  fps[i].first.toRawUTF8(), fps[j].first.toRawUTF8(), d);
                }
            }
        std::printf ("    %-13s closest %.3f (%s)\n", deviceTypeName ((DeviceType) t).toRawUTF8(), worst, pair.toRawUTF8());
        ctx.check (close == 0, deviceTypeName ((DeviceType) t) + ": " + juce::String (close) + " pairs sound too alike");
    }
}

/** Not a check: sets each factory preset's level for a common loudness (−18 dB short-term, short
    sounds by their peak) with the peak kept under 0.7, writing the preset files in place.
    MB_BALANCE=mono,poly limits it to those devices. */
MB_TEST (tool_balance_presets)
{
    const auto only = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("MB_BALANCE", ""), ",", "");
    int changed = 0;
    for (int t = 0; t < (int) DeviceType::Count; ++t)
    {
        const auto type = (DeviceType) t;
        if (! only.isEmpty() && ! only.contains (deviceTypeId (type))) continue;
        for (auto& p : presets::factory (type))
        {
            auto d = createDevice (type);
            presets::apply (*d, p, true);
            const juce::String key = isDrumDevice (type) ? "kitLevel" : "level";
            const int li = d->params().indexOf (key);
            const float lvl = d->get (li);
            const auto fp = render (*d, isDrumDevice (type));
            const float level = std::max (fp.loudDb, dsp::gainToDb (fp.peak) - 12.0f);
            float g = std::pow (10.0f, (-18.0f - level) / 20.0f);
            g = std::min (g, 0.7f / std::max (fp.peak, 1.0e-6f));
            const float nl = juce::jlimit (0.05f, d->params()[li].max, lvl * g);
            auto json = juce::JSON::parse (p.file);
            if (auto* vals = json.getProperty ("values", {}).getDynamicObject())
            {
                vals->setProperty (key, std::round (nl * 10000.0f) / 10000.0f);
                p.file.replaceWithText (juce::JSON::toString (json));
                ++changed;
            }
        }
    }
    presets::rescan();
    std::printf ("    balanced %d presets\n", changed);
    ctx.check (changed > 0, "balanced");
}

/** Not a check: separates factory presets that render too alike. Each preset keeps its recipe (wave,
    family, effects, name); only secondary settings move, within bounds that keep the name honest
    (e.g. cutoff ×0.75–1.33, resonance ±0.1, envelope times ×0.7–1.4). For the worst pair, the later
    preset tries a few bounded variations and keeps the one farthest from all others; repeats until
    every pair passes kMinDistance (plus a margin) or no variation helps. MB_NUDGE=acid,... limits it. */
MB_TEST (tool_nudge_presets)
{
    struct Dim { juce::String id; bool mul; float amount; };
    auto dimsFor = [] (DeviceType t)
    {
        std::vector<Dim> d;
        auto add = [&d] (juce::String id, bool mul, float a) { d.push_back ({ id, mul, a }); };
        switch (t)
        {
            case DeviceType::Mono: add ("cutoff", true, 1.33f); add ("reso", false, 0.1f); add ("envAmt", false, 0.15f); add ("decay", true, 1.4f);
                                   add ("sustain", false, 0.15f); add ("key", false, 0.2f); add ("release", true, 1.4f); break;
            case DeviceType::Poly: add ("cutoff", true, 1.33f); add ("reso", false, 0.1f); add ("envAmt", false, 0.15f); add ("fDecay", true, 1.4f);
                                   add ("aDecay", true, 1.4f); add ("fSustain", false, 0.15f); add ("aRelease", true, 1.4f); add ("key", false, 0.2f); break;
            case DeviceType::Acid: add ("cutoff", true, 1.33f); add ("reso", false, 0.08f); add ("envMod", false, 0.15f); add ("decay", true, 1.4f);
                                   add ("accAmt", false, 0.2f); add ("bassComp", false, 0.25f); add ("pw", false, 0.15f); break;
            case DeviceType::FM:
                for (int o = 1; o <= 6; ++o) { const juce::String s = "op" + juce::String (o); add (s + "Level", false, 0.08f); add (s + "R2", false, 0.08f); }
                add ("feedback", false, 0.1f);
                break;
            case DeviceType::Sampler: add ("cutoff", true, 1.8f); add ("attack", true, 1.5f); add ("decay", true, 1.4f); add ("release", true, 1.4f); break;
            case DeviceType::Drums:
                for (int p = 1; p <= 8; ++p)
                {
                    const juce::String s = "p" + juce::String (p);
                    add (s + "Pitch", true, 1.18f); add (s + "Decay", true, 1.4f); add (s + "Tone", false, 0.2f); add (s + "Click", false, 0.2f);
                }
                break;
            case DeviceType::DrumSampler:
                for (int p = 1; p <= 8; ++p) { const juce::String s = "pd" + juce::String (p); add (s + "Decay", true, 1.4f); add (s + "HiCut", true, 1.6f); }
                break;
            default: break;
        }
        return d;
    };
    const auto only = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("MB_NUDGE", ""), ",", "");
    dsp::Rng rng (2026u);
    for (int ti = 0; ti < (int) DeviceType::Count; ++ti)
    {
        const auto type = (DeviceType) ti;
        const auto dims = dimsFor (type);
        if (dims.empty() || (! only.isEmpty() && ! only.contains (deviceTypeId (type)))) continue;
        const bool drum = isDrumDevice (type);
        auto list = presets::factory (type);
        std::vector<std::unique_ptr<DeviceProcessor>> devs;
        std::vector<Fingerprint> fps;
        for (auto& p : list) { devs.push_back (createDevice (type)); presets::apply (*devs.back(), p, true); fps.push_back (render (*devs.back(), drum)); }
        const size_t n = fps.size();
        std::vector<std::vector<float>> dist (n, std::vector<float> (n, 1.0e9f));
        for (size_t i = 0; i < n; ++i) for (size_t j = i + 1; j < n; ++j) dist[i][j] = dist[j][i] = fpDistance (fps[i], fps[j]);
        std::vector<bool> changed (n, false), stuck (n, false);
        const float target = kMinDistance + 0.05f;
        int moves = 0;
        for (int iter = 0; iter < 4000; ++iter)
        {
            float worst = 1.0e9f; size_t a = 0, b = 0;
            for (size_t i = 0; i < n; ++i) for (size_t j = i + 1; j < n; ++j)
                if (dist[i][j] < worst && ! (stuck[i] && stuck[j])) { worst = dist[i][j]; a = i; b = j; }
            if (worst >= target) break;
            auto nearest = [&] (size_t k, const Fingerprint& f) { float d = 1.0e9f; for (size_t o = 0; o < n; ++o) if (o != k) d = std::min (d, fpDistance (f, fps[o])); return d; };
            // try moving either preset of the pair; keep the variation that leaves it farthest from all others
            size_t m = b;
            float bestD = 0.0f; std::unique_ptr<DeviceProcessor> best; Fingerprint bestFp;
            for (size_t cand : { b, a })
            {
                if (stuck[cand]) continue;
                const float before = nearest (cand, fps[cand]);
                for (int k = 0; k < 14; ++k)
                {
                    auto c = createDevice (type);
                    presets::apply (*c, list[cand], true);
                    for (int dd = 0; dd < (int) devs[cand]->params().size(); ++dd) c->set (dd, devs[cand]->get (dd));
                    for (int q = 0; q < 3; ++q)
                    {
                        const auto& dim = dims[(size_t) (rng.next() % dims.size())];
                        const int pi = c->params().indexOf (dim.id);
                        if (pi < 0) continue;
                        const auto& info = c->params()[pi];
                        float v = c->get (pi);
                        v = dim.mul ? v * std::pow (dim.amount, rng.bipolar()) : v + dim.amount * rng.bipolar();
                        c->set (pi, juce::jlimit (info.min, info.max, v));
                    }
                    auto f = render (*c, drum);
                    const float d = nearest (cand, f) - before;
                    if (d > bestD && f.finite) { bestD = d; best = std::move (c); bestFp = f; m = cand; }
                }
            }
            if (best == nullptr) { stuck[a] = stuck[b] = true; continue; }
            devs[m] = std::move (best); fps[m] = bestFp; changed[m] = true; ++moves;
            for (size_t o = 0; o < n; ++o) if (o != m) dist[m][o] = dist[o][m] = fpDistance (fps[m], fps[o]);
        }
        // write the moved presets back (only their own values change)
        int written = 0;
        for (size_t i = 0; i < n; ++i)
        {
            if (! changed[i]) continue;
            auto json = juce::JSON::parse (list[i].file);
            if (auto* vals = json.getProperty ("values", {}).getDynamicObject())
            {
                for (auto& dim : dims)
                {
                    const int pi = devs[i]->params().indexOf (dim.id);
                    if (pi < 0) continue;
                    vals->setProperty (dim.id, std::round (devs[i]->get (pi) * 10000.0f) / 10000.0f);
                }
                list[i].file.replaceWithText (juce::JSON::toString (json));
                ++written;
            }
        }
        float worst = 1.0e9f;
        int close = 0;
        for (size_t i = 0; i < n; ++i) for (size_t j = i + 1; j < n; ++j) { worst = std::min (worst, dist[i][j]); close += dist[i][j] < kMinDistance ? 1 : 0; }
        std::printf ("    %-12s %d moves, %d presets adjusted, closest now %.3f, pairs under %.2f: %d\n", deviceTypeId (type).toRawUTF8(), moves, written,
                     worst, kMinDistance, close);
    }
    presets::rescan();
    ctx.check (true, "nudged");
}

/** The electric pianos ring on like a struck tine (several seconds), not a short pluck. */
MB_TEST (fm_pianos_sustain)
{
    for (auto& info : presets::factory (DeviceType::FM))
    {
        if (info.family != "Electric Piano" || info.name.contains ("Muted") || info.name.contains ("Plucked") || info.name.contains ("Toy")) continue;
        auto d = createDevice (DeviceType::FM);
        presets::apply (*d, info, true);
        d->prepare (48000.0, 512);
        NoteEvent on; on.note = 60; on.velocity = 0.8f; d->handleEvent (on);
        std::vector<float> l (512), r (512);
        float peak = 0, at3 = 0;
        for (int b = 0; b < (int) (3.2 * 48000 / 512); ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
            d->render (l.data(), r.data(), 512);
            float p = 0; for (float v : l) p = std::max (p, std::abs (v));
            peak = std::max (peak, p);
            if (b * 512 > 3 * 48000) at3 = std::max (at3, p);
        }
        const float drop = dsp::gainToDb (at3 / std::max (1e-6f, peak));
        ctx.check (drop > -30.0f, "'" + info.name + "' still rings after 3 s (" + juce::String (drop, 1) + " dB)");
    }
}

/** Prints, for every preset whose name or family says pluck / muted / click, how long a held note takes to
    fall 30 dB below its peak (tool). MB_DECAY=all lists every preset. */
MB_TEST (tool_decay_report)
{
    const bool all = juce::SystemStats::getEnvironmentVariable ("MB_DECAY", "") == "all";
    for (auto t : { DeviceType::Mono, DeviceType::Poly, DeviceType::Acid, DeviceType::FM, DeviceType::Sampler })
        for (auto& info : presets::factory (t))
        {
            const auto key = (info.name + " " + info.family).toLowerCase();
            if (! all && ! key.contains ("pluck") && ! key.contains ("muted") && ! key.contains ("click") && ! key.contains ("pizz")) continue;
            auto d = createDevice (t);
            presets::apply (*d, info, true);
            d->prepare (48000.0, 256);
            NoteEvent on; on.note = t == DeviceType::Sampler ? 60 : 55; on.velocity = 0.8f; d->handleEvent (on);
            std::vector<float> l (256), r (256), env;
            for (int b = 0; b < 48000 * 3 / 256; ++b)
            {
                std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
                d->render (l.data(), r.data(), 256);
                float p = 0; for (float v : l) p = std::max (p, std::abs (v));
                env.push_back (p);
            }
            size_t pk = 0; for (size_t i = 0; i < env.size(); ++i) if (env[i] > env[pk]) pk = i;
            size_t end = pk; while (end < env.size() && env[end] > env[pk] * 0.0316f) ++end;
            std::printf ("    %-8s %-28s %-16s -30 dB after %5.2f s\n", deviceTypeName (t).toRawUTF8(), info.name.toRawUTF8(), info.family.toRawUTF8(), (end - pk) * 256 / 48000.0);
        }
    ctx.check (true, "report");
}

namespace
{
/** What a held note of a preset does: attack, ring, sustain and brightness over time. */
struct Shape { float attackMs = 0, ring30 = 0, sustainDb = 0, bright0 = 0, bright1 = 0; };

Shape measureShape (DeviceProcessor& d, int note)
{
    const double sr = 48000.0;
    d.prepare (sr, 256);
    NoteEvent on; on.note = note; on.velocity = 0.8f; d.handleEvent (on);
    std::vector<float> l (256), r (256), y;
    for (int b = 0; b < (int) (3.0 * sr / 256); ++b)
    {
        std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
        d.render (l.data(), r.data(), 256);
        y.insert (y.end(), l.begin(), l.end());
    }
    // 5 ms peak envelope
    const int hop = 240;
    std::vector<float> env;
    for (size_t i = 0; i + hop <= y.size(); i += hop) { float p = 0; for (int k = 0; k < hop; ++k) p = std::max (p, std::abs (y[i + (size_t) k])); env.push_back (p); }
    size_t pk = 0; for (size_t i = 0; i < env.size(); ++i) if (env[i] > env[pk]) pk = i;
    Shape s;
    const float peak = std::max (1.0e-6f, env[pk]);
    size_t a = 0; while (a < env.size() && env[a] < peak * 0.7f) ++a;
    s.attackMs = (float) a * 5.0f;
    size_t e = pk; while (e < env.size() && env[e] > peak * 0.0316f) ++e;
    s.ring30 = (float) (e - pk) * 0.005f;
    s.sustainDb = dsp::gainToDb (std::max (1.0e-6f, env[std::min (env.size() - 1, (size_t) 300)]) / peak);   // at 1.5 s
    auto centroid = [&] (size_t start)
    {
        juce::dsp::FFT fft (12);
        std::vector<float> buf (8192, 0.0f);
        for (int i = 0; i < 4096 && start + (size_t) i < y.size(); ++i) buf[(size_t) i] = y[start + (size_t) i] * (0.5f - 0.5f * std::cos (dsp::kTwoPi * i / 4096.0f));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double num = 0, den = 0;
        for (int b = 1; b < 2048; ++b) { num += b * (double) buf[(size_t) b]; den += buf[(size_t) b]; }
        const double hz = den > 0 ? num / den * sr / 4096.0 : 0.0;
        return (float) (hz / dsp::midiToHz ((float) note));   // in harmonics of the played note
    };
    s.bright0 = centroid (pk * hop);
    s.bright1 = centroid ((size_t) (0.6 * sr));
    return s;
}
} // namespace

/** Checks that each preset behaves like its name and family say (tool: prints mismatches; MB_AUDIT=all lists
    every preset's shape). */
MB_TEST (tool_preset_audit)
{
    const bool all = juce::SystemStats::getEnvironmentVariable ("MB_AUDIT", "") == "all";
    int bad = 0;
    for (auto t : { DeviceType::Mono, DeviceType::Poly, DeviceType::Acid, DeviceType::FM, DeviceType::Sampler })
        for (auto& info : presets::factory (t))
        {
            auto d = createDevice (t);
            presets::apply (*d, info, true);
            for (auto fx : { "reverb", "fxReverb", "delayMix", "fxDelay", "chorusMix", "fxChorus" }) d->set (fx, 0.0f);   // the dry envelope
            const auto s = measureShape (*d, t == DeviceType::Sampler ? 60 : 55);
            const auto k = info.name.toLowerCase() + (info.family == "Pluck" ? " pluck" : "") + (info.family == "Brass" ? " brass" : "");
            auto has = [&k] (std::initializer_list<const char*> w) { for (auto x : w) if (k.contains (x)) return true; return false; };
            juce::StringArray why;
            if (has ({ "pluck", "pizz", "harp" }) && (s.ring30 < 0.4f || s.attackMs > 20)) why.add ("pluck: needs a fast attack and a 0.4 s+ ring");
            if (has ({ "muted" }) && s.ring30 < 0.25f) why.add ("muted: rings too short (a click)");
            if (has ({ "piano", "tine", "keys" }) && ! has ({ "muted", "toy", "pluck" }) && s.ring30 < 1.5f) why.add ("piano/keys: should ring 1.5 s+");
            if (has ({ "bell", "chime" }) && s.ring30 < 1.0f) why.add ("bell: should ring 1 s+");
            if (has ({ "brass", "horn", "trumpet", "trombone", "tuba", "sax", "fanfare" }) && ! has ({ "pad", "distant", "swell" }))
            {
                if (s.attackMs < 8) why.add ("brass: no attack swell (instant)");
                if (! has ({ "stab" }) && s.sustainDb < -12) why.add ("brass: doesn't sustain");
                if (s.bright0 < 3.0f && ! has ({ "tuba", "trombone", "muted", "horn", "dark", "soft" })) why.add ("brass: too dull");
            }
            if (has ({ "stab" }) && (s.attackMs > 60 || s.ring30 < 0.15f)) why.add ("stab: needs punch and some body");
            if (has ({ " pad", "pad ", "choir", "strings" }) && ! has ({ "pizz", "pluck" }) && s.attackMs < 40) why.add ("pad/strings: attack too fast");
            if (has ({ "organ" }) && (s.sustainDb < -4 || s.attackMs > 60)) why.add ("organ: should sustain flat");
            if (has ({ "swell", "rising", "slow", "fade" }) && s.attackMs < 120) why.add ("swell: attack too fast");
            if (has ({ "lead" }) && ! has ({ "pluck" }) && s.sustainDb < -18) why.add ("lead: doesn't sustain");
            if (all || ! why.isEmpty())
                std::printf ("    %-8s %-28s %-16s atk %5.0f ms  ring %5.2f s  sus %6.1f dB  bright %4.1f→%4.1f  %s\n",
                             deviceTypeName (t).toRawUTF8(), info.name.toRawUTF8(), info.family.toRawUTF8(), s.attackMs, s.ring30, s.sustainDb,
                             s.bright0, s.bright1, why.joinIntoString ("; ").toRawUTF8());
            bad += why.isEmpty() ? 0 : 1;
        }
    std::printf ("    %d presets don't match their description\n", bad);
    ctx.check (true, "audit");
}
