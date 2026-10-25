#include "Tests.h"
#include "devices/DeviceProcessor.h"
#include <juce_dsp/juce_dsp.h>

using namespace mb;

namespace
{
constexpr double kSr = 48000.0;

std::vector<float> play (DeviceProcessor& d, const std::vector<int>& notes, double seconds, float vel = 0.8f)
{
    d.prepare (kSr, 256);
    const int total = (int) (seconds * kSr);
    std::vector<float> l ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
    TransportInfo ti; ti.bpm = 120.0; ti.playing = true;
    for (int pos = 0; pos < total; pos += 256)
    {
        if (pos == 0) for (int n : notes) { NoteEvent e; e.note = n; e.velocity = vel; d.handleEvent (e); }
        d.setTransport (ti);
        d.render (l.data() + pos, r.data() + pos, std::min (256, total - pos));
    }
    for (size_t i = 0; i < l.size(); ++i) l[i] = 0.5f * (l[i] + r[i]);
    return l;
}

/** Energy outside ±2 % of the harmonics of f0, relative to the total, in dB (aliasing + noise). */
float inharmonicDb (const std::vector<float>& x, float f0)   // f0 refined below
{
    const int n = 1 << 15, start = (int) x.size() - n;
    juce::dsp::FFT fft (15);
    std::vector<float> buf ((size_t) n * 2, 0.0f);
    for (int i = 0; i < n; ++i) buf[(size_t) i] = x[(size_t) (start + i)] * (0.5f - 0.5f * std::cos (6.2831853f * i / n));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    // the voice's real pitch (VCO voices sit a few cents off): refine from the strongest bin near f0
    {
        const int b0 = (int) std::round (f0 * n / kSr);
        int best = b0;
        for (int b = std::max (1, b0 - 6); b <= b0 + 6; ++b) if (buf[(size_t) b] > buf[(size_t) best]) best = b;
        const double a = buf[(size_t) best - 1], m = buf[(size_t) best], c = buf[(size_t) best + 1];
        const double delta = 0.5 * (a - c) / (a - 2.0 * m + c + 1e-20);
        f0 = (float) ((best + delta) * kSr / n);
    }
    double harm = 0.0, other = 0.0;
    for (int b = 2; b < n / 2; ++b)
    {
        const double hz = b * kSr / n;
        if (hz < 30.0) continue;
        const double k = hz / f0, dist = std::abs (k - std::round (k)) * f0;
        const double e = (double) buf[(size_t) b] * buf[(size_t) b];
        (dist < std::max (0.02 * f0, 12.0) && std::round (k) >= 1.0 ? harm : other) += e;
    }
    return (float) (10.0 * std::log10 (other / (harm + other) + 1e-20));
}

float peakOf (const std::vector<float>& x) { float p = 0.0f; for (float v : x) p = std::max (p, std::abs (v)); return p; }
} // namespace

/** Not a check: prints aliasing, self-oscillation and chord peaks for the synths. */
MB_TEST (tool_synth_lab)
{
    struct Case { DeviceType t; const char* name; std::vector<std::pair<const char*, float>> set; };
    const Case cases[] = {
        { DeviceType::Mono, "Mono saw", { { "mixSaw", 1 }, { "cutoff", 18000 }, { "reso", 0 } } },
        { DeviceType::Poly, "Poly DCO saw", { { "engine", 0 }, { "dcoWave", 1 }, { "cutoff", 18000 }, { "reso", 0 }, { "hpf", 1 } } },
        { DeviceType::Poly, "Poly VA saw", { { "engine", 1 }, { "vaWave", 0 }, { "cutoff", 18000 }, { "reso", 0 }, { "filterMode", 1 } } },
        { DeviceType::Acid, "Acid saw", { { "oscWave", 0 }, { "cutoff", 4000 }, { "reso", 0 }, { "envMod", 0 } } },
        { DeviceType::Acid, "Acid square", { { "oscWave", 1 }, { "cutoff", 4000 }, { "reso", 0 }, { "envMod", 0 } } },
    };
    for (auto& c : cases)
    {
        for (int note : { 48, 72, 96 })
        {
            auto d = createDevice (c.t);
            for (auto& [id, v] : c.set) d->set (id, v);
            const auto x = play (*d, { note }, 1.2);
            std::printf ("    %-12s note %d  inharmonic %6.1f dB  peak %.2f\n", c.name, note, inharmonicDb (x, 440.0f * std::exp2 ((note - 69) / 12.0f)), peakOf (x));
        }
        auto d = createDevice (c.t);
        for (auto& [id, v] : c.set) d->set (id, v);
        d->set ("reso", 1.0f); d->set ("cutoff", c.t == DeviceType::Acid ? 800.0f : 1000.0f);
        const auto so = play (*d, { 60 }, 1.0);
        std::printf ("    %-12s full resonance at 1 kHz: peak %.2f\n", c.name, peakOf (so));
        auto d2 = createDevice (c.t);
        for (auto& [id, v] : c.set) d2->set (id, v);
        const auto ch = play (*d2, { 48, 52, 55, 59, 62, 67 }, 0.8, 1.0f);
        std::printf ("    %-12s 6-note chord at full velocity: peak %.2f\n", c.name, peakOf (ch));
    }
    ctx.check (true, "measured");
}
