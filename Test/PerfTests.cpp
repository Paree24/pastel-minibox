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
