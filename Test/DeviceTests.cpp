#include "Tests.h"
#include "devices/DeviceProcessor.h"
#include "devices/fm/FmDevice.h"
#include "app/presets/PresetBank.h"
#include <iostream>
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

/** Tool (MB_RES=1): the Drums kit resonator: the default kit's hits with the resonator's Mix at full against off. */
MB_TEST (tool_drum_resonator)
{
    if (juce::SystemStats::getEnvironmentVariable ("MB_RES", {}).isEmpty()) return;
    auto render = [] (float mix, float send)
    {
        auto d = createDevice (DeviceType::Drums);
        presets::loadDefault (*d, true);
        d->prepare (48000.0, 512);
        d->set ("resMix", mix);
        if (send >= 0.0f) for (int p = 1; p <= 8; ++p) d->set ("p" + juce::String (p) + "ResSend", send);
        std::vector<float> l (512), r (512), out;
        for (int b = 0; b < 200; ++b)
        {
            if (b % 25 == 0) { NoteEvent e; e.type = NoteEvent::NoteOn; e.note = (b / 25) % 2 ? 38 : 36; e.velocity = 1.0f; d->handleEvent (e); }
            std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
            d->render (l.data(), r.data(), 512);
            out.insert (out.end(), l.begin(), l.end());
        }
        return out;
    };
    const auto off = render (0.0f, -1.0f);
    for (auto [mix, send, what] : { std::tuple { 1.0f, -1.0f, "Mix full, sends as the kit has them" }, std::tuple { 1.0f, 0.5f, "Mix full, every part sending 50 %" } })
    {
        const auto on = render (mix, send);
        double d2 = 0, r2 = 0;
        for (size_t i = 0; i < off.size(); ++i) { const double dd = on[i] - off[i]; d2 += dd * dd; r2 += (double) off[i] * off[i]; }
        std::cout << "    " << what << ": difference " << juce::String (10 * std::log10 ((d2 + 1e-15) / (r2 + 1e-15)), 1) << " dB" << std::endl;
    }
}

// MB_KNOBS=1: for every part and engine, how much each engine knob changes the hit (difference between the knob at
// 25 % and 75 % of its range, dB relative to the hit): a knob near -100 dB does nothing on that engine
MB_TEST (tool_drum_engine_knobs)
{
    if (juce::SystemStats::getEnvironmentVariable ("MB_KNOBS", {}).isEmpty()) return;
    const int notes[8] = { 36, 38, 39, 45, 42, 46, 49, 37 };
    for (int part = 0; part < 8; ++part)
    {
        const juce::String p = "p" + juce::String (part + 1);
        auto probe = createDevice (DeviceType::Drums);
        const auto& L = probe->params();
        const auto& eng = L[L.indexOf (p + "Eng")];
        for (int e = 0; e < eng.choices.size(); ++e)
        {
            juce::String line = "    part " + juce::String (part + 1) + " " + eng.choices[e].paddedRight (' ', 8);
            for (auto k : { "Pitch", "Bend", "BendTime", "Decay", "Tone", "Click", "LayMix", "LayRatio" })
            {
                auto render = [&] (float at)
                {
                    auto d = createDevice (DeviceType::Drums);
                    d->prepare (48000.0, 512);
                    d->set (p + "Eng", (float) e);
                    d->setNorm (d->params().indexOf (p + k), at);
                    NoteEvent ev; ev.type = NoteEvent::NoteOn; ev.note = notes[part]; ev.velocity = 1.0f; d->handleEvent (ev);
                    std::vector<float> l (512), r (512), out;
                    for (int b = 0; b < 94; ++b)
                    {
                        std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
                        d->render (l.data(), r.data(), 512);
                        out.insert (out.end(), l.begin(), l.end());
                    }
                    return out;
                };
                const auto a = render (0.25f), b = render (0.75f);
                double d2 = 0, r2 = 0;
                for (size_t i = 0; i < a.size(); ++i) { const double dd = b[i] - a[i]; d2 += dd * dd; r2 += (double) a[i] * a[i]; }
                line << " " << juce::String (k) << " " << juce::String (10 * std::log10 ((d2 + 1e-15) / (r2 + 1e-15)), 0);
            }
            std::cout << line << std::endl;
        }
    }
}

// MB_KITS=1: every Drums kit's kick, snare and closed hat at velocity 0.8: peak, RMS over 300 ms and the low end
// (one-pole low-pass at 150 Hz) in dBFS, to compare the kits' levels
MB_TEST (tool_drum_kit_levels)
{
    if (juce::SystemStats::getEnvironmentVariable ("MB_KITS", {}).isEmpty()) return;
    const double sr = 48000.0;
    const auto only = juce::SystemStats::getEnvironmentVariable ("MB_KIT", {});
    for (auto& p : presets::list (DeviceType::Drums))
    {
        if (only.isNotEmpty() && ! juce::String (p.name).containsIgnoreCase (only)) continue;
        juce::String line; line << juce::String (p.name).paddedRight (' ', 22);
        for (int note : { 36, 38, 42 })
        {
            auto d = createDevice (DeviceType::Drums);
            presets::apply (*d, p, true);
            d->prepare (sr, 512);
            std::vector<float> l (512), r (512), out;
            for (int b = 0; b < 30; ++b)
            {
                if (b == 0) { NoteEvent e; e.type = NoteEvent::NoteOn; e.note = note; e.velocity = 0.8f; d->handleEvent (e); }
                std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
                d->render (l.data(), r.data(), 512);
                out.insert (out.end(), l.begin(), l.end());
            }
            double pk = 0, s2 = 0, lp = 0, lo2 = 0; const double a = std::exp (-2.0 * juce::MathConstants<double>::pi * 150.0 / sr);
            const size_t n = (size_t) (0.3 * sr);
            for (size_t i = 0; i < n; ++i) { pk = std::max (pk, (double) std::abs (out[i])); s2 += out[i] * out[i]; lp = a * lp + (1 - a) * out[i]; lo2 += lp * lp; }
            auto db = [] (double v) { return juce::String (20 * std::log10 (v + 1e-9), 1).paddedLeft (' ', 6); };
            juce::String extra;
            if (note == 36)
            {
                // above 150 Hz (what a phone speaker plays) and the pitch 40-120 ms in (zero crossings)
                double lp2 = 0, hi2 = 0; int zc = 0;
                for (size_t i = 0; i < n; ++i) { lp2 = a * lp2 + (1 - a) * out[i]; const double h = out[i] - lp2; hi2 += h * h; }
                const size_t z0 = (size_t) (0.04 * sr), z1 = (size_t) (0.12 * sr);
                for (size_t i = z0 + 1; i < z1; ++i) if ((out[i - 1] < 0) != (out[i] < 0)) ++zc;
                // the attack above 300 Hz (a phone speaker's range): first 50 ms through two one-pole high-passes
                const double b3 = std::exp (-2.0 * juce::MathConstants<double>::pi * 300.0 / sr);
                double l1 = 0, l2 = 0, at2 = 0; const size_t na = (size_t) (0.05 * sr);
                for (size_t i = 0; i < na; ++i) { l1 = b3 * l1 + (1 - b3) * out[i]; const double h1 = out[i] - l1; l2 = b3 * l2 + (1 - b3) * h1; const double h2 = h1 - l2; at2 += h2 * h2; }
                // the click: above 2 kHz in the first 10 ms; the sub's tail: below 150 Hz from 100 to 300 ms
                const double b2 = std::exp (-2.0 * juce::MathConstants<double>::pi * 2000.0 / sr);
                double c1 = 0, c2 = 0, ck = 0; const size_t nc = (size_t) (0.01 * sr);
                for (size_t i = 0; i < nc; ++i) { c1 = b2 * c1 + (1 - b2) * out[i]; const double h1 = out[i] - c1; c2 = b2 * c2 + (1 - b2) * h1; const double h2 = h1 - c2; ck += h2 * h2; }
                double sl = 0, st = 0; const size_t t0 = (size_t) (0.1 * sr);
                for (size_t i = 0; i < n; ++i) { sl = a * sl + (1 - a) * out[i]; if (i >= t0) st += sl * sl; }
                extra << db (std::sqrt (lo2 / n)) << " hi" << db (std::sqrt (hi2 / n)) << " atk300" << db (std::sqrt (at2 / na))
                      << " click" << db (std::sqrt (ck / nc)) << " subtail" << db (std::sqrt (st / (n - t0))) << "  " << juce::String (zc / 2.0 / 0.08, 0) << " Hz";
            }
            line << (note == 36 ? "  kick " : note == 38 ? "  snare " : "  hat ") << db (pk) << db (std::sqrt (s2 / n)) << extra;
        }
        std::cout << "    " << line << std::endl;
    }
}
