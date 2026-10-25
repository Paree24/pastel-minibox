#pragma once
#include "DspCommon.h"

namespace mb::dsp
{
/** Zero-delay-feedback state-variable filter (12 dB LP / BP / HP). */
struct SvfTpt
{
    float ic1 = 0.0f, ic2 = 0.0f, a1 = 0, a2 = 0, a3 = 0, k = 1.4f;
    void set (float hz, float res, float sr)
    {
        const float g = std::tan (kPi * clamp (hz, 16.0f, sr * 0.45f) / sr);
        k = 2.0f - 1.96f * clamp (res, 0.0f, 1.0f) * clamp (res, 0.0f, 1.0f);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    /** mode: 0 LP, 1 BP, 2 HP */
    float process (float x, int mode)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flush (2.0f * v1 - ic1);
        ic2 = flush (2.0f * v2 - ic2);
        if (mode == 0) return v2;
        if (mode == 1) return v1 * k;
        return x - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = 0.0f; }
};

/** 4-pole transistor ladder (ZDF one-poles, per-stage tanh, resonance compensation, self-oscillates). */
struct Ladder4
{
    float s[4] {}, G = 0.0f, g = 0.0f, k = 0.0f, comp = 0.0f;
    void set (float hz, float res, float sr)
    {
        g = std::tan (kPi * clamp (hz, 16.0f, sr * 0.45f) / sr);
        G = g / (1.0f + g);
        const float r = clamp (res, 0.0f, 1.0f);
        k = 4.2f * r * r * (2.0f - r) * 0.5f + 0.0f;   // gradual curve, reaches self-oscillation near 1
        k *= 2.0f;
        comp = 1.0f + 0.65f * k;                        // keeps the pass-band level as resonance rises
    }
    float process (float x)
    {
        const float b = 1.0f / (1.0f + g);
        const float S = G * G * G * s[0] * b + G * G * s[1] * b + G * s[2] * b + s[3] * b;
        const float G4 = G * G * G * G;
        const float y4est = (G4 * x * comp + S) / (1.0f + k * G4);
        float u = fastTanh (x * comp - k * y4est);
        for (int i = 0; i < 4; ++i)
        {
            const float v = (u - s[i]) * G;
            const float y = v + s[i];
            s[i] = flush (y + v);
            u = i < 3 ? fastTanh (y) : y;
        }
        return u;
    }
    void reset() { for (auto& v : s) v = 0.0f; }
};

/** Diode-ladder flavour: unequal stage coupling, softer resonance, thinner bass at high resonance
    and a bass-compensation control that restores the lost low end. */
struct DiodeLadder4
{
    float s[4] {}, G = 0.0f, k = 0.0f, bassComp = 0.0f;
    OnePole fbHp, lowShelf;
    float sr = 48000.0f;
    void prepare (float rate) { sr = rate; fbHp.setCutoff (90.0f, rate); lowShelf.setCutoff (160.0f, rate); }
    void set (float hz, float res, float, float bc)
    {
        const float g = std::tan (kPi * clamp (hz, 16.0f, sr * 0.45f) / sr);
        G = g / (1.0f + g);
        const float r = clamp (res, 0.0f, 1.0f);
        k = 7.2f * r * r * (1.5f - 0.5f * r);
        bassComp = bc;
    }
    float process (float x)
    {
        // feedback goes through a high-pass, so resonance thins the bass like the diode circuit
        const float fb = fbHp.hp (s[3]);
        float u = fastTanh (1.15f * (x - k * fb));
        static constexpr float couple[4] = { 0.5f, 1.0f, 1.0f, 1.0f };
        for (int i = 0; i < 4; ++i)
        {
            const float next = i < 3 ? s[i + 1] : 0.0f;
            const float in = u - 0.25f * couple[i] * (next - s[i]) * 0.5f;
            const float v = (in - s[i]) * G;
            const float y = v + s[i];
            s[i] = flush (y + v);
            u = y * 1.02f;
            if (i == 1) u = fastTanh (u * 1.2f) / 1.2f;
        }
        const float lows = lowShelf.lp (x);
        return u * (1.0f + 0.4f * k / 7.2f) + bassComp * 0.9f * lows * (k / 7.2f);
    }
    void reset() { for (auto& v : s) v = 0.0f; fbHp.reset(); lowShelf.reset(); }
};

/** 2-pole OTA-style low-pass (12 dB) with a soft-clipping resonance loop and input drive. */
struct Ota2Pole
{
    float ic1 = 0.0f, ic2 = 0.0f, g = 0.0f, k = 2.0f, drive = 1.0f;
    void set (float hz, float res, float sr, float drv)
    {
        g = std::tan (kPi * clamp (hz, 16.0f, sr * 0.45f) / sr);
        const float r = clamp (res, 0.0f, 1.0f);
        k = 2.0f - 2.05f * r * r;
        drive = 1.0f + 4.0f * drv;
    }
    float process (float x)
    {
        x = fastTanh (x * drive) / std::sqrt (drive);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float v3 = x - ic2;
        const float v1 = a1 * (ic1 + g * v3);
        const float v2 = ic2 + g * v1;
        ic1 = flush (2.0f * fastTanh (v1) - ic1);   // soft clip inside the resonance loop
        ic2 = flush (2.0f * v2 - ic2);
        return v2;
    }
    void reset() { ic1 = ic2 = 0.0f; }
};

/** Four-position non-resonant high-pass. Position 0 is a gentle low-shelf boost. */
struct HpfStepped
{
    SvfTpt svf;
    OnePole shelf;
    int pos = 1;
    void set (int position, float sr)
    {
        pos = std::clamp (position, 0, 3);
        static constexpr float hz[4] = { 0.0f, 140.0f, 360.0f, 820.0f };
        if (pos > 0) svf.set (hz[pos], 0.12f, sr);
        shelf.setCutoff (110.0f, sr);
    }
    float process (float x)
    {
        if (pos == 0) return x + 0.5f * shelf.lp (x);
        return svf.process (x, 2);
    }
    void reset() { svf.reset(); shelf.reset(); }
};

/** Runs a non-linear filter at `factor`× the host rate with linear-interpolated input
    and averaged output. Filters must be `set` with the oversampled rate. */
template <typename F>
inline float oversampled (F&& tick, float x, float& prevIn, int factor)
{
    if (factor <= 1) { prevIn = x; return tick (x); }
    float acc = 0.0f;
    for (int i = 1; i <= factor; ++i)
        acc += tick (prevIn + (x - prevIn) * (float) i / (float) factor);
    prevIn = x;
    return acc / (float) factor;
}
} // namespace mb::dsp
