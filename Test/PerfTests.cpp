#include "Tests.h"
#include "engine/Engine.h"
#include "app/presets/PresetBank.h"

using namespace mb;

MB_TEST (perf_full_rack_cpu)
{
    Engine e;
    e.prepare (48000.0, 128);
    const DeviceType types[4] = { DeviceType::Drums, DeviceType::Acid, DeviceType::Poly, DeviceType::FM };
    for (int t = 0; t < 4; ++t) { auto d = createDevice (types[t]); presets::loadDefault (*d, true); e.setDevice (0, t, std::move (d)); }
    auto& p = e.project();
    for (int s = 0; s < 16; ++s)
    {
        for (int l = 0; l < 8; ++l) p.patterns[0].slots[0].track.lanes[(size_t) l][(size_t) s].on = (s + l) % 2 == 0;
        p.patterns[0].slots[1].track.steps[(size_t) s].addNote (36 + s % 5);
        if (s % 4 == 0) { for (int n : { 48, 52, 55, 59, 62, 65 }) p.patterns[0].slots[2].track.steps[(size_t) s].addNote (n); p.patterns[0].slots[2].track.steps[(size_t) s].setLength (512); }
        if (s % 2 == 0) for (int n : { 60, 64, 67, 71, 72, 76, 79, 83 }) p.patterns[0].slots[3].track.steps[(size_t) s].addNote (n);
        p.patterns[0].slots[3].track.steps[(size_t) s].setLength (512);
    }
    e.play();
    std::vector<float> l (128), r (128);
    const int blocks = (int) (10.0 * 48000 / 128);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < blocks; ++i) e.process (l.data(), r.data(), 128, nullptr);
    const double load = (juce::Time::getMillisecondCounterHiRes() - t0) / 10000.0;
    std::printf ("    full rack: %.1f %% of one core (2x oversampling)\n", load * 100.0);
    ctx.check (load < 0.15, "full rack under 15 % of one core: " + juce::String (load * 100.0, 1) + " %");
}

/** Not a check: the same workload, one device at a time (where the CPU goes). */
MB_TEST (tool_perf_per_device)
{
    const DeviceType types[4] = { DeviceType::Drums, DeviceType::Acid, DeviceType::Poly, DeviceType::FM };
    for (int only = 0; only < 4; ++only)
    {
        Engine e;
        e.prepare (48000.0, 128);
        auto d = createDevice (types[only]); presets::loadDefault (*d, true); e.setDevice (0, only, std::move (d));
        auto& p = e.project();
        for (int s = 0; s < 16; ++s)
        {
            for (int l = 0; l < 8; ++l) p.patterns[0].slots[0].track.lanes[(size_t) l][(size_t) s].on = (s + l) % 2 == 0;
            p.patterns[0].slots[1].track.steps[(size_t) s].addNote (36 + s % 5);
            if (s % 4 == 0) { for (int n : { 48, 52, 55, 59, 62, 65 }) p.patterns[0].slots[2].track.steps[(size_t) s].addNote (n); p.patterns[0].slots[2].track.steps[(size_t) s].setLength (512); }
            if (s % 2 == 0) for (int n : { 60, 64, 67, 71, 72, 76, 79, 83 }) p.patterns[0].slots[3].track.steps[(size_t) s].addNote (n);
            p.patterns[0].slots[3].track.steps[(size_t) s].setLength (512);
        }
        e.play();
        std::vector<float> l (128), r (128);
        const int blocks = (int) (10.0 * 48000 / 128);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < blocks; ++i) e.process (l.data(), r.data(), 128, nullptr);
        std::printf ("    %-6s %.2f %%\n", deviceTypeName (types[only]).toRawUTF8(), (juce::Time::getMillisecondCounterHiRes() - t0) / 100.0);
    }
    ctx.check (true, "measured");
}

/** Not a check: each device with its preset's reverb card on and off (how much the per-device plates cost). */
MB_TEST (tool_perf_reverb_share)
{
    const DeviceType types[4] = { DeviceType::Drums, DeviceType::Acid, DeviceType::Poly, DeviceType::FM };
    for (int only = 0; only < 4; ++only)
        for (int rev = 0; rev < 2; ++rev)
        {
            Engine e;
            e.prepare (48000.0, 128);
            auto d = createDevice (types[only]); presets::loadDefault (*d, true);
            for (auto id : { "reverb", "fxReverb", "kitReverb" }) if (d->params().indexOf (id) >= 0) d->set (id, rev ? 0.3f : 0.0f);
            e.setDevice (0, only, std::move (d));
            auto& p = e.project();
            p.master.reverbReturn = 0.0f; p.master.delayReturn = 0.0f;
            for (int s = 0; s < 16; ++s)
            {
                for (int l = 0; l < 8; ++l) p.patterns[0].slots[0].track.lanes[(size_t) l][(size_t) s].on = (s + l) % 2 == 0;
                p.patterns[0].slots[1].track.steps[(size_t) s].addNote (36 + s % 5);
                if (s % 4 == 0) { for (int n : { 48, 52, 55, 59, 62, 65 }) p.patterns[0].slots[2].track.steps[(size_t) s].addNote (n); p.patterns[0].slots[2].track.steps[(size_t) s].setLength (512); }
                if (s % 2 == 0) for (int n : { 60, 64, 67, 71, 72, 76, 79, 83 }) p.patterns[0].slots[3].track.steps[(size_t) s].addNote (n);
                p.patterns[0].slots[3].track.steps[(size_t) s].setLength (512);
            }
            e.play();
            std::vector<float> l (128), r (128);
            const int blocks = (int) (10.0 * 48000 / 128);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < blocks; ++i) e.process (l.data(), r.data(), 128, nullptr);
            std::printf ("    %-6s reverb %s %.2f %%\n", deviceTypeName (types[only]).toRawUTF8(), rev ? "on " : "off", (juce::Time::getMillisecondCounterHiRes() - t0) / 100.0);
        }
    ctx.check (true, "measured");
}

/** Eco (low-end phones): the same busy rack costs clearly less, still plays (finite, audible), and the devices'
    Reverb cards go to the master reverb instead of their own plates. */
MB_TEST (eco_tier_costs_less)
{
    auto run = [] (bool ecoOn, float& peak)
    {
        qualitySetting() = ecoOn ? (int) Quality::Eco : (int) Quality::Full;
        Engine e;
        e.prepare (48000.0, 128);
        const DeviceType types[4] = { DeviceType::Drums, DeviceType::Acid, DeviceType::Poly, DeviceType::FM };
        for (int t = 0; t < 4; ++t)
        {
            auto d = createDevice (types[t]); presets::loadDefault (*d, true);
            for (auto id : { "reverb", "fxReverb", "kitReverb" }) if (d->params().indexOf (id) >= 0) d->set (id, 0.3f);
            e.setDevice (0, t, std::move (d));
        }
        auto& p = e.project();
        for (int s = 0; s < 16; ++s)
        {
            for (int l = 0; l < 8; ++l) p.patterns[0].slots[0].track.lanes[(size_t) l][(size_t) s].on = (s + l) % 2 == 0;
            p.patterns[0].slots[1].track.steps[(size_t) s].addNote (36 + s % 5);
            if (s % 4 == 0) { for (int n : { 48, 52, 55, 59, 62, 65 }) p.patterns[0].slots[2].track.steps[(size_t) s].addNote (n); p.patterns[0].slots[2].track.steps[(size_t) s].setLength (512); }
            if (s % 2 == 0) for (int n : { 60, 64, 67, 71, 72, 76, 79, 83 }) p.patterns[0].slots[3].track.steps[(size_t) s].addNote (n);
            p.patterns[0].slots[3].track.steps[(size_t) s].setLength (512);
        }
        e.play();
        std::vector<float> l (128), r (128);
        peak = 0.0f;
        bool finite = true;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < (int) (8.0 * 48000 / 128); ++i)
        {
            e.process (l.data(), r.data(), 128, nullptr);
            for (int k = 0; k < 128; ++k) { finite = finite && std::isfinite (l[(size_t) k]); peak = std::max (peak, std::abs (l[(size_t) k])); }
        }
        if (! finite) peak = -1.0f;
        return juce::Time::getMillisecondCounterHiRes() - t0;
    };
    float fullPeak = 0, ecoPeak = 0;
    run (false, fullPeak);   // warm up caches
    const double full = run (false, fullPeak);
    const double ecoT = run (true, ecoPeak);
    qualitySetting() = (int) Quality::Full;
    std::printf ("    full %.0f ms, eco %.0f ms (%.0f %%)\n", full, ecoT, 100.0 * ecoT / full);
    ctx.check (ecoT < 0.8 * full, "Eco costs at least 20 % less (" + juce::String (100.0 * ecoT / full, 0) + " % of Full)");
    ctx.check (ecoPeak > 0.05f, "Eco still plays, finite and audible (peak " + juce::String (ecoPeak, 3) + ")");
    ctx.check (voiceCap (6) == 6, "Full keeps the polyphony");
}

/** Not a check: the VA holding six-note chords on each engine (the phone's heaviest single device). */
MB_TEST (tool_perf_va_chords)
{
    for (int engineVa = 0; engineVa < 2; ++engineVa)
    {
        Engine e;
        e.prepare (48000.0, 128);
        auto d = createDevice (DeviceType::Poly); presets::loadDefault (*d, true);
        d->set ("engine", (float) engineVa);
        e.setDevice (0, 2, std::move (d));
        auto& p = e.project();
        for (int s = 0; s < 16; s += 4) { for (int n : { 48, 52, 55, 59, 62, 65 }) p.patterns[0].slots[2].track.steps[(size_t) s].addNote (n); p.patterns[0].slots[2].track.steps[(size_t) s].setLength (512); }
        e.play();
        std::vector<float> l (128), r (128);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < (int) (10.0 * 48000 / 128); ++i) e.process (l.data(), r.data(), 128, nullptr);
        std::printf ("    VA chords (%s engine) %.2f %%\n", engineVa ? "VCO" : "DCO", (juce::Time::getMillisecondCounterHiRes() - t0) / 100.0);
    }
    ctx.check (true, "measured");
}

/** Not a check: every device on a busy part (the baseline for the CPU work on the phone). */
MB_TEST (tool_perf_all_devices)
{
    const DeviceType types[7] = { DeviceType::Mono, DeviceType::Poly, DeviceType::Acid, DeviceType::FM, DeviceType::Sampler, DeviceType::Drums, DeviceType::DrumSampler };
    for (auto type : types)
    {
        Engine e;
        e.prepare (48000.0, 128);
        auto d = createDevice (type); presets::loadDefault (*d, true);
        const bool drums = d->laneCount() > 0;
        e.setDevice (0, 0, std::move (d));
        auto& tr = e.project().patterns[0].slots[0].track;
        for (int s = 0; s < 16; ++s)
        {
            if (drums) { for (int l = 0; l < 8; ++l) tr.lanes[(size_t) l][(size_t) s].on = (s + l) % 2 == 0; continue; }
            if (type == DeviceType::Mono || type == DeviceType::Acid) { tr.steps[(size_t) s].addNote (36 + s % 5); continue; }
            if (s % 4 == 0) { for (int n : { 48, 52, 55, 59, 62, 65 }) tr.steps[(size_t) s].addNote (n); tr.steps[(size_t) s].setLength (512); }
        }
        e.play();
        std::vector<float> l (128), r (128);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < (int) (10.0 * 48000 / 128); ++i) e.process (l.data(), r.data(), 128, nullptr);
        std::printf ("    %-13s %.2f %%\n", deviceTypeName (type).toRawUTF8(), (juce::Time::getMillisecondCounterHiRes() - t0) / 100.0);
    }
    {
        Engine e;
        e.prepare (48000.0, 128);
        e.play();
        std::vector<float> l (128), r (128);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < (int) (10.0 * 48000 / 128); ++i) e.process (l.data(), r.data(), 128, nullptr);
        std::printf ("    %-13s %.2f %%\n", "(empty)", (juce::Time::getMillisecondCounterHiRes() - t0) / 100.0);
    }
    ctx.check (true, "measured");
}

/** The phone's 1× voices: the VA (both engines) and the Bassline still play (finite, audible) and cost less. */
MB_TEST (voices_at_1x)
{
    auto run = [] (DeviceType type, float engine, bool os, float& peak)
    {
        oversampleVoices() = os;
        Engine e;
        e.prepare (48000.0, 128);
        auto d = createDevice (type); presets::loadDefault (*d, true);
        if (type == DeviceType::Poly) d->set ("engine", engine);
        e.setDevice (0, 0, std::move (d));
        auto& tr = e.project().patterns[0].slots[0].track;
        for (int s = 0; s < 16; s += 4) { for (int n : { 48, 52, 55, 59, 62, 65 }) tr.steps[(size_t) s].addNote (n); tr.steps[(size_t) s].setLength (512); }
        e.play();
        std::vector<float> l (128), r (128);
        peak = 0.0f;
        bool finite = true;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < (int) (6.0 * 48000 / 128); ++i)
        {
            e.process (l.data(), r.data(), 128, nullptr);
            for (float v : l) { finite = finite && std::isfinite (v); peak = std::max (peak, std::abs (v)); }
        }
        if (! finite) peak = -1.0f;
        return juce::Time::getMillisecondCounterHiRes() - t0;
    };
    for (auto [type, engine, name] : { std::tuple { DeviceType::Poly, 0.0f, "VA DCO" }, std::tuple { DeviceType::Poly, 1.0f, "VA VCO" },
                                       std::tuple { DeviceType::Mono, 0.0f, "Bassline" } })
    {
        float p2 = 0, p1 = 0;
        const double t2 = run (type, engine, true, p2), t1 = run (type, engine, false, p1);
        std::printf ("    %-8s 2x %.0f ms, 1x %.0f ms\n", name, t2, t1);
        ctx.check (p1 > 0.02f && p1 < 4.0f, juce::String (name) + " plays at 1x (peak " + juce::String (p1, 3) + ")");
        ctx.check (t1 < t2, juce::String (name) + " costs less at 1x");
    }
    oversampleVoices() = true;
}
