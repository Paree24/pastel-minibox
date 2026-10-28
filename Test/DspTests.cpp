#include "Tests.h"
#include "dsp/Filters.h"
#include "dsp/Envelopes.h"
#include "dsp/Oscillators.h"
#include "engine/Engine.h"
#include "model/Serializer.h"
#include "app/presets/PresetBank.h"
#include "devices/SampleSlot.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace mb;

namespace
{
/** Steady-state gain of a filter at `hz` (sine in, RMS ratio after settling). */
template <typename F>
float gainAt (F&& tick, float hz, float sr = 48000.0f)
{
    double in = 0.0, out = 0.0;
    const int n = (int) sr;
    for (int i = 0; i < n; ++i)
    {
        const float x = std::sin (dsp::kTwoPi * hz * (float) i / sr);
        const float y = tick (x);
        if (i > n / 2) { in += x * x; out += y * y; }
    }
    return (float) std::sqrt (out / in);
}
} // namespace

MB_TEST (dsp_svf_cutoff_accuracy)
{
    // a Butterworth-Q state-variable low-pass is 3 dB down at its cutoff
    for (float fc : { 200.0f, 1000.0f, 5000.0f })
    {
        float lo = fc * 0.5f, hi = fc * 2.0f;
        for (int it = 0; it < 30; ++it)
        {
            const float mid = std::sqrt (lo * hi);
            dsp::SvfTpt s;   // coefficients for Q = 0.707
            const float g = std::tan (dsp::kPi * fc / 48000.0f), k = 1.41421f;
            s.k = k; s.a1 = 1.0f / (1.0f + g * (g + k)); s.a2 = g * s.a1; s.a3 = g * s.a2;
            const float gain = gainAt ([&s] (float x) { return s.process (x, 0); }, mid);
            if (gain > 0.7071f) lo = mid; else hi = mid;
        }
        const float measured = std::sqrt (lo * hi);
        ctx.check (std::abs (measured / fc - 1.0f) < 0.05f, "SVF -3 dB at " + juce::String (fc) + " Hz measured " + juce::String (measured, 1));
    }
}
MB_TEST (dsp_envelope_timing)
{
    for (float atk : { 0.005f, 0.05f, 0.3f })
    {
        dsp::Adsr e; e.prepare (48000.0f); e.set (atk, 0.5f, 0.5f, 0.2f); e.gateOn();
        int n = 0;
        while (e.process() < 0.999f && n < 480000) ++n;
        const float ms = n / 48.0f;
        ctx.check (std::abs (ms - atk * 1000.0f) <= 1.0f + atk * 1000.0f * 0.02f, "attack " + juce::String (atk * 1000) + " ms took " + juce::String (ms, 2) + " ms");
    }
    dsp::Adsr e; e.prepare (48000.0f); e.set (0.001f, 0.1f, 0.0f, 0.05f); e.gateOn();
    for (int i = 0; i < 4800; ++i) e.process();
    e.gateOff();
    int n = 0;
    while (e.active() && n < 480000) { e.process(); ++n; }
    ctx.check (! e.active(), "release always reaches idle");
}

MB_TEST (dsp_oscillator_tuning)
{
    for (float hz : { 55.0f, 440.0f, 3520.0f })
    {
        dsp::PolyBlepOsc o; o.setFreq (hz, 48000.0f);
        int wraps = 0;
        for (int i = 0; i < 48000; ++i) { o.advance(); wraps += o.wrapped ? 1 : 0; }
        ctx.check (std::abs (wraps - hz) <= 1.0f, juce::String (hz) + " Hz oscillator gives " + juce::String (wraps) + " cycles");
    }
}

/** Offline-renders a fixed demo and compares it with the golden file (created on the first run). */
MB_TEST (regression_render_golden)
{
    Engine e;
    e.prepare (48000.0, 512);
    forceSyncSampleLoads() = true;
    const int picks[4] = { 0, 0, 0, 16 };   // Boom Bap kit, first acid line, first pad, first bell
    const DeviceType types[4] = { DeviceType::Drums, DeviceType::Acid, DeviceType::Poly, DeviceType::FM };
    for (int t = 0; t < 4; ++t)
    {
        auto d = createDevice (types[t]);
        presets::apply (*d, presets::factory (types[t])[(size_t) picks[t]], true);
        e.setDevice (0, t, std::move (d));
    }
    forceSyncSampleLoads() = false;
    auto& p = e.project();
    for (int s = 0; s < 16; ++s)
    {
        p.patterns[0].slots[0].track.lanes[(size_t) (s % 3 == 0 ? 0 : 4)][(size_t) s].on = true;
        p.patterns[0].slots[1].track.steps[(size_t) s].addNote (36 + (s * 5) % 12);
        if (s % 8 == 0) for (int n : { 48, 52, 55 }) p.patterns[0].slots[2].track.steps[(size_t) s].addNote (n);
        if (s % 4 == 2) p.patterns[0].slots[3].track.steps[(size_t) s].addNote (72);
    }
    p.master.reverbReturn = 0.0f;   // the reverb is the only part with random modulation-free but long state
    auto out = juce::File::createTempFile (".wav");
    auto r = serializer::exportWav (e, out, 1);
    ctx.check (r.ok, "rendered");
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    auto golden = juce::File (MINIBOX_SOURCE_DIR).getChildFile ("Test/golden/demo_render.wav");
    if (! golden.existsAsFile())
    {
        golden.getParentDirectory().createDirectory();
        out.copyFileTo (golden);
        std::printf ("    golden file created: %s\n", golden.getFullPathName().toRawUTF8());
        out.deleteFile();
        return;
    }
    std::unique_ptr<juce::AudioFormatReader> a (fm.createReaderFor (out)), b (fm.createReaderFor (golden));
    const int n = (int) std::min (a->lengthInSamples, b->lengthInSamples);
    juce::AudioBuffer<float> ba (2, n), bb (2, n);
    a->read (&ba, 0, n, 0, true, true); b->read (&bb, 0, n, 0, true, true);
    double diff = 0.0, ref = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) { const double d = ba.getSample (ch, i) - bb.getSample (ch, i); diff += d * d; ref += bb.getSample (ch, i) * bb.getSample (ch, i); }
    const double db = 10.0 * std::log10 ((diff + 1e-20) / (ref + 1e-20));
    ctx.check (db < -60.0, "render matches the golden file (difference " + juce::String (db, 1) + " dB)");
    out.deleteFile();
}
