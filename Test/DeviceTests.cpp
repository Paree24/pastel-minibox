#include "Tests.h"
#include "devices/DeviceProcessor.h"
#include "devices/fm/FmDevice.h"
#include <cmath>
#include <tuple>

using namespace mb;

namespace
{
struct Render { std::vector<float> l, r; float peak = 0.0f, rms = 0.0f; bool finite = true; };

Render renderNotes (DeviceProcessor& d, std::vector<int> notes, double seconds, double holdSeconds, double sr = 48000.0)
{
    const int block = 256;
    d.prepare (sr, block);
    Render out;
    const int total = (int) (seconds * sr);
    out.l.assign ((size_t) total, 0.0f); out.r.assign ((size_t) total, 0.0f);
    TransportInfo t; t.bpm = 120.0; t.playing = true;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        if (pos == 0) for (int nn : notes) { NoteEvent e; e.note = nn; e.velocity = 0.8f; d.handleEvent (e); }
        if (pos <= (int) (holdSeconds * sr) && pos + block > (int) (holdSeconds * sr))
            for (int nn : notes) { NoteEvent e; e.type = NoteEvent::NoteOff; e.note = nn; d.handleEvent (e); }
        t.beatPos = pos / sr * 2.0;
        d.setTransport (t);
        d.render (out.l.data() + pos, out.r.data() + pos, n);
        d.clearLocks();
    }
    double sq = 0.0;
    for (int i = 0; i < total; ++i)
    {
        const float a = out.l[(size_t) i], b = out.r[(size_t) i];
        if (! std::isfinite (a) || ! std::isfinite (b)) out.finite = false;
        out.peak = std::max ({ out.peak, std::abs (a), std::abs (b) });
        sq += a * a + b * b;
    }
    out.rms = (float) std::sqrt (sq / (2.0 * total));
    return out;
}
} // namespace

MB_TEST (devices_init_render)
{
    for (int t = 0; t < (int) DeviceType::Count; ++t)
    {
        auto d = createDevice ((DeviceType) t);
        ctx.check (d != nullptr, "create " + deviceTypeName ((DeviceType) t));
        if (! d) continue;
        const bool drum = isDrumDevice (d->type());
        std::vector<int> notes = drum ? std::vector<int> { 36, 38, 42 } : std::vector<int> { 60 };
        auto r = renderNotes (*d, notes, 1.5, 0.6);
        const auto name = deviceTypeName ((DeviceType) t);
        ctx.check (r.finite, name + " finite");
        ctx.check (r.peak < 4.0f, name + " bounded, peak " + juce::String (r.peak));
        // samplers are silent until a sample is loaded
        if (d->type() != DeviceType::Sampler && d->type() != DeviceType::DrumSampler)
            ctx.check (r.rms > 1.0e-3f, name + " audible, rms " + juce::String (r.rms));
    }
}

MB_TEST (devices_release_to_silence)
{
    for (int t = 0; t < (int) DeviceType::Count; ++t)
    {
        auto d = createDevice ((DeviceType) t);
        auto r = renderNotes (*d, { 48 }, 4.0, 0.2);
        float tail = 0.0f;
        for (size_t i = r.l.size() - 4800; i < r.l.size(); ++i) tail = std::max (tail, std::abs (r.l[i]));
        ctx.check (tail < 1.0e-3f, deviceTypeName ((DeviceType) t) + " tail decays, " + juce::String (tail));
    }
}

MB_TEST (devices_state_roundtrip)
{
    for (int t = 0; t < (int) DeviceType::Count; ++t)
    {
        auto a = createDevice ((DeviceType) t);
        juce::Random rnd (42 + t);
        for (int i = 0; i < a->params().size(); ++i) a->setNorm (i, rnd.nextFloat());
        juce::ValueTree s1 ("Device");
        a->save (s1);
        auto b = createDevice ((DeviceType) t);
        b->load (s1);
        juce::ValueTree s2 ("Device");
        b->save (s2);
        ctx.check (s1.toXmlString() == s2.toXmlString(), deviceTypeName ((DeviceType) t) + " save/load/save identical");
    }
}

MB_TEST (fm_algorithms_all_render)
{
    FmDevice d;
    for (int o = 0; o < 6; ++o) d.set (d.opParam (o, FmDevice::Level), 0.8f);
    for (int a = 1; a <= 32; ++a)
    {
        d.set ("algorithm", (float) a);
        d.set ("feedback", 1.0f);
        auto r = renderNotes (d, { 60 }, 0.4, 0.3);
        ctx.check (r.finite && r.peak < 4.0f && r.rms > 1.0e-3f, "algorithm " + juce::String (a));
    }
}

// Poly arp follows the transport: with playback running from mid-beat, steps
// count from the key in transport beats (1/16 here, 1/8 D from bar 2)
MB_TEST (poly_arp_locks_to_transport)
{
    const double sr = 48000.0;
    const int block = 256;
    for (auto [rate, startBeat, stepSamples] : { std::tuple<float, double, double> { 10.0f, 0.4, 6000.0 },
                                                 std::tuple<float, double, double> { 9.0f, 4.0, 18000.0 } })
    {
        auto d = createDevice (DeviceType::Poly);
        d->prepare (sr, block);
        d->set ("arpMode", 1.0f); d->set ("arpRate", rate); d->set ("arpGate", 0.3f);
        d->set ("aRelease", 0.005f); // short release: the gate leaves a gap between steps
        TransportInfo t; t.bpm = 120.0; t.playing = true;
        std::vector<float> l ((size_t) (3.0 * sr), 0.0f), r (l.size(), 0.0f);
        for (int pos = 0; pos < (int) l.size(); pos += block)
        {
            if (pos == 0) { NoteEvent e; e.note = 60; e.velocity = 0.8f; d->handleEvent (e); }
            t.beatPos = startBeat + pos / sr * 2.0;
            d->setTransport (t);
            d->render (l.data() + pos, r.data() + pos, std::min (block, (int) l.size() - pos));
        }
        // onsets: first audible sample after a quiet stretch that grows into an attack
        std::vector<int> on; int quiet = 1000;
        for (int i = 0; i < (int) l.size(); ++i)
        {
            if (std::abs (l[(size_t) i]) <= 1.0e-3f) { ++quiet; continue; }
            float ahead = 0.0f;
            for (int j = i; j < std::min ((int) l.size(), i + 300); ++j) ahead = std::max (ahead, std::abs (l[(size_t) j]));
            if (quiet > 200 && ahead > 0.03f) on.push_back (i);
            quiet = 0;
        }
        int bad = 0;
        for (int o : on)
        {
            const double off = o - std::round (o / stepSamples) * stepSamples;
            if (off < -1.0 || off > 150.0) ++bad;
        }
        const juce::String tag = "rate " + juce::String (rate) + ": ";
        ctx.check (on.size() >= 7, tag + "arp steps sounded: " + juce::String ((int) on.size()));
        ctx.check (bad == 0, tag + juce::String (bad) + " onsets off the even step grid");
        ctx.check (! on.empty() && on[0] < 150, tag + "first step on the key, onset " + juce::String (on.empty() ? -1 : on[0]));
    }
}
