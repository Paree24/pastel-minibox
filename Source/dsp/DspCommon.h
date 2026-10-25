#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mb::dsp
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float clamp (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
inline float midiToHz (float note) { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }
inline float semisToRatio (float semis) { return std::exp2 (semis / 12.0f); }
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

/** tanh from Lambert's continued fraction (7th order): within 2e-5 of tanh, monotonic, never above
    1 and smooth through its knee. (A 3rd-order Pade overshot 1 and had a corner at its clamp, which
    put a hard edge into every drive and filter stage.) */
inline float fastTanh (float x)
{
    if (x > 4.97f) return 1.0f;
    if (x < -4.97f) return -1.0f;
    const float x2 = x * x;
    const float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return clamp (num / den, -1.0f, 1.0f);
}

/** A device's output stage: linear below 0.7, rounding smoothly into a 0.95 ceiling (never a corner). */
inline float softLimit (float x)
{
    const float a = std::abs (x);
    if (a <= 0.7f) return x;
    return std::copysign (0.7f + 0.25f * fastTanh ((a - 0.7f) / 0.25f), x);
}

/** Coefficient for a one-pole smoother reaching ~63 % in `ms`. */
inline float onePoleCoef (float ms, float sr)
{
    if (ms <= 0.0f) return 1.0f;
    return 1.0f - std::exp (-1.0f / (0.001f * ms * sr));
}

/** Per-sample one-pole smoother. The target is passed in on every tick and never written
    back into the parameter it was read from (a smoother that reads its own output stalls). */
struct Smoother
{
    float y = 0.0f, k = 1.0f;
    void setTime (float ms, float sr) { k = onePoleCoef (ms, sr); }
    void reset (float v) { y = v; }
    float tick (float target) { y += (target - y) * k; return y; }
};

/** xorshift32 noise. */
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    explicit Rng (uint32_t seed = 0x9E3779B9u) : s (seed ? seed : 1u) {}
    void seed (uint32_t v) { s = v ? v : 1u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float bipolar() { return (float) (int32_t) next() * (1.0f / 2147483648.0f); }
    float unipolar() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
};

struct OnePole
{
    float z = 0.0f, a = 1.0f;
    void setCutoff (float hz, float sr) { a = 1.0f - std::exp (-kTwoPi * clamp (hz, 1.0f, sr * 0.49f) / sr); }
    float lp (float x) { z += a * (x - z); return z; }
    float hp (float x) { return x - lp (x); }
    void reset() { z = 0.0f; }
};

struct DcBlock
{
    float x1 = 0.0f, y1 = 0.0f, r = 0.995f;
    void prepare (float sr) { r = 1.0f - (kTwoPi * 10.0f / sr); }
    float process (float x) { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0.0f; }
};

inline float flush (float v) { return std::abs (v) < 1.0e-15f ? 0.0f : v; }

/** Tempo-synced divisions shared by LFOs, delays and the arpeggiator (beats = quarter notes). */
struct Division { const char* name; double beats; };
constexpr int kNumDivisions = 14;
inline const Division* divisions()
{
    static const Division d[kNumDivisions] = {
        { "2 bar", 8.0 }, { "1 bar", 4.0 }, { "1/2", 2.0 }, { "1/4 D", 1.5 }, { "1/4", 1.0 }, { "1/4 T", 2.0 / 3.0 },
        { "1/8 D", 0.75 }, { "1/8", 0.5 }, { "1/8 T", 1.0 / 3.0 }, { "3/16", 0.75 }, { "1/16", 0.25 },
        { "1/16 T", 1.0 / 6.0 }, { "1/32", 0.125 }, { "1/64", 0.0625 } };
    return d;
}
inline double divisionBeats (float index)
{
    const int i = std::clamp ((int) std::lround (index), 0, kNumDivisions - 1);
    return divisions()[i].beats;
}
} // namespace mb::dsp
