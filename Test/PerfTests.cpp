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
