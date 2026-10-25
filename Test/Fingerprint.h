#pragma once
#include "devices/DeviceProcessor.h"
#include <juce_dsp/juce_dsp.h>

/** Audio fingerprints of presets: used by the preset tests and the factory-bank selection tool. */
namespace mb::fingerprint
{
constexpr double kSr = 48000.0;
constexpr float kMinDistance = 0.6f;

struct Fingerprint
{
    std::vector<float> rms, centroid;
    std::array<float, 24> shape {};     // band levels (dB) relative to their mean, over the loud frames
    float peak = 0.0f, loudDb = -120.0f;
    bool finite = true;
    std::vector<Fingerprint> parts;     // drum kits: each sound on its own
};

inline Fingerprint measure (const std::vector<float>& l, const std::vector<float>& r);

/** Plays `events (pos, on, off)` through the device for `seconds` and measures the result. */
template <typename Events>
Fingerprint play (DeviceProcessor& d, double seconds, Events&& events)
{
    const int block = 256;
    d.prepare (kSr, block);
    const int total = (int) (seconds * kSr);
    std::vector<float> l ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
    TransportInfo ti; ti.bpm = 120.0; ti.playing = true;
    auto on = [&] (int note, float vel) { NoteEvent e; e.note = note; e.velocity = vel; d.handleEvent (e); };
    auto off = [&] (int note) { NoteEvent e; e.type = NoteEvent::NoteOff; e.note = note; d.handleEvent (e); };
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        events (pos, pos + block, on, off);
        ti.beatPos = pos / kSr * 2.0;
        d.setTransport (ti);
        d.render (l.data() + pos, r.data() + pos, n);
    }
    return measure (l, r);
}

/** Renders a preset: a held note and its release; a kit plays all its parts in a row (for loudness)
    and each part alone (for comparing kits sound by sound). */
inline Fingerprint render (DeviceProcessor& d, bool drum)
{
    auto at = [] (int pos, int end, double t) { const int s = (int) (t * kSr); return pos <= s && end > s; };
    if (! drum)
    {
        // a fifth; the Sampler plays it at its sources' root octave (C4), the synths an octave lower
        const int lo = d.type() == DeviceType::Sampler ? 60 : 48, hi = lo + 7;
        return play (d, 2.0, [&] (int pos, int end, auto& on, auto& off)
        {
            if (pos == 0) { on (lo, 0.8f); on (hi, 0.8f); }
            if (at (pos, end, 1.2)) { off (lo); off (hi); }   // long enough for slow attacks to arrive
        });
    }
    auto fp = play (d, 1.6, [&] (int pos, int end, auto& on, auto&)
    {
        for (int lane = 0; lane < 8; ++lane) if (at (pos, end, lane * 0.1)) on (d.laneNote (lane), 0.8f);
    });
    for (int lane = 0; lane < 8; ++lane)
        fp.parts.push_back (play (d, 0.8, [&] (int pos, int, auto& on, auto&) { if (pos == 0) on (d.laneNote (lane), 0.8f); }));
    return fp;
}

inline Fingerprint measure (const std::vector<float>& l, const std::vector<float>& r)
{
    const int total = (int) l.size();
    Fingerprint fp;
    juce::dsp::FFT fft (11);
    std::vector<float> buf (4096);
    const int hop = (int) (0.05 * kSr);
    std::vector<std::array<float, 24>> bands;
    for (int f = 0; f + 2048 <= total; f += hop)
    {
        double sq = 0.0;
        for (int i = 0; i < 2048; ++i)
        {
            const float m = 0.5f * (l[(size_t) (f + i)] + r[(size_t) (f + i)]);
            if (! std::isfinite (m)) fp.finite = false;
            fp.peak = std::max (fp.peak, std::abs (m));
            sq += m * m;
            buf[(size_t) i] = m * (0.5f - 0.5f * std::cos (dsp::kTwoPi * i / 2048.0f));
        }
        std::fill (buf.begin() + 2048, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double num = 0.0, den = 1.0e-9;
        for (int b = 1; b < 1024; ++b) { num += b * buf[(size_t) b]; den += buf[(size_t) b]; }
        const double hz = num / den * kSr / 2048.0;
        const float rmsDb = (float) (10.0 * std::log10 (sq / 2048.0 + 1.0e-12));
        fp.rms.push_back (rmsDb);
        fp.centroid.push_back ((float) std::log2 (std::max (40.0, hz)));
        std::array<float, 24> b {};
        for (int k = 0; k < 24; ++k)
        {
            const double lo = 50.0 * std::pow (2.0, k * 8.3 / 24.0), hi = 50.0 * std::pow (2.0, (k + 1) * 8.3 / 24.0);
            double e = 1.0e-12;
            for (int bin = std::max (1, (int) (lo / kSr * 2048.0)); bin <= std::min (1023, (int) (hi / kSr * 2048.0)); ++bin)
                e += buf[(size_t) bin] * buf[(size_t) bin];
            b[(size_t) k] = (float) (10.0 * std::log10 (e));
        }
        bands.push_back (b);
    }
    // short-term loudness: the loudest 0.4 s window
    for (size_t i = 0; i + 8 <= fp.rms.size(); ++i)
    {
        double s = 0.0;
        for (size_t k = i; k < i + 8; ++k) s += std::pow (10.0, fp.rms[k] / 10.0);
        fp.loudDb = std::max (fp.loudDb, (float) (10.0 * std::log10 (s / 8.0 + 1.0e-12)));
    }
    // spectral shape over the frames within 9 dB of the loudest one
    const float top = *std::max_element (fp.rms.begin(), fp.rms.end());
    int used = 0;
    for (size_t i = 0; i < bands.size(); ++i)
        if (fp.rms[i] > top - 9.0f) { for (int k = 0; k < 24; ++k) fp.shape[(size_t) k] += bands[i][(size_t) k]; ++used; }
    float mean = 0.0f;
    for (auto& v : fp.shape) { v /= (float) std::max (1, used); mean += v / 24.0f; }
    for (auto& v : fp.shape) v = std::max (v - mean, -40.0f);
    return fp;
}

/** Mean difference of the normalised envelope (dB/12) and centroid (octaves) trajectories. */
inline float fpDistance (const Fingerprint& a, const Fingerprint& b)
{
    if (! a.parts.empty() && a.parts.size() == b.parts.size())
    {
        float sum = 0.0f;   // kits: the mean difference of their sounds, part by part
        for (size_t i = 0; i < a.parts.size(); ++i) sum += fpDistance (a.parts[i], b.parts[i]);
        return sum / (float) a.parts.size();
    }
    const size_t n = std::min (a.rms.size(), b.rms.size());
    const float ma = *std::max_element (a.rms.begin(), a.rms.end()), mbx = *std::max_element (b.rms.begin(), b.rms.end());
    double d = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const float ea = std::max (-60.0f, a.rms[i] - ma), eb = std::max (-60.0f, b.rms[i] - mbx);
        d += std::abs (ea - eb) / 12.0;
        const float w = std::min (1.0f, std::exp ((std::max (ea, eb)) / 12.0f));   // centroid matters where it is audible
        d += w * std::abs (a.centroid[i] - b.centroid[i]);
    }
    d /= (double) n;
    double s = 0.0;
    for (int k = 0; k < 24; ++k) s += std::abs (a.shape[(size_t) k] - b.shape[(size_t) k]);
    return (float) (d + s / 24.0 / 6.0);   // 6 dB of average spectral-shape difference counts as 1
}

} // namespace mb::fingerprint
