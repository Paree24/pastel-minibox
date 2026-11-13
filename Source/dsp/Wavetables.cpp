#include "Wavetables.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <functional>

namespace mb::dsp::wt
{
namespace
{
constexpr double kPi = 3.14159265358979323846, k2Pi = 2.0 * kPi;
constexpr int kGenOrder = 12, kGenN = 1 << kGenOrder;   // a time-domain frame is sampled at 4096 points

double frac (double x) { return x - std::floor (x); }
double sine (double x) { return std::sin (k2Pi * x); }
double saw (double x) { return 2.0 * frac (x) - 1.0; }
double sq (double x, double pw = 0.5) { return frac (x) < pw ? 1.0 : -1.0; }
double tri (double x) { return 1.0 - 4.0 * std::abs (frac (x + 0.25) - 0.5); }
double lerp (double a, double b, double t) { return a + (b - a) * t; }
double gauss (double x, double c, double w) { return std::exp (-(x - c) * (x - c) / (2.0 * w * w)); }
uint32_t hash (uint32_t a) { a ^= a >> 16; a *= 0x7feb352dU; a ^= a >> 15; a *= 0x846ca68bU; a ^= a >> 16; return a; }
double rnd (uint32_t a) { return (double) (hash (a) & 0xffffff) / 16777216.0; }   // 0 … 1

/** A table is drawn either in time (a value for a phase x at a position t) or as a spectrum (amplitude and phase of
    harmonic h at a position t, a sine series). */
struct Gen
{
    TableInfo info;
    std::function<double (double t, double x)> time;
    std::function<std::pair<double, double> (int h, double t)> spec;
};

Gen T (const char* name, const char* fam, std::function<double (double, double)> f) { return { { name, fam }, std::move (f), {} }; }
Gen S (const char* name, const char* fam, std::function<std::pair<double, double> (int, double)> f) { return { { name, fam }, {}, std::move (f) }; }
std::pair<double, double> A (double amp, double ph = 0.0) { return { amp, ph }; }

// formant vowels (2026-11-13, second pass): real vowel formants (Hz) as harmonic numbers of a 55 Hz note, so a bass
// note's vowels sit where a voice's do (a table is read at any pitch, so they move with the note, as in any vocal table)
struct Vowel { double f1, f2, f3; };
constexpr double kRef = 55.0;
const Vowel kA { 730 / kRef, 1090 / kRef, 2440 / kRef }, kE { 530 / kRef, 1840 / kRef, 2480 / kRef },
            kI { 270 / kRef, 2290 / kRef, 3010 / kRef }, kO { 570 / kRef, 840 / kRef, 2410 / kRef },
            kU { 300 / kRef, 870 / kRef, 2240 / kRef };
/** A voiced vowel: a bright buzz (harmonics falling as h^-tilt) shaped by three formant resonances over a floor. */
double vowel (int h, const Vowel& v, double width = 1.0, double tilt = 0.75, double floor = 0.07)
{
    const double f = floor + gauss (h, v.f1, 1.5 * width) + 0.7 * gauss (h, v.f2, 2.2 * width) + 0.4 * gauss (h, v.f3, 3.0 * width);
    return std::pow ((double) h, -tilt) * f;
}
Vowel mix (const Vowel& a, const Vowel& b, double t) { return { lerp (a.f1, b.f1, t), lerp (a.f2, b.f2, t), lerp (a.f3, b.f3, t) }; }
Vowel path (std::initializer_list<Vowel> vs, double t)   // through several vowels in turn
{
    const std::vector<Vowel> v (vs);
    const double s = t * (double) (v.size() - 1);
    const int k = std::min ((int) v.size() - 2, (int) s);
    return mix (v[(size_t) k], v[(size_t) k + 1], s - k);
}

double fbFm (double x, double beta)   // y = sin (θ + β y), by fixed-point iteration (β < 1.6)
{
    double y = 0.0;
    for (int k = 0; k < 12; ++k) y = std::sin (k2Pi * x + beta * y);
    return y;
}

const std::vector<Gen>& gens()
{
    static const std::vector<Gen> g = {
        // ---- Basic
        T ("Basic Shapes", "Basic", [] (double t, double x)
        {
            const double s = t * 3.0;   // sine → triangle → saw → square
            const int k = std::min (2, (int) s);
            const double u = s - k;
            const double v[4] = { sine (x), tri (x), saw (x + 0.5), sq (x) };
            return lerp (v[k], v[k + 1], u);
        }),
        // ---- Analog
        T ("PWM", "Analog", [] (double t, double x) { const double pw = lerp (0.5, 0.04, t); return sq (x, pw) - (2.0 * pw - 1.0); }),
        S ("Saw Sweep", "Analog", [] (int h, double t) { return A (1.0 / h * std::exp (-h / (1.5 + t * t * 160.0))); }),
        S ("Square Sweep", "Analog", [] (int h, double t) { return A (h % 2 ? 1.0 / h * std::exp (-h / (1.5 + t * t * 160.0)) : 0.0); }),
        S ("Tri to Square", "Analog", [] (int h, double t) { return A (h % 2 ? 1.0 / std::pow (h, 2.0 - t) : 0.0, (h / 2) % 2 ? kPi * (1.0 - t) : 0.0); }),
        S ("Organ", "Analog", [] (int h, double t)
        {
            const double bars[9] = { 1.0, 0.8, lerp (0.0, 0.7, t), 0.6, 0.0, lerp (0.0, 0.5, t), 0.0, lerp (0.0, 0.45, t), 0.0 };
            return A (h <= 9 ? bars[h - 1] : (h == 16 ? 0.35 * t : 0.0));
        }),
        S ("Sine Harmonics", "Analog", [] (int h, double t) { const double top = 1.0 + t * 31.0; return A (h <= top + 1 ? 1.0 / h * std::min (1.0, top + 1.0 - h) : 0.0); }),
        S ("Odd Harmonics", "Analog", [] (int h, double t) { const double top = 1.0 + t * 63.0; return A (h % 2 && h <= top + 1 ? 1.0 / h * std::min (1.0, top + 1.0 - h) : 0.0); }),
        S ("Even Harmonics", "Analog", [] (int h, double t) { return A (h == 1 ? 1.0 : (h % 2 == 0 ? 1.0 / h * std::exp (-h / (2.0 + t * 60.0)) * 2.0 : 0.0)); }),
        S ("Hollow", "Analog", [] (int h, double t) { return A (h % 2 ? (h == 1 ? 1.0 - 0.92 * t : 1.0 / h) : 0.0); }),
        // ---- Sub
        S ("Sub Sine", "Sub", [] (int h, double t) { return A (h == 1 ? 1.0 : h == 2 ? 0.32 * t : h == 3 ? 0.14 * t * t : 0.0); }),
        T ("Warm Sine", "Sub", [] (double t, double x) { const double g = 1.0 + 6.0 * t; return std::tanh (g * sine (x)) / std::tanh (g); }),
        T ("Sine Fold", "Sub", [] (double t, double x) { return std::sin (kPi * 0.5 * (1.0 + 7.0 * t) * sine (x)); }),
        T ("Round Pulse", "Sub", [] (double t, double x) { const double s = sine (x); return (s < 0 ? -1.0 : 1.0) * std::pow (std::abs (s), lerp (1.0, 0.08, t)); }),
        T ("Sine Sync", "Sub", [] (double t, double x) { const double r = 1.0 + 2.0 * t; return std::sin (k2Pi * r * frac (x)) * (1.0 - 0.3 * frac (x)); }),
        T ("Deep Tri", "Sub", [] (double t, double x) { const double v = tri (x); return std::tanh ((1.0 + 2.5 * t) * (v + 0.35 * t * v * v)) ; }),
        // ---- Reese
        T ("Reese Saw", "Reese", [] (double t, double x) { return 0.5 * (saw (x) + saw (x + 0.5 * t)); }),
        T ("Reese Square", "Reese", [] (double t, double x) { return 0.5 * (sq (x) + sq (x + 0.25 * t)); }),
        T ("Reese Phase", "Reese", [] (double t, double x) { return (saw (x) + saw (x + 0.37 * t) + saw (x + 0.81 * t)) / 3.0; }),
        S ("Dispersed Saw", "Reese", [] (int h, double t) { return A (1.0 / h, t * 0.012 * h * h * k2Pi); }),
        T ("Phase Saw", "Reese", [] (double t, double x)
        {
            const double d = lerp (0.5, 0.015, t), p = frac (x);   // phase distortion: the cycle bent at d
            const double w = p < d ? 0.5 * p / d : 0.5 + 0.5 * (p - d) / (1.0 - d);
            return -std::cos (k2Pi * w);
        }),
        T ("Rasp", "Reese", [] (double t, double x) { return saw (x) * (1.0 - t + t * sq (3.0 * x)); }),
        S ("Buzz", "Reese", [] (int h, double t) { return A (std::exp (-h / (3.0 + 120.0 * t * t)), 0.0); }),
        T ("Reese Hollow", "Reese", [] (double t, double x) { return (sq (x) + sq (x + 0.2 * t) + saw (x + 0.5 * t)) / 3.0; }),
        // ---- Growl (formants that move through the table)
        // growl: a gritty buzz (its harmonics dispersed) opening from "oo" through "oh" to "ah", the "rawr" of a growl
        S ("Growl", "Growl", [] (int h, double t) { return A (vowel (h, path ({ kU, kO, kA }, t), 1.2, 0.6, 0.12), h * h * 0.37); }),
        S ("Vowel A-O", "Growl", [] (int h, double t) { return A (vowel (h, mix (kA, kO, t))); }),
        S ("Vowel E-I", "Growl", [] (int h, double t) { return A (vowel (h, mix (kE, kI, t))); }),
        // yoy: "ee" to "oh" (an envelope there and back says "yoy")
        S ("Yoy", "Growl", [] (int h, double t) { return A (vowel (h, path ({ kI, kE, kO }, t), 1.1, 0.65)); }),
        S ("Talk", "Growl", [] (int h, double t)
        {
            const Vowel v[5] = { kA, kE, kI, kO, kU };
            const double s = t * 4.0;
            const int k = std::min (3, (int) s);
            return A (vowel (h, mix (v[k], v[k + 1], s - k)));
        }),
        S ("Foghorn", "Growl", [] (int h, double t)
        {
            const double src = (h % 2 ? 1.0 : 0.55) * std::pow (h, -0.55);
            return A (src * (0.12 + gauss (h, lerp (6.0, 12.0, t), 2.2) + 0.8 * gauss (h, lerp (13.0, 20.0, t), 2.8) + 0.3 * gauss (h, 44.0, 4.0)));
        }),
        S ("Horn", "Growl", [] (int h, double t) { return A (1.0 / std::pow (h, lerp (2.2, 0.9, t)) * (1.0 + gauss (h, 4.0 + 6.0 * t, 2.0))); }),
        S ("Throat", "Growl", [] (int h, double t) { return A (std::pow (h, -0.7) * (0.08 + gauss (h, 5.0 + 9.0 * t, 1.8) + 0.8 * gauss (h, 34.0 - 16.0 * t, 3.0)), h * h * 0.21); }),
        // ---- Neuro
        T ("Neuro Grind", "Neuro", [] (double t, double x) { const double v = std::sin (k2Pi * x + (0.5 + 5.0 * t) * sine (2.0 * x)); return std::sin (kPi * 0.5 * (1.0 + 2.0 * t) * v); }),
        T ("Neuro Bite", "Neuro", [] (double t, double x) { return std::sin (kPi * 0.5 * (1.0 + 5.0 * t) * saw (x + 0.5)); }),
        T ("Snarl", "Neuro", [] (double t, double x) { return std::sin (kPi * 0.5 * (1.0 + 3.0 * t) * tri (x) + 0.6 * t * sine (2.0 * x)); }),
        T ("Saw Fold", "Neuro", [] (double t, double x) { return std::sin (kPi * 0.5 * (1.0 + 9.0 * t) * saw (x + 0.5)) * (0.6 + 0.4 * std::abs (saw (x + 0.5))); }),
        T ("Square Fold", "Neuro", [] (double t, double x) { const double v = (1.0 + 7.0 * t) * tri (x); return 1.0 - 4.0 * std::abs (frac (v * 0.25 + 0.25) - 0.5); }),
        T ("Crushed Saw", "Neuro", [] (double t, double x) { const double lv = std::pow (2.0, lerp (7.0, 1.5, t)); return std::round (saw (x + 0.5) * lv) / lv; }),
        S ("Shred", "Neuro", [] (int h, double t)
        {
            const double gate = rnd ((uint32_t) h * 7919u + (uint32_t) (t * 15.0 + 0.5) * 104729u) < lerp (0.9, 0.35, t) ? 1.0 : 0.15;
            return A (gate / std::pow (h, 0.85), rnd ((uint32_t) h * 31u) * k2Pi);
        }),
        T ("Gnarl", "Neuro", [] (double t, double x) { return lerp (saw (x + 0.5), saw (x + 0.5) * sine (3.0 * x) * 1.6, t); }),
        // ---- Metal (sparse, bell-like partials)
        S ("Metal Bell", "Metal", [] (int h, double t)
        {
            static const int p[7] = { 1, 3, 5, 9, 13, 17, 21 };
            for (int k = 0; k < 7; ++k) if (h == p[k]) return A (k == 0 ? 1.0 : lerp (0.15, 0.75, t) / (1.0 + 0.25 * k) * (k % 2 ? 1.0 : t + 0.3));
            return A (0.0);
        }),
        S ("Clang", "Metal", [] (int h, double t)
        {
            static const int p[6] = { 1, 7, 11, 13, 19, 23 };
            for (int k = 0; k < 6; ++k) if (h == p[k]) return A (k == 0 ? 1.0 : 0.9 * t * std::exp (-0.2 * k) + 0.05);
            return A (0.0);
        }),
        T ("Ring Mod", "Metal", [] (double t, double x)
        {
            const double r = 2.0 + 7.0 * t;
            const int k = std::min (8, (int) r);
            const double u = r - k;
            return sine (x) * lerp (sine (k * x), sine ((k + 1) * x), u);
        }),
        S ("Tine", "Metal", [] (int h, double t) { return A (h == 1 ? 1.0 : h == 14 ? 0.7 * t : h == 7 ? 0.25 * t : h == 2 ? 0.2 : 0.0); }),
        S ("Gong", "Metal", [] (int h, double t) { return A (h == 1 ? 0.8 : (h >= 17 && h <= 47 ? (0.2 + 0.6 * t) * rnd ((uint32_t) h * 977u) * gauss (h, 30.0, 8.0) : 0.0)); }),
        S ("Chime", "Metal", [] (int h, double t) { const int r = (int) std::lround (std::sqrt ((double) h)); return A (r * r == h ? (h == 1 ? 1.0 : (0.25 + 0.6 * t) / std::sqrt ((double) r)) : 0.0); }),
        S ("Alloy", "Metal", [] (int h, double t)
        {
            static const int p[10] = { 1, 2, 3, 5, 7, 11, 13, 17, 19, 23 };
            for (int k = 0; k < 10; ++k) if (h == p[k]) return A (k < 2 ? 1.0 / (k + 1) : (k % 2 ? t : 1.0 - t) * 0.6 / std::sqrt ((double) k));
            return A (0.0);
        }),
        S ("Tin", "Metal", [] (int h, double t) { return A (h == 1 ? 1.0 : (h % 2 ? (0.5 / std::sqrt ((double) h)) * (0.5 + 0.5 * std::cos (h * (0.4 + 1.2 * t))) : 0.0)); }),
        // ---- FM / Sync
        T ("FM 1:1", "FM", [] (double t, double x) { return std::sin (k2Pi * x + 6.0 * t * sine (x)); }),
        T ("FM 1:2", "FM", [] (double t, double x) { return std::sin (k2Pi * x + 5.0 * t * sine (2.0 * x)); }),
        T ("FM 1:3", "FM", [] (double t, double x) { return std::sin (k2Pi * x + 4.0 * t * sine (3.0 * x)); }),
        T ("FM 2:1", "FM", [] (double t, double x) { return std::sin (k2Pi * 2.0 * x + 4.0 * t * sine (x)) * 0.7 + 0.3 * sine (x); }),
        T ("Feedback FM", "FM", [] (double t, double x) { return fbFm (x, 1.5 * t); }),
        T ("Hard Sync", "FM", [] (double t, double x) { return saw (frac (x) * (1.0 + 7.0 * t) + 0.5); }),
        T ("Sync Square", "FM", [] (double t, double x) { return sq (frac (x) * (1.0 + 7.0 * t)); }),
        T ("PD Reso", "FM", [] (double t, double x) { const double p = frac (x); return std::sin (k2Pi * (1.0 + 11.0 * t) * p) * (1.0 - p); }),
        // ---- Digital
        T ("Bitcrush", "Digital", [] (double t, double x) { const double lv = std::pow (2.0, lerp (5.0, 1.0, t)); return std::round (sine (x) * lv) / lv; }),
        T ("Digital Steps", "Digital", [] (double t, double x) { const double n = std::round (lerp (32.0, 3.0, t)); return std::floor (frac (x) * n) / (n - 1.0) * 2.0 - 1.0; }),
        S ("Harmonic Sweep", "Digital", [] (int h, double t) { const double c = 1.0 + 31.0 * t; return A ((h == 1 ? 0.5 : 0.0) + std::max (0.0, 1.0 - std::abs (h - c))); }),
        S ("Comb", "Digital", [] (int h, double t) { return A (1.0 / std::sqrt ((double) h) * std::pow (std::abs (std::cos (kPi * h * (0.05 + 0.45 * t))), 2.0)); }),
        S ("Spectral", "Digital", [] (int h, double t)
        {
            const double f = t * 15.0;
            const int k = std::min (14, (int) f);
            const double a = lerp (rnd ((uint32_t) (h * 131 + k)), rnd ((uint32_t) (h * 131 + k + 1)), f - k);
            return A (a / std::pow (h, 0.9) * (h < 64 ? 1.0 : 0.3), rnd ((uint32_t) h * 17u) * k2Pi);
        }),
        S ("Noise Hash", "Digital", [] (int h, double t) { return A ((0.3 + 0.7 * rnd ((uint32_t) h * 4099u + (uint32_t) (t * 8.0))) / std::sqrt ((double) h) * std::exp (-h / (8.0 + 300.0 * t)), rnd ((uint32_t) h * 6151u) * k2Pi); }),
        T ("Pulse Train", "Digital", [] (double t, double x)
        {
            const int n = 1 + (int) std::lround (7.0 * t);
            const double p = frac (x * n);
            return (p < 0.12 ? 1.0 : -0.12 / 0.88);
        }),
        S ("Squelch", "Digital", [] (int h, double t) { const double c = 2.0 + 40.0 * t * t; return A (1.0 / h * (1.0 + 6.0 * gauss (h, c, 0.8 + 0.08 * c)) * (h > c + 4 ? std::exp (-(h - c - 4) / 6.0) : 1.0)); }),
    };
    jassert ((int) g.size() == kTables);
    return g;
}
} // namespace

const TableInfo& Bank::info (int table) { return gens()[(size_t) std::clamp (table, 0, kTables - 1)].info; }

juce::StringArray Bank::names()
{
    juce::StringArray s;
    for (auto& g : gens()) s.add (g.info.name);
    return s;
}

juce::StringArray Bank::families()
{
    juce::StringArray s;
    for (auto& g : gens()) s.add (g.info.family);
    return s;
}

const Bank& Bank::get()
{
    static const Bank bank;
    return bank;
}

Bank::Bank()
{
    // layout: per table, per frame, the levels one after another (each with three guard samples)
    size_t off = 0;
    for (int l = 0; l < kLevels; ++l) { levelOffset[(size_t) l] = off; off += (size_t) size (l) + 3; }
    perFrame = off;
    perTable = perFrame * kFrames;
    data.assign (perTable * kTables, 0.0f);

    juce::dsp::FFT fwd (kGenOrder);
    std::vector<std::complex<float>> tin ((size_t) kGenN), tout ((size_t) kGenN);
    std::vector<std::unique_ptr<juce::dsp::FFT>> inv;
    std::vector<float> unit;   // the inverse transform's gain at each level (one harmonic at amplitude 1)
    for (int l = 0; l < kLevels; ++l)
    {
        const int n = size (l);
        int order = 0;
        while ((1 << order) < n) ++order;
        inv.push_back (std::make_unique<juce::dsp::FFT> (order));
        std::vector<std::complex<float>> a ((size_t) n), b ((size_t) n);
        a[1] = { 0.0f, -0.5f }; a[(size_t) n - 1] = { 0.0f, 0.5f };   // sin: one harmonic
        inv.back()->perform (a.data(), b.data(), true);
        float pk = 0.0f;
        for (auto& v : b) pk = std::max (pk, std::abs (v.real()));
        unit.push_back (pk > 0.0f ? 1.0f / pk : 1.0f);
    }

    std::vector<std::complex<double>> spec ((size_t) kTopHarm + 1);   // harmonic h: amplitude and phase of a sine
    std::vector<std::complex<float>> a, b;
    for (int ti = 0; ti < kTables; ++ti)
    {
        const auto& g = gens()[(size_t) ti];
        for (int f = 0; f < kFrames; ++f)
        {
            const double t = (double) f / (kFrames - 1);
            std::fill (spec.begin(), spec.end(), std::complex<double> {});
            if (g.spec)
                for (int h = 1; h <= kTopHarm; ++h) { const auto [amp, ph] = g.spec (h, t); spec[(size_t) h] = std::polar (amp, ph); }
            else
            {
                for (int i = 0; i < kGenN; ++i) tin[(size_t) i] = { (float) g.time (t, (double) i / kGenN), 0.0f };
                fwd.perform (tin.data(), tout.data(), false);
                // X[h] = N/2 · A · e^{i(φ − π/2)} for A sin (2πhx + φ)
                for (int h = 1; h <= kTopHarm; ++h)
                {
                    const auto X = std::complex<double> (tout[(size_t) h].real(), tout[(size_t) h].imag()) * (2.0 / kGenN);
                    spec[(size_t) h] = X * std::complex<double> (0.0, 1.0);   // back to the sine's amplitude and phase
                }
            }
            // the frame at each level: the harmonics it can hold, at the shared scale of its full-band version
            float scale = 1.0f;
            for (int l = 0; l < kLevels; ++l)
            {
                const int n = size (l), top = kTopHarm >> l;
                a.assign ((size_t) n, {}); b.assign ((size_t) n, {});
                for (int h = 1; h <= std::min (top, n / 2 - 1); ++h)
                {
                    // fade the last harmonics of a level in (no step in brightness between two levels)
                    const double edge = std::clamp ((top - h + 1) / (0.12 * top + 1.0), 0.0, 1.0);
                    const auto c = spec[(size_t) h] * std::complex<double> (0.0, -0.5) * edge;   // a sine of that amplitude and phase
                    a[(size_t) h] = { (float) c.real(), (float) c.imag() };
                    a[(size_t) (n - h)] = std::conj (a[(size_t) h]);
                }
                inv[(size_t) l]->perform (a.data(), b.data(), true);
                float* out = data.data() + (size_t) ti * perTable + (size_t) f * perFrame + levelOffset[(size_t) l];
                for (int i = 0; i < n; ++i) out[i] = b[(size_t) i].real() * unit[(size_t) l];
                if (l == 0)
                {
                    float pk = 0.0f;
                    for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (out[i]));
                    scale = pk > 1.0e-6f ? 0.9f / pk : 0.0f;
                }
                for (int i = 0; i < n; ++i) out[i] *= scale;
                out[n] = out[0]; out[n + 1] = out[1]; out[n + 2] = out[2];
            }
        }
    }
}
} // namespace mb::dsp::wt
