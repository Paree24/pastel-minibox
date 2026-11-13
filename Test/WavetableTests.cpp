#include "Tests.h"
#include "devices/wavetable/WavetableDevice.h"
#include "app/presets/PresetBank.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <iostream>

using namespace mb;

namespace
{
struct Out { std::vector<float> l, r; };

Out play (WavetableDevice& d, std::vector<int> notes, double seconds, double hold = 1.0e9, double sr = 48000.0)
{
    const int block = 256, total = (int) (seconds * sr);
    d.prepare (sr, block);
    Out o;
    o.l.assign ((size_t) total, 0.0f); o.r.assign ((size_t) total, 0.0f);
    TransportInfo t; t.bpm = 120.0; t.playing = true;
    for (int pos = 0; pos < total; pos += block)
    {
        if (pos == 0) for (int nn : notes) { NoteEvent e; e.note = nn; e.velocity = 1.0f; d.handleEvent (e); }
        if ((double) pos <= hold * sr && (double) (pos + block) > hold * sr)
            for (int nn : notes) { NoteEvent e; e.type = NoteEvent::NoteOff; e.note = nn; d.handleEvent (e); }
        t.beatPos = pos / sr * 2.0;
        d.setTransport (t);
        d.render (o.l.data() + pos, o.r.data() + pos, std::min (block, total - pos));
    }
    return o;
}

/** A plain voice: one table at a position, nothing else (no sub, filter open, no envelope movement). */
void plain (WavetableDevice& d, const juce::String& table, float pos)
{
    d.resetToDefaults();
    d.set ("oscATable", (float) dsp::wt::Bank::names().indexOf (table));
    d.set ("oscAPos", pos);
    d.set ("oscBLevel", 0.0f); d.set ("subLevel", 0.0f); d.set ("cutoff", 20000.0f); d.set ("filterEnv", 0.0f);
    d.set ("reso", 0.0f); d.set ("filterMode", 1.0f); d.set ("monoBass", 0.0f); d.set ("velocity", 0.0f);
}

/** The magnitude spectrum (dB) of n samples from `from`, Hann-windowed. */
std::vector<float> spectrum (const std::vector<float>& x, int from, int order)
{
    const int n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) n * 2, 0.0f);
    for (int i = 0; i < n; ++i) buf[(size_t) i] = x[(size_t) (from + i)] * (0.5f - 0.5f * std::cos (6.2831853f * i / (n - 1)));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    std::vector<float> db ((size_t) n / 2);
    for (int i = 0; i < n / 2; ++i) db[(size_t) i] = 20.0f * std::log10 (buf[(size_t) i] + 1.0e-9f);
    return db;
}

double rms (const std::vector<float>& x, int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += x[(size_t) i] * x[(size_t) i]; return std::sqrt (s / (b - a)); }
} // namespace

/** The bank: 64 tables of 16 frames, every frame there (normalised, finite), Basic Shapes starting on a sine. */
MB_TEST (wavetable_bank)
{
    const auto& b = dsp::wt::Bank::get();
    ctx.check (b.count() == 64 && dsp::wt::Bank::names().size() == 64, "64 tables");
    juce::StringArray seen;
    for (auto& nme : dsp::wt::Bank::names()) { ctx.check (! seen.contains (nme), "a unique name: " + nme); seen.add (nme); }
    ctx.check (dsp::wt::Bank::names()[0] == "Basic Shapes", "Basic Shapes first");
    for (int t = 0; t < 64; ++t)
        for (int f = 0; f < dsp::wt::kFrames; ++f)
        {
            const float* fr = b.frame (t, 0, f);
            float pk = 0.0f; bool fin = true;
            for (int i = 0; i < dsp::wt::Bank::size (0); ++i) { pk = std::max (pk, std::abs (fr[i])); fin = fin && std::isfinite (fr[i]); }
            if (! fin || pk < 0.85f || pk > 0.95f)
                ctx.check (false, juce::String (dsp::wt::Bank::names()[t]) + " frame " + juce::String (f) + " peak " + juce::String (pk, 3));
        }
    // Basic Shapes' first frame: a sine (its second harmonic and up are far below)
    const float* s = b.frame (0, 0, 0);
    double h1 = 0, rest = 0;
    const int n = dsp::wt::Bank::size (0);
    for (int i = 0; i < n; ++i) { const double v = 0.9 * std::sin (6.283185307 * i / n); h1 += v * v; rest += (s[i] - v) * (s[i] - v); }
    ctx.check (rest < h1 * 1.0e-4, "Basic Shapes starts on a sine (" + juce::String (10 * std::log10 (rest / h1 + 1e-12), 1) + " dB off)");
}

/** A note plays at its pitch: A1 on the sine is 55 Hz. */
MB_TEST (wavetable_pitch)
{
    WavetableDevice d;
    plain (d, "Basic Shapes", 0.0f);
    const auto o = play (d, { 33 }, 1.0);
    int zc = 0;
    for (int i = 24001; i < 48000; ++i) if ((o.l[(size_t) i - 1] < 0) != (o.l[(size_t) i] < 0)) ++zc;
    const double hz = zc / 2.0 / 0.5;
    ctx.check (std::abs (hz - 55.0) < 1.0, "A1 plays 55 Hz (" + juce::String (hz, 1) + ")");
}

/** A bright table high up does not fold back: at C7 the saw's partials stay on its harmonics (what falls between
    them, aliasing, is far down). */
MB_TEST (wavetable_band_limited)
{
    for (const char* t : { "Saw Sweep", "Hard Sync", "Crushed Saw" })
    {
        WavetableDevice d;
        plain (d, t, 1.0f);
        const auto o = play (d, { 96 }, 1.0);
        const auto db = spectrum (o.l, 8000, 14);
        const double f0 = 440.0 * std::pow (2.0, (96 - 69) / 12.0), binHz = 48000.0 / 16384.0;
        float harm = -200.0f, other = -200.0f;
        for (int i = 4; i < (int) db.size(); ++i)
        {
            const double hz = i * binHz, k = hz / f0;
            const bool onHarmonic = std::abs (k - std::round (k)) * f0 < 40.0;
            (onHarmonic ? harm : other) = std::max (onHarmonic ? harm : other, db[(size_t) i]);
        }
        ctx.check (other < harm - 45.0f, juce::String (t) + ": aliasing " + juce::String (other - harm, 1) + " dB under the harmonics");
    }
}

/** A wide unison stack keeps a mono low end: below Mono Bass the two sides are the same. */
MB_TEST (wavetable_mono_bass)
{
    WavetableDevice d;
    plain (d, "Reese Saw", 0.5f);
    d.set ("uniVoices", 7.0f); d.set ("uniDetune", 0.6f); d.set ("uniWidth", 1.0f); d.set ("subLevel", 0.6f);
    auto lowSide = [&] (float monoHz)
    {
        d.set ("monoBass", monoHz);
        const auto o = play (d, { 28 }, 1.0);
        // the side (L − R) in the low end against the mid
        // the side and the mid through a steep low-pass at 60 Hz (four one-pole stages): the fundamental's band
        double a1[4] {}, a2[4] {}, s = 0, m = 0;
        const double c = 1.0 - std::exp (-6.2831853 * 60.0 / 48000.0);
        for (int i = 0; i < 48000; ++i)
        {
            double x1 = o.l[(size_t) i] - o.r[(size_t) i], x2 = o.l[(size_t) i] + o.r[(size_t) i];
            for (int k = 0; k < 4; ++k) { a1[k] += c * (x1 - a1[k]); x1 = a1[k]; a2[k] += c * (x2 - a2[k]); x2 = a2[k]; }
            if (i > 12000) { s += x1 * x1; m += x2 * x2; }
        }
        return 10.0 * std::log10 (s / (m + 1e-12) + 1e-12);
    };
    const double wide = lowSide (0.0f), mono = lowSide (150.0f);
    ctx.check (wide > -30.0, "without it the low end is wide (" + juce::String (wide, 1) + " dB side)");
    ctx.check (mono < -30.0 && mono < wide - 15.0, "with Mono Bass 150 Hz it is mono (" + juce::String (mono, 1) + " dB side)");
}

/** The matrix routes: an LFO on the cutoff opens and closes the sound; a slot with no source does nothing. */
MB_TEST (wavetable_matrix_routes)
{
    WavetableDevice d;
    plain (d, "Saw Sweep", 1.0f);
    d.set ("cutoff", 500.0f); d.set ("filterMode", 0.0f);
    d.set ("lfo1Dest", 0.0f); d.set ("lfo1Depth", 1.0f); d.set ("lfo1Sync", 1.0f); d.set ("lfo1Div", 4.0f);   // ¼ notes
    auto swing = [&]
    {
        const auto o = play (d, { 36 }, 2.0);
        double lo = 1e9, hi = 0;
        for (int b = 2; b < 16; ++b) { const double v = rms (o.l, b * 6000, b * 6000 + 2400); lo = std::min (lo, v); hi = std::max (hi, v); }
        return 20.0 * std::log10 (hi / (lo + 1e-9));
    };
    const double still = swing();
    d.set (WavetableDevice::slotParam (0, 0), (float) WavetableDevice::SrcLfo1);
    d.set (WavetableDevice::slotParam (0, 1), (float) WavetableDevice::DstCutoff);
    d.set (WavetableDevice::slotParam (0, 2), 0.8f);
    const double moving = swing();
    ctx.check (still < 1.5, "no route: a steady level (" + juce::String (still, 1) + " dB swing)");
    ctx.check (moving > 6.0, "LFO 1 → Cutoff: it wobbles (" + juce::String (moving, 1) + " dB swing)");
}

/** Every table, swept, played low and high with distortion: finite and under control. */
MB_TEST (wavetable_every_table_stable)
{
    WavetableDevice d;
    for (int t = 0; t < 64; ++t)
    {
        plain (d, dsp::wt::Bank::names()[t], 0.0f);
        d.set ("oscBLevel", 0.7f); d.set ("oscBTable", (float) ((t * 7) % 64)); d.set ("oscBMode", (float) (t % 4)); d.set ("oscBMod", 0.8f);
        d.set ("distDrive", 0.7f); d.set ("distFold", 0.5f); d.set ("distCrush", 0.3f); d.set ("uniVoices", 4.0f);
        d.set ("lfo1Dest", (float) WavetableDevice::DstPosA); d.set ("lfo1Depth", 1.0f);
        const auto o = play (d, { 24, 60, 100 }, 0.4);
        bool fin = true; float pk = 0.0f;
        for (size_t i = 0; i < o.l.size(); ++i) { fin = fin && std::isfinite (o.l[i]) && std::isfinite (o.r[i]); pk = std::max ({ pk, std::abs (o.l[i]), std::abs (o.r[i]) }); }
        if (! fin || pk > 1.5f || pk < 0.01f) ctx.check (false, dsp::wt::Bank::names()[t] + ": finite " + juce::String ((int) fin) + ", peak " + juce::String (pk, 3));
    }
    ctx.check (true, "every table");
}

/** Tool (MB_WT=1): the cost of the heaviest patch: six notes of four unison stacks, both oscillators, FM, distortion. */
MB_TEST (tool_wavetable_cpu)
{
    if (juce::SystemStats::getEnvironmentVariable ("MB_WT", {}).isEmpty()) return;
    WavetableDevice d;
    plain (d, "Growl", 0.5f);
    d.set ("voiceMode", 2.0f); d.set ("uniVoices", 4.0f); d.set ("oscBLevel", 0.6f); d.set ("oscBMode", 2.0f); d.set ("oscBMod", 0.5f);
    d.set ("distDrive", 0.5f); d.set ("lfo1Depth", 0.8f); d.set ("subLevel", 0.5f); d.set ("noiseLevel", 0.2f);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    play (d, { 36, 43, 48, 55, 60, 64 }, 10.0);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
    std::cout << "    6 notes × 4 stacks, 10 s of audio in " << juce::String (ms, 0) << " ms (" << juce::String (ms / 100.0, 2) << " % of one core)" << std::endl;
}

MB_TEST (tool_wt_probe)
{
    if (juce::SystemStats::getEnvironmentVariable ("MB_WT", {}).isEmpty()) return;
    for (int mode : { 0, 1 })
        for (float cut : { 500.0f, 20000.0f })
        {
            WavetableDevice d;
            plain (d, "Saw Sweep", 1.0f);
            d.set ("filterMode", (float) mode); d.set ("cutoff", cut);
            d.prepare (48000.0, 256);
            NoteEvent e; e.note = 36; d.handleEvent (e);
            std::vector<float> l (256), r (256);
            d.render (l.data(), r.data(), 256);
            float pk = 0; for (float x : l) pk = std::max (pk, std::abs (x));
            std::cout << "    active " << d.anyVoiceActive() << " pk " << pk << " level " << d.get ("level") << " lvlA " << d.get ("oscALevel") << std::endl;
            const auto o = play (d, { 36 }, 0.3);
            std::cout << "    mode " << mode << " cut " << cut << " rms " << rms (o.l, 0, 14400) << " get " << d.get ("filterMode") << std::endl;
        }
}

MB_TEST (tool_wt_alias)
{
    if (juce::SystemStats::getEnvironmentVariable ("MB_WT", {}).isEmpty()) return;
    WavetableDevice d;
    plain (d, "Saw Sweep", 1.0f);
    const auto o = play (d, { 96 }, 0.5);
    const auto db = spectrum (o.l, 8000, 14);
    const double f0 = 440.0 * std::pow (2.0, (96 - 69) / 12.0), binHz = 48000.0 / 16384.0;
    std::vector<std::pair<float, double>> pk;
    for (int i = 4; i + 1 < (int) db.size(); ++i) if (db[(size_t) i] > db[(size_t) i - 1] && db[(size_t) i] >= db[(size_t) i + 1]) pk.push_back ({ db[(size_t) i], i * binHz });
    std::sort (pk.rbegin(), pk.rend());
    for (int k = 0; k < 14 && k < (int) pk.size(); ++k) std::cout << "    " << juce::String (pk[(size_t) k].second, 1) << " Hz (" << juce::String (pk[(size_t) k].second / f0, 2) << " f0) " << juce::String (pk[(size_t) k].first, 1) << " dB" << std::endl;
}

/** Tool (MB_WT_OUT=<folder>): each factory preset holding a note for 4 s (bass presets on E1, the others on C3), as
    WAVs for listening and for comparing with reference sounds (MB_WT_ONLY=<name part> for some). */
MB_TEST (tool_wavetable_render)
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("MB_WT_OUT", {});
    if (dir.isEmpty()) return;
    const auto only = juce::SystemStats::getEnvironmentVariable ("MB_WT_ONLY", {});
    juce::File out (dir);
    out.createDirectory();
    int i = 0;
    for (auto& p : presets::list (DeviceType::Wavetable))
    {
        ++i;
        if (only.isNotEmpty() && ! p.name.containsIgnoreCase (only)) continue;
        WavetableDevice d;
        presets::apply (d, p, true);
        for (auto kv : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("MB_WT_SET", {}), ",", ""))
            d.set (kv.upToFirstOccurrenceOf ("=", false, false).trim(), kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
        const bool bass = juce::StringArray { "Sub", "Reese", "Growl", "Neuro", "Wobble", "FM Bass", "Pluck Bass", "Digital Bass" }.contains (p.family);
        const int note = juce::SystemStats::getEnvironmentVariable ("MB_WT_NOTE", "31").getIntValue();
        const auto o = play (d, { bass ? note : 48 }, 4.5, 4.0);
        juce::AudioBuffer<float> buf (2, (int) o.l.size());
        buf.copyFrom (0, 0, o.l.data(), (int) o.l.size()); buf.copyFrom (1, 0, o.r.data(), (int) o.r.size());
        auto f = out.getChildFile (juce::String (i).paddedLeft ('0', 3) + " " + p.name + ".wav");
        f.deleteFile();
        juce::WavAudioFormat fmt;
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (fmt.createWriterFor (new juce::FileOutputStream (f), 48000.0, 2, 24, {}, 0)))
            w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }
    std::cout << "    rendered to " << out.getFullPathName() << std::endl;
}

MB_TEST (tool_wt_width)
{
    if (juce::SystemStats::getEnvironmentVariable ("MB_WT", {}).isEmpty()) return;
    for (int mono : { 0, 120 })
    {
        WavetableDevice d;
        plain (d, "Reese Saw", 0.5f);
        d.set ("uniVoices", 5.0f); d.set ("uniWidth", 1.0f); d.set ("uniDetune", 0.4f); d.set ("monoBass", (float) mono);
        const auto o = play (d, { 28 }, 1.0);
        double s = 0, m = 0;
        for (size_t i = 12000; i < o.l.size(); ++i) { const double a = o.l[i], b = o.r[i]; s += (a - b) * (a - b); m += (a + b) * (a + b); }
        std::cout << "    mono " << mono << " side/mid " << 10 * std::log10 (s / m + 1e-30) << " dB" << std::endl;
    }
}

/** Squash brightens and densifies without touching the sub: the mids and top come up against the whole sound. */
MB_TEST (wavetable_squash_brightens)
{
    auto measure = [] (float sq)
    {
        WavetableDevice d;
        plain (d, "Saw Sweep", 0.6f);
        d.set ("subLevel", 0.3f); d.set ("subOct", 0.0f); d.set ("squash", sq);
        const auto o = play (d, { 31 }, 1.5);
        const auto db = spectrum (o.l, 24000, 14);
        double lo = 0, hi = 0;
        for (size_t i = 1; i < db.size(); ++i) { const double p = std::pow (10.0, db[i] / 10.0), hz = i * 48000.0 / 16384.0; (hz < 150 ? lo : hi) += p; }
        return 10.0 * std::log10 (hi / lo);
    };
    const double off = measure (0.0f), on = measure (1.0f);
    ctx.check (on > off + 2.5, "Squash lifts what is above the sub (" + juce::String (off, 1) + " -> " + juce::String (on, 1) + " dB against it)");
}

/** The noise holds with the note at its default Decay (it used to fade in 150 ms). */
MB_TEST (wavetable_noise_holds)
{
    WavetableDevice d;
    plain (d, "Basic Shapes", 0.0f);
    d.set ("oscALevel", 0.0f); d.set ("noiseLevel", 0.6f);
    const auto o = play (d, { 48 }, 2.0);
    const double early = rms (o.l, 4800, 14400), late = rms (o.l, 72000, 81600);
    ctx.check (early > 1e-3 && late > early * 0.8, "still there after 1.5 s (" + juce::String (20 * std::log10 (late / early), 1) + " dB)");
}

/** A synced S&H (or Smooth) LFO takes one new value a cycle while the host plays, not one a block: the bug was a hiss
    under every preset with a synced random LFO (the bank's slow drifts), whenever the transport ran. */
MB_TEST (wavetable_synced_random_lfo_steps_once_a_cycle)
{
    for (int shape : { 4, 5 })
    {
        WavetableDevice d;
        plain (d, "Basic Shapes", 0.5f);
        d.set ("lfo2Shape", (float) shape); d.set ("lfo2Sync", 1.0f); d.set ("lfo2Div", 4.0f);   // a quarter: 0.5 s at 120
        d.set ("lfo2Dest", 2.0f); d.set ("lfo2Depth", 0.5f);
        const auto o = play (d, { 48 }, 1.0);
        const auto db = spectrum (o.l, 9600, 13);   // 0.2 s to 0.37 s, inside one quarter
        double in = 0, all = 0;
        for (size_t i = 1; i < db.size(); ++i)
        {
            const double hz = i * 48000.0 / 8192.0, p = std::pow (10.0, db[i] / 10.0), f0 = 130.8128;
            all += p;
            if (hz > 50.0 && std::abs (hz / f0 - std::round (hz / f0)) * f0 > 25.0) in += p;
        }
        const double r = 10.0 * std::log10 (in / all);
        ctx.check (r < -20.0, juce::String (shape == 4 ? "S&H" : "Smooth") + ": energy between the harmonics " + juce::String (r, 1) + " dB");
    }
}

/** Volume acts after the distortion: an envelope there swells a sound that hard drive would otherwise hold flat. */
MB_TEST (wavetable_volume_swells_past_the_drive)
{
    auto swell = [] (float amt)
    {
        WavetableDevice d;
        plain (d, "Basic Shapes", 0.6f);
        d.set ("distType", 1.0f); d.set ("distDrive", 0.8f);
        d.set ("level", 0.1f);   // well under the output limiter
        d.set ("fltA", 1.2f); d.set ("fltS", 1.0f);
        d.set ("mod1Src", 4.0f); d.set ("mod1Dst", 24.0f); d.set ("mod1Amt", amt);   // Filter Env -> Volume
        const auto o = play (d, { 36 }, 2.0);
        return 20.0 * std::log10 (rms (o.l, 76800, 86400) / rms (o.l, 4800, 14400));   // 1.6 s against 0.1 s
    };
    const double flat = swell (0.0f), up = swell (0.5f);
    ctx.check (std::abs (flat) < 3.0 && up > flat + 6.0, "hard drive alone " + juce::String (flat, 1) + " dB, with Volume " + juce::String (up, 1) + " dB");
}
