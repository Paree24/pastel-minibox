#include "Tests.h"
#include "devices/DeviceProcessor.h"

using namespace mb;

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 128;

struct Result { float peak = 0.0f; int clicks = 0; double firstClick = -1.0; };

/** Clicks: the test sounds are dark (filters ≤ 1.5 kHz, soft envelopes), so a working engine puts
    nothing above 8 kHz; a discontinuity (a cut voice, a gate or parameter step) splashes energy there.
    The signal is high-passed at 8 kHz (two 2nd-order sections) and any excursion more than −36 dB
    under the signal's peak is a click (one per 10 ms): relative, so it means the same at any level. */
Result analyse (const std::vector<float>& x)
{
    Result r;
    struct Biquad { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
        double run (double v) { const double y = b0 * v + z1; z1 = b1 * v - a1 * y + z2; z2 = b2 * v - a2 * y; return y; } };
    const double w = 2.0 * 3.14159265358979 * 8000.0 / kSr, cw = std::cos (w), al = std::sin (w) / (2.0 * 0.7071);
    const double a0 = 1.0 + al;
    Biquad h1 { (1 + cw) / 2 / a0, -(1 + cw) / a0, (1 + cw) / 2 / a0, -2 * cw / a0, (1 - al) / a0 };
    Biquad h2 = h1;
    int hold = 0;
    for (float v : x) r.peak = std::max (r.peak, std::abs (v));
    const double thresh = 0.0158 * std::max (0.05f, r.peak);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double hp = h2.run (h1.run (x[i]));
        if (hold > 0) { --hold; continue; }
        if (i > 64 && std::abs (hp) > thresh)
        {
            ++r.clicks; hold = (int) (0.01 * kSr);
            if (r.firstClick < 0.0) r.firstClick = (double) i / kSr;
        }
    }
    return r;
}

/** scenario 0: staccato 16ths, 1: 8-note chords every 250 ms (voice stealing), 2: held chord with the
    cutoff knob swept every block. */
Result run (DeviceType t, const std::vector<std::pair<const char*, float>>& set, int scenario)
{
    auto d = createDevice (t);
    for (auto& [id, v] : set) d->set (id, v);
    d->prepare (kSr, kBlock);
    TransportInfo ti; ti.bpm = 140.0; ti.playing = true;
    const int total = (int) (4.0 * kSr);
    std::vector<float> out ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
    dsp::Rng rng (77u);
    std::vector<int> sounding;
    auto on = [&] (int n, float v) { NoteEvent e; e.note = n; e.velocity = v; d->handleEvent (e); sounding.push_back (n); };
    auto offAll = [&] { for (int n : sounding) { NoteEvent e; e.type = NoteEvent::NoteOff; e.note = n; d->handleEvent (e); } sounding.clear(); };
    const int step = (int) (60.0 / 140.0 / 4.0 * kSr);
    const int cutIdx = d->params().indexOf ("cutoff");
    for (int pos = 0; pos < total; pos += kBlock)
    {
        if (scenario == 0 && pos % step < kBlock)
        {
            offAll();
            if (pos / step % 4 != 3) on (40 + (int) (rng.unipolar() * 30.0f), 0.5f + 0.5f * rng.unipolar());
        }
        if (scenario == 1 && pos % (int) (0.25 * kSr) < kBlock)
        {
            offAll();
            const int root = 45 + (int) (rng.unipolar() * 12.0f);
            for (int k = 0; k < 8; ++k) on (root + k * 3 + (k > 3 ? 2 : 0), 1.0f);
        }
        if (scenario == 2)
        {
            if (pos == 0) for (int nn : { 48, 55, 60, 64 }) on (nn, 0.9f);
            if (cutIdx >= 0)
            {
                const double ph = 0.5 + 0.5 * std::sin (6.2831853 * 0.7 * pos / kSr);
                d->set (cutIdx, (float) (100.0 * std::pow (15.0, ph)));   // a hand on the knob: 100 Hz – 1.5 kHz
            }
        }
        d->setTransport (ti);
        d->render (out.data() + pos, r.data() + pos, std::min (kBlock, total - pos));
    }
    if (juce::SystemStats::getEnvironmentVariable ("MB_DUMP", "").isNotEmpty())
    {
        juce::File f (juce::File (MINIBOX_SOURCE_DIR).getChildFile ("build/torture_dump.raw"));
        f.replaceWithData (out.data(), out.size() * sizeof (float));
    }
    // each channel on its own (combining them non-linearly would itself make corners)
    auto a = analyse (out), b = analyse (r);
    a.peak = std::max (a.peak, b.peak);
    a.clicks += b.clicks;
    if (a.firstClick < 0.0 || (b.firstClick >= 0.0 && b.firstClick < a.firstClick)) a.firstClick = b.firstClick;
    return a;
}
} // namespace

MB_TEST (tool_click_probe)
{
    // isolates where a device's clicks come from (not a check)
    const std::vector<std::pair<const char*, std::vector<std::pair<const char*, float>>>> probes = {
        { "DCO plain", { { "cutoff", 900 }, { "aAttack", 0.006f }, { "aRelease", 0.04f } } },
        { "DCO 1 voice", { { "cutoff", 900 }, { "aAttack", 0.006f }, { "aRelease", 0.04f }, { "voiceMode", 0 } } },
        { "DCO no sub", { { "cutoff", 900 }, { "sub", 0 }, { "aAttack", 0.006f }, { "aRelease", 0.04f } } },
        { "DCO pulse", { { "dcoWave", 0 }, { "cutoff", 900 }, { "aAttack", 0.006f }, { "aRelease", 0.04f } } },
        { "DCO cut 300", { { "cutoff", 300 }, { "aAttack", 0.006f }, { "aRelease", 0.04f } } },
        { "DCO hpf0", { { "cutoff", 900 }, { "hpf", 0 }, { "aAttack", 0.006f }, { "aRelease", 0.04f } } },
        { "DCO env src", { { "cutoff", 900 }, { "ampSrc", 1 }, { "fAttack", 0.006f }, { "fRelease", 0.04f } } },
        { "VA plain", { { "engine", 1 }, { "cutoff", 900 }, { "aAttack", 0.006f }, { "aRelease", 0.04f } } },
    };
    {
        const auto r = run (DeviceType::Mono, { { "cutoff", 900 }, { "reso", 0.6f }, { "attack", 0.006f }, { "release", 0.04f } }, 0);
        std::printf ("    Mono staccato %d (%.4f s)\n", r.clicks, r.firstClick);
    }
    if (juce::SystemStats::getEnvironmentVariable ("MB_MONO_ONLY", "").isNotEmpty()) { ctx.check (true, "mono"); return; }
    if (juce::SystemStats::getEnvironmentVariable ("MB_ACID", "").isNotEmpty())
    {
        const auto r = run (DeviceType::Acid, { { "cutoff", 400 }, { "reso", 0.7f }, { "envMod", 0.2f }, { "decay", 0.4f } }, 0);
        std::printf ("    Acid staccato %d (%.4f s)\n", r.clicks, r.firstClick);
        ctx.check (true, "acid"); return;
    }
    for (auto& [name, set] : probes)
    {
        const auto r = run (DeviceType::Poly, set, 0);
        const auto r1 = run (DeviceType::Poly, set, 1);
        std::printf ("    %-14s staccato %d (%.3f s)  stealing %d (%.3f s)\n", name, r.clicks, r.firstClick, r1.clicks, r1.firstClick);
    }
    ctx.check (true, "probed");
}

MB_TEST (synths_no_clicks_no_clipping)
{
    struct Case { DeviceType t; const char* name; std::vector<std::pair<const char*, float>> smooth; };
    // dark, soft settings: a correct engine is silent above 8 kHz with these
    const Case cases[] = {
        { DeviceType::Mono, "Mono", { { "cutoff", 900 }, { "reso", 0.6f }, { "attack", 0.006f }, { "release", 0.04f } } },
        { DeviceType::Poly, "Poly DCO", { { "cutoff", 900 }, { "reso", 0.5f }, { "aAttack", 0.006f }, { "aRelease", 0.04f },
                                          { "fAttack", 0.006f }, { "chorusMode", 3.0f }, { "chorusMix", 0.8f } } },
        { DeviceType::Poly, "Poly VA", { { "engine", 1 }, { "cutoff", 900 }, { "reso", 0.5f }, { "aAttack", 0.006f }, { "aRelease", 0.04f } } },
        { DeviceType::Poly, "Poly VA shared", { { "engine", 1 }, { "filterMode", 0 }, { "cutoff", 900 }, { "reso", 0.6f }, { "aAttack", 0.006f },
                                                { "aRelease", 0.04f }, { "fAttack", 0.006f } } },
        { DeviceType::Acid, "Acid", { { "cutoff", 400 }, { "reso", 0.7f }, { "envMod", 0.2f }, { "decay", 0.4f } } },
        { DeviceType::FM, "FM sine", { { "op1R1", 0.8f }, { "op1R4", 0.75f }, { "op3Level", 0.0f }, { "op5Level", 0.0f } } },
    };
    const char* scen[3] = { "staccato", "stealing", "knob sweep" };
    for (auto& c : cases)
        for (int s = 0; s < 3; ++s)
        {
            const auto r = run (c.t, c.smooth, s);
            std::printf ("    %-16s %-10s clicks %d%s\n", c.name, scen[s], r.clicks,
                         r.clicks ? (" (first at " + juce::String (r.firstClick, 3) + " s)").toRawUTF8() : "");
            ctx.check (r.clicks == 0, juce::String (c.name) + " " + scen[s] + " clicks " + juce::String (r.clicks));
        }
    // loudness: default sounds, the worst case is the 8-note full-velocity chord change
    for (auto t : { DeviceType::Mono, DeviceType::Poly, DeviceType::Acid, DeviceType::FM })
        for (int s = 0; s < 2; ++s)
        {
            const auto r = run (t, {}, s);
            std::printf ("    %-16s %-10s peak %.2f\n", deviceTypeName (t).toRawUTF8(), scen[s], r.peak);
            ctx.check (r.peak < 1.0f, deviceTypeName (t) + " " + scen[s] + " peak " + juce::String (r.peak, 2));
        }
}

MB_TEST (legato_keys_retrigger_plucks)
{
    // a pluck played legato on a keyboard: the second key must sound like the first (not be cut)
    struct Case { DeviceType t; const char* name; std::vector<std::pair<const char*, float>> set; };
    const Case cases[] = {
        { DeviceType::Mono, "Mono pluck", { { "attack", 0.001f }, { "decay", 0.2f }, { "sustain", 0.0f }, { "release", 0.2f } } },
        { DeviceType::Acid, "Acid pluck", { { "ampMode", 1.0f }, { "ampDecay", 0.2f } } },
        { DeviceType::Poly, "Poly pluck", { { "aAttack", 0.001f }, { "aDecay", 0.2f }, { "aSustain", 0.0f } } },
    };
    for (auto& c : cases)
    {
        auto d = createDevice (c.t);
        for (auto& [id, v] : c.set) d->set (id, v);
        d->prepare (kSr, kBlock);
        const int total = (int) (1.2 * kSr);
        std::vector<float> l ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
        for (int pos = 0; pos < total; pos += kBlock)
        {
            if (pos == 0) { NoteEvent e; e.note = juce::SystemStats::getEnvironmentVariable ("MB_SWAP", "").isNotEmpty() ? 52 : 48; e.velocity = 0.8f; d->handleEvent (e); }
            if (pos == (int) (0.6 * kSr) / kBlock * kBlock && juce::SystemStats::getEnvironmentVariable ("MB_NOLEG", "").isNotEmpty())
            { NoteEvent off; off.type = NoteEvent::NoteOff; off.note = juce::SystemStats::getEnvironmentVariable ("MB_SWAP", "").isNotEmpty() ? 52 : 48; d->handleEvent (off); }
            if (pos == (int) (0.6 * kSr) / kBlock * kBlock) { NoteEvent e; e.note = juce::SystemStats::getEnvironmentVariable ("MB_SWAP", "").isNotEmpty() ? 48 : 52; e.velocity = 0.8f; d->handleEvent (e); }
            d->render (l.data() + pos, r.data() + pos, std::min (kBlock, total - pos));
        }
        auto rms = [&] (double t0) { double s = 0; const int a = (int) (t0 * kSr), n = (int) (0.06 * kSr);
                                     for (int i = a; i < a + n; ++i) s += l[(size_t) i] * l[(size_t) i]; return std::sqrt (s / n); };
        const double first = rms (0.0), second = rms ((int) (0.6 * kSr) / kBlock * kBlock / kSr);
        auto pk = [&] (double t0) { float m = 0; const int a = (int) (t0 * kSr); for (int i = a; i < a + (int) (0.06 * kSr); ++i) m = std::max (m, std::abs (l[(size_t) i])); return m; };
        std::printf ("    %-11s first %.3f (peak %.3f)  legato second %.3f (peak %.3f)\n", c.name, first, pk (0.0), second, pk ((int) (0.6 * kSr) / kBlock * kBlock / kSr));
        ctx.check (second > 0.6 * first, juce::String (c.name) + ": legato key cut (" + juce::String (second / first, 2) + "×)");
    }
}
