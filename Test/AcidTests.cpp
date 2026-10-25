#include "Tests.h"
#include "devices/acid/AcidDevice.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>

using namespace mb;

namespace
{
std::vector<float> renderLine (AcidDevice& d, int notes, bool accents, double sr = 48000.0)
{
    d.prepare (sr, 256);
    const int step = (int) (sr * 0.125);
    std::vector<float> out ((size_t) (step * notes + sr), 0.0f), r (out.size());
    for (int pos = 0; pos < (int) out.size(); pos += 256)
    {
        const int s = pos / step;
        if (pos % step < 256 && s < notes)
        {
            NoteEvent on; on.note = 36 + (s % 3) * 5; on.velocity = 0.8f; on.flags = accents && s % 2 == 0 ? kAccent : 0;
            d.handleEvent (on);
        }
        if ((pos + step / 2) % step < 256 && s < notes) { NoteEvent off; off.type = NoteEvent::NoteOff; off.note = 36 + (s % 3) * 5; d.handleEvent (off); }
        d.render (out.data() + pos, r.data() + pos, std::min (256, (int) out.size() - pos));
    }
    return out;
}

float bandEnergy (const std::vector<float>& x, float lo, float hi, double sr = 48000.0)
{
    juce::dsp::FFT fft (14);
    std::vector<float> buf (32768, 0.0f);
    for (int i = 0; i < 16384 && i < (int) x.size(); ++i) buf[(size_t) i] = x[(size_t) i] * (0.5f - 0.5f * std::cos (dsp::kTwoPi * i / 16384.0f));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    double e = 0.0;
    for (int b = (int) (lo / sr * 16384); b <= (int) (hi / sr * 16384); ++b) e += buf[(size_t) b] * buf[(size_t) b];
    return (float) e;
}
} // namespace

MB_TEST (acid_303_behaviour)
{
    AcidDevice d;
    d.set ("envMod", 0.6f); d.set ("decay", 0.5f);
    for (float res : { 0.0f, 0.5f, 1.0f })
    {
        d.set ("reso", res);
        auto y = renderLine (d, 16, true);
        float peak = 0.0f; bool finite = true;
        for (float v : y) { peak = std::max (peak, std::abs (v)); finite &= std::isfinite (v); }
        std::printf ("    reso %.1f peak %.2f\n", res, peak);
        ctx.check (finite && peak < 4.0f && peak > 0.02f, "stable at reso " + juce::String (res) + ", peak " + juce::String (peak, 2));
    }
    // the feedback high-pass keeps resonance from swallowing the bass
    d.set ("envMod", 0.0f); d.set ("cutoff", 500.0f);
    d.set ("reso", 0.0f); const float lowDry = bandEnergy (renderLine (d, 8, false), 40, 120);
    d.set ("reso", 0.9f); const float lowRes = bandEnergy (renderLine (d, 8, false), 40, 120);
    std::printf ("    bass 40-120 Hz: reso 0 %.3g, reso 0.9 %.3g (ratio %.2f)\n", lowDry, lowRes, lowRes / lowDry);
    ctx.check (lowRes > lowDry * 0.05f, "resonance does not kill the bass entirely");
    // env mod opens the filter: more energy above 2 kHz at the start of a note
    d.set ("reso", 0.5f); d.set ("cutoff", 400.0f);
    d.set ("envMod", 0.0f); const float closed = bandEnergy (renderLine (d, 1, false), 1500, 6000);
    d.set ("envMod", 1.0f); const float open = bandEnergy (renderLine (d, 1, false), 1500, 6000);
    ctx.check (open > closed * 4.0f, "env mod sweeps the filter (" + juce::String (open / closed, 1) + "x)");
    // accent is louder and brighter
    d.set ("envMod", 0.5f); d.set ("accAmt", 1.0f);
    auto plain = renderLine (d, 1, false), acc = renderLine (d, 1, true);
    float pp = 0, pa = 0; for (size_t i = 0; i < 12000; ++i) { pp = std::max (pp, std::abs (plain[i])); pa = std::max (pa, std::abs (acc[i])); }
    ctx.check (pa > pp * 1.3f, "accent is louder (" + juce::String (pa / pp, 2) + "x)");
}

/** Writes a short acid line to build/acid_demo.wav for listening (tool). */
MB_TEST (tool_acid_demo)
{
    AcidDevice d;
    d.set ("cutoff", 600.0f); d.set ("reso", 0.85f); d.set ("envMod", 0.65f); d.set ("decay", 0.4f); d.set ("accAmt", 0.8f);
    auto y = renderLine (d, 32, true);
    juce::AudioBuffer<float> b (1, (int) y.size());
    b.copyFrom (0, 0, y.data(), (int) y.size());
    auto f = juce::File (MINIBOX_SOURCE_DIR).getChildFile ("build/acid_demo.wav");
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (f.createOutputStream().release(), 48000.0, 1, 24, {}, 0));
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    ctx.check (true, "written");
}

/** Keyswitches are momentary: holding C♯0 accents, holding D0 makes overlapping notes glide without
    re-triggering, releasing returns to the chip's setting; the keyswitch notes never sound. */
MB_TEST (acid_keyswitch_articulations)
{
    AcidDevice d;
    d.prepare (48000.0, 256);
    std::vector<float> l (256), r (256);
    auto play = [&] (int note, bool on) { NoteEvent e; e.type = on ? NoteEvent::NoteOn : NoteEvent::NoteOff; e.note = note; e.velocity = 0.8f; d.handleEvent (e); };
    auto block = [&] { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); d.render (l.data(), r.data(), 256); float p = 0; for (float v : l) p = std::max (p, std::abs (v)); return p; };
    play (13, true);
    ctx.check ((int) d.get ("artic") == 1, "holding C#0 accents");
    float quiet = 0; for (int i = 0; i < 4; ++i) quiet = std::max (quiet, block());
    ctx.check (quiet < 1.0e-4f, "keyswitches make no sound");
    play (14, true);
    ctx.check ((int) d.get ("artic") == 3, "C#0 + D0 held: both");
    play (13, false);
    ctx.check ((int) d.get ("artic") == 2, "D0 still held: slide");
    play (36, true); for (int i = 0; i < 20; ++i) block();
    const float before = block();
    play (43, true); play (36, false);   // legato while D0 is held: a glide, the envelopes keep going
    const float after = block();
    ctx.check (after < before * 1.5f, "slide does not re-trigger (" + juce::String (after / std::max (1e-6f, before), 2) + "x)");
    play (43, false);
    play (14, false);
    ctx.check ((int) d.get ("artic") == 0, "released: back to normal");
    d.set ("artic", 1.0f);   // a lasting setting from the chip survives a keyswitch press
    play (14, true); play (14, false);
    ctx.check ((int) d.get ("artic") == 1, "chip setting returns after the keyswitch");
}

/** The three pedals: bounded, add harmonics, and differ from each other. */
MB_TEST (acid_drive_pedals)
{
    std::vector<std::vector<float>> outs;
    for (int type = 0; type < 3; ++type)
    {
        AcidDevice d;
        d.set ("cutoff", 700.0f); d.set ("reso", 0.6f); d.set ("envMod", 0.4f);
        d.set ("driveType", (float) type); d.set ("driveAmt", 0.8f);
        auto y = renderLine (d, 8, true);
        float peak = 0; bool finite = true;
        for (float v : y) { peak = std::max (peak, std::abs (v)); finite &= std::isfinite (v); }
        ctx.check (finite && peak < 1.0f && peak > 0.05f, "pedal " + juce::String (type) + " bounded, peak " + juce::String (peak, 2));
        outs.push_back (y);
    }
    AcidDevice clean;
    clean.set ("cutoff", 700.0f); clean.set ("reso", 0.6f); clean.set ("envMod", 0.4f);
    const auto c = renderLine (clean, 8, true);
    const float cleanHi = bandEnergy (c, 2000, 8000) / std::max (1.0f, bandEnergy (c, 40, 8000));
    for (int t = 0; t < 3; ++t)
    {
        const float hi = bandEnergy (outs[(size_t) t], 2000, 8000) / std::max (1.0f, bandEnergy (outs[(size_t) t], 40, 8000));
        std::printf ("    pedal %d: upper-band share %.4f (clean %.4f)\n", t, hi, cleanHi);
        ctx.check (hi > cleanHi * 1.5f, "pedal " + juce::String (t) + " adds harmonics");
    }
    const float od = bandEnergy (outs[0], 700, 1500), fz = bandEnergy (outs[2], 700, 1500);
    const float odAll = bandEnergy (outs[0], 40, 8000), fzAll = bandEnergy (outs[2], 40, 8000);
    ctx.check (od / odAll > fz / fzAll, "the overdrive pushes the mids, the fuzz scoops them");
}

/** The Reverb card: Mix adds a tail that outlasts the note, Size lengthens it, and pulling Mix to zero
    fades the tail out instead of cutting it. */
MB_TEST (device_reverb_card)
{
    auto tail = [] (float mix, float size)
    {
        AcidDevice d;
        d.set ("cutoff", 900.0f); d.set ("reverb", mix); d.set ("revSize", size);
        auto y = renderLine (d, 1, false);
        double e = 0; for (size_t i = 12000; i < y.size(); ++i) e += (double) y[i] * y[i];   // after the note
        return e;
    };
    const double dry = tail (0.0f, 0.5f), small = tail (0.5f, 0.1f), big = tail (0.5f, 0.9f);
    std::printf ("    tail energy: dry %.3g, small %.3g, big %.3g\n", dry, small, big);
    ctx.check (small > dry * 10.0, "Mix adds a tail");
    ctx.check (big > small * 1.5, "Size lengthens it");
    AcidDevice d;
    d.prepare (48000.0, 256);
    d.set ("reverb", 0.6f);
    std::vector<float> l (256), r (256), all;
    NoteEvent on; on.note = 45; on.velocity = 0.9f; d.handleEvent (on);
    for (int b = 0; b < 60; ++b)
    {
        if (b == 20) { NoteEvent off; off.type = NoteEvent::NoteOff; off.note = 45; d.handleEvent (off); }
        if (b == 30) d.set ("reverb", 0.0f);
        std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
        d.render (l.data(), r.data(), 256);
        all.insert (all.end(), l.begin(), l.end());
    }
    float maxStep = 0; for (size_t i = 30 * 256 - 64; i < 30 * 256 + 2048; ++i) maxStep = std::max (maxStep, std::abs (all[i] - all[i - 1]));
    ctx.check (maxStep < 0.05f, "Mix to zero fades, no step (" + juce::String (maxStep, 4) + ")");
}

/** A pedal after the VCA must not flatten the line: a decaying note keeps its decay through the fuzz. */
MB_TEST (acid_pedals_keep_the_envelope)
{
    auto decayDb = [] (int type, float amt)
    {
        AcidDevice d;
        d.set ("ampMode", 1.0f); d.set ("ampDecay", 0.25f); d.set ("cutoff", 700.0f); d.set ("reso", 0.5f);
        d.set ("driveType", (float) type); d.set ("driveAmt", amt);
        d.prepare (48000.0, 256);
        NoteEvent on; on.note = 40; on.velocity = 0.8f; d.handleEvent (on);
        std::vector<float> l (256), r (256);
        float early = 0, late = 0;
        for (int b = 0; b < 160; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
            d.render (l.data(), r.data(), 256);
            float p = 0; for (float v : l) p = std::max (p, std::abs (v));
            if (b >= 2 && b < 10) early = std::max (early, p);
            if (b >= 140 && b < 150) late = std::max (late, p);   // ≈ 0.75 s in
        }
        return dsp::gainToDb (late / std::max (1e-6f, early));
    };
    const float clean = decayDb (0, 0.0f);
    for (int t = 0; t < 3; ++t)
    {
        const float driven = decayDb (t, 1.0f);
        std::printf ("    pedal %d: decay %.1f dB (clean %.1f dB)\n", t, driven, clean);
        ctx.check (driven < clean + 10.0f, "pedal " + juce::String (t) + " keeps the decay");
    }
}

/** In the plugin, the host's Reverb Return scales the instrument's own Reverb card. */
MB_TEST (plugin_reverb_return_scales_the_card)
{
    auto tail = [] (float scale)
    {
        AcidDevice d;
        d.set ("cutoff", 900.0f); d.set ("reverb", 0.5f);
        d.reverbScale = scale;
        auto y = renderLine (d, 1, false);
        double e = 0; for (size_t i = 12000; i < y.size(); ++i) e += (double) y[i] * y[i];
        return e;
    };
    const double off = tail (0.0f), def = tail (1.0f);
    ctx.check (off < def * 0.01, "Reverb Return at zero silences the card's tail");
}
