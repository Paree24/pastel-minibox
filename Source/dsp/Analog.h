#pragma once
#include "DspCommon.h"
#include "Oscillators.h"

/*  Analog voice blocks for the Poly and Mono, written from docs/research/synths.md:
    the 4-pole OTA cascade filter, the 1974-style 12 dB low-pass, the DCO and VCO waveforms, the DCO
    synth's high-pass and BBD chorus, per-voice component tolerances and 2× oversampling.
    Filters are zero-delay-feedback (topology-preserving) with each OTA's tanh linearised around the
    previous sample's operating point, so they stay stable and smooth when driven or self-oscillating. */

namespace mb::dsp::analog
{
/** tanh(v)/v, the OTA's small-signal gain at its operating point: a rational fit (within ~1 % to
    |v| = 3, then falling like 1/|v|), cheap enough to run per stage at 2×. */
inline float tanhOverX (float v)
{
    const float v2 = v * v;
    if (v2 > 9.0f) return 1.0f / std::sqrt (v2);
    return (27.0f + v2) / (27.0f + 9.0f * v2);
}

// ------------------------------------------------------------------------------------------ oversampling
/** 2× oversampling: 31-tap half-band low-pass (Kaiser-ish Blackman window), ≈ −70 dB stop band. */
struct Oversampler2x
{
    static constexpr int N = 31, C = 15;
    float h[N] {};
    // each history is written twice (at p and p + N), so a tap reads b[p + N - i] with no wrap-around
    float up[2 * N] {}, dn[2 * N] {};
    int pu = 0, pd = 0;
    Oversampler2x()
    {
        double sum = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const double n = i - C;
            const double sinc = n == 0 ? 0.5 : std::sin (kPi * 0.5 * n) / (kPi * n);
            const double w = 0.42 - 0.5 * std::cos (2.0 * kPi * i / (N - 1)) + 0.08 * std::cos (4.0 * kPi * i / (N - 1));
            h[i] = (float) (sinc * w);
            sum += h[i];
        }
        for (auto& v : h) v = (float) (v / sum);
    }
    float fir (const float* b, int p) const
    {
        const float* x = b + p + N;   // x[-i] is the sample i steps back
        float acc = h[C] * x[-C];
        for (int i = 0; i < N; i += 2) acc += h[i] * x[-i];   // half-band: odd taps off-centre are zero
        return acc;
    }
    static void push (float* b, int& p, float v) { p = p + 1 == N ? 0 : p + 1; b[p] = b[p + N] = v; }
    void upsample (float x, float& a, float& b)
    {
        push (up, pu, 2.0f * x); a = fir (up, pu);
        push (up, pu, 0.0f);     b = fir (up, pu);
    }
    float downsample (float a, float b)
    {
        push (dn, pd, a);
        push (dn, pd, b);
        return fir (dn, pd);
    }
    void reset() { std::fill (std::begin (up), std::end (up), 0.0f); std::fill (std::begin (dn), std::end (dn), 0.0f); }
};

/** Runs a per-sample non-linear process at 2× through the oversampler. */
template <typename F>
inline float run2x (Oversampler2x& os, float x, F&& tick)
{
    float a, b;
    os.upsample (x, a, b);
    a = tick (a); b = tick (b);
    return os.downsample (a, b);
}

// ------------------------------------------------------------------------------------------ OTA cascade
/** Four OTA integrators in cascade with negative feedback from the fourth (the integrated 4-pole
    filter of the DCO polysynth and the monosynth). Each stage: dy/dt = ωc·tanh(u − y). Resonance
    0…1 maps to loop gain 0…≈4.3 (self-oscillation above ≈ 0.93), tapering near the top of the range;
    the passband drops with resonance (partly made up at the input, as on the circuit). */
struct OtaCascade
{
    float s[4] {}, d[4] {};   // integrator states, last stage differential inputs
    float g = 0.1f, k = 0.0f, inGain = 1.0f;
    uint32_t noise = 0x1234567u;
    void set (float hz, float res01, float sr)
    {
        const float fc = clamp (hz, 10.0f, 0.42f * sr);
        g = std::tan (kPi * fc / sr);
        const float r = clamp (res01, 0.0f, 1.0f);
        k = 4.3f * r * (0.6f + 0.4f * r);
        const float top = clamp ((fc / (0.5f * sr) - 0.6f) / 0.3f, 0.0f, 1.0f);   // resonance fades near Nyquist
        k *= 1.0f - 0.7f * top;
        inGain = 1.0f + 0.3f * k;
    }
    float process (float x)
    {
        // linearise each OTA around its last operating point
        float G[4], B[4];
        for (int i = 0; i < 4; ++i)
        {
            const float gi = g * tanhOverX (d[i]);
            G[i] = gi / (1.0f + gi); B[i] = 1.0f - G[i];
        }
        const float S = G[1] * G[2] * G[3] * B[0] * s[0] + G[2] * G[3] * B[1] * s[1] + G[3] * B[2] * s[2] + B[3] * s[3];
        const float Gp = G[0] * G[1] * G[2] * G[3];
        noise = noise * 1664525u + 1013904223u;
        const float xin = x * inGain + ((float) (noise >> 9) * (1.0f / 8388608.0f) - 1.0f) * 1.0e-5f;
        const float y4 = (Gp * xin + S) / (1.0f + k * Gp);
        float u = xin - k * y4;
        for (int i = 0; i < 4; ++i)
        {
            const float y = G[i] * u + B[i] * s[i];
            d[i] = u - y;
            s[i] = flush (2.0f * y - s[i]);
            u = y;
        }
        return u;
    }
    void reset() { for (int i = 0; i < 4; ++i) s[i] = d[i] = 0.0f; }
};

/** 12 dB/oct low-pass of the 1974 compact synth: a 2-pole state-variable core whose resonance (Peak)
    is positive feedback through a saturating stage, so it self-oscillates smoothly and tames loud
    resonant swings; a driven input stage for the paraphonic VA engine. */
struct Peak12
{
    float ic1 = 0.0f, ic2 = 0.0f, g = 0.1f, k = 2.0f, lastBp = 0.0f;
    void set (float hz, float peak01, float sr)
    {
        g = std::tan (kPi * clamp (hz, 10.0f, 0.42f * sr) / sr);
        const float p = clamp (peak01, 0.0f, 1.0f);
        k = 2.0f - 2.08f * p * p;   // < 0 near the top: self-oscillation, bounded by the saturation below
    }
    float process (float x, float drive = 0.0f)
    {
        x = fastTanh (x * (1.0f + 3.0f * drive)) / (1.0f + 1.5f * drive);
        // damping applied through a saturating feedback path: linearise around the last band-pass value
        const float sat = tanhOverX (lastBp * 0.8f);
        const float kk = 2.0f - (2.0f - k) * sat;
        const float a1 = 1.0f / (1.0f + g * (g + kk)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flush (2.0f * v1 - ic1); ic2 = flush (2.0f * v2 - ic2);
        lastBp = v1;
        return v2;
    }
    void reset() { ic1 = ic2 = lastBp = 0.0f; }
};

// ------------------------------------------------------------------------------------------ oscillators
/** DCO: a clock-reset capacitor ramp (slight curvature, short undershoot after the reset), a pulse
    from a comparator on that ramp, a flip-flop sub softened by an RC, crystal-stable pitch. */
struct Dco
{
    double phase = 0.0, dt = 0.0;
    bool flip = false;
    float under = 0.0f, subLp = 0.0f, subA = 0.5f;
    void prepare (float sr) { subA = 1.0f - std::exp (-kTwoPi * 4200.0f / sr); }
    void setFreq (double hz, double sr) { dt = std::clamp (hz / sr, 0.0, 0.45); }
    void randomise (Rng& r) { phase = r.unipolar(); flip = (r.next() & 1u) != 0; }
    static float curve (double p) { return (float) (p * (1.0 + 0.15 * (1.0 - p))); }   // 0..1
    void advance()
    {
        phase += dt;
        if (phase >= 1.0) { phase -= 1.0; flip = ! flip; under = -0.1f; }
        else under *= 0.5f;
    }
    float saw() const
    {
        const float v = 2.0f * curve (phase) - 1.0f;
        return v - (float) polyBlepD (phase, dt) + under;
    }
    float pulse (float pw) const
    {
        // the comparator threshold sits on the curved ramp: convert the knob to the matching phase
        const double w = std::clamp ((double) pw / (1.0 + 0.15 * (1.0 - pw)), 0.03, 0.97);
        double v = phase < w ? 1.0 : -1.0;
        v += polyBlepD (phase, dt);
        double t2 = phase - w; if (t2 < 0.0) t2 += 1.0;
        v -= polyBlepD (t2, dt);
        return (float) (v - (2.0 * w - 1.0));
    }
    float sub()
    {
        // band-limited square one octave down, then the passive RC
        const double sp = (flip ? 0.5 : 0.0) + 0.5 * phase, sdt = 0.5 * dt;
        double v = sp < 0.5 ? 1.0 : -1.0;
        v += polyBlepD (sp, sdt);
        double t2 = sp - 0.5; if (t2 < 0.0) t2 += 1.0;
        v -= polyBlepD (t2, sdt);
        subLp += subA * ((float) v - subLp);
        return subLp;
    }
    static double polyBlepD (double t, double d)
    {
        if (t < d) { t /= d; return t + t - t * t - 1.0; }
        if (t > 1.0 - d) { t = (t - 1.0) / d; return t * t + t + t + 1.0; }
        return 0.0;
    }
};

// ------------------------------------------------------------------------------------------ tolerance
/** Fixed per-voice component tolerances (as measured across real voice cards): cutoff ±5 %, envelope
    timing ±8 %, VCA gain ±0.5 dB; VCO voices also get a small static pitch offset and a slow drift. */
struct Tolerance
{
    float cutoff = 1.0f, envTime = 1.0f, gain = 1.0f, cents = 0.0f, driftRate = 0.1f, driftPh = 0.0f;
    void roll (Rng& r, bool vco)
    {
        cutoff = 1.0f + 0.05f * r.bipolar();
        envTime = 1.0f + 0.08f * r.bipolar();
        gain = dbToGain (0.5f * r.bipolar());
        cents = vco ? 2.5f * r.bipolar() : 0.0f;
        driftRate = 0.05f + 0.2f * r.unipolar();
        driftPh = r.unipolar();
    }
    /** Semitone offset for a VCO voice at this sample (static offset + slow ±1.5 cent wander). */
    float vcoSemis (float sr)
    {
        driftPh += driftRate / sr; if (driftPh >= 1.0f) driftPh -= 1.0f;
        return (cents + 1.5f * std::sin (kTwoPi * driftPh)) * 0.01f;
    }
};

// ------------------------------------------------------------------------------------------ chorus
/** The DCO polysynth's BBD chorus: one delay line per side around 3.5 ms, a rounded triangle LFO
    (I: 0.513 Hz ±0.5 ms, II: 0.863 Hz ±1.1 ms, I+II: both), sides 180° apart, ≈ 15 kHz filtering
    around the BBD, soft charge saturation, and the dry signal mixed in. */
struct BbdChorus60
{
    std::vector<float> bl, br;
    int w = 0, size = 1;
    float sr = 48000.0f, ph1 = 0.0f, ph2 = 0.25f;
    float preL = 0.0f, preR = 0.0f, postL = 0.0f, postR = 0.0f, a15 = 0.5f;
    void prepare (float rate)
    {
        sr = rate; size = (int) (0.012f * rate) + 4;
        bl.assign ((size_t) size, 0.0f); br.assign ((size_t) size, 0.0f);
        a15 = 1.0f - std::exp (-kTwoPi * 15000.0f / rate);
    }
    void reset() { std::fill (bl.begin(), bl.end(), 0.0f); std::fill (br.begin(), br.end(), 0.0f); }
    static float tri (float p) { const float t = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p; return t * (1.5f - 0.5f * t * t); }
    static float charge (float x) { return std::abs (x) < 0.7f ? x : std::copysign (0.7f + 0.3f * fastTanh ((std::abs (x) - 0.7f) / 0.3f), x); }
    float read (const std::vector<float>& b, float delaySamples) const
    {
        float rp = (float) w - delaySamples;
        while (rp < 0.0f) rp += (float) size;
        int i0 = (int) rp; const float f = rp - (float) i0;
        if (i0 >= size) i0 -= size;
        const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
        return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * f;
    }
    /** mode 0 off, 1 = I, 2 = II, 3 = I+II; `mix` scales the wet path (1 = the hardware balance). */
    void process (float& l, float& r, int mode, float mix)
    {
        preL += a15 * (l - preL); preR += a15 * (r - preR);
        bl[(size_t) w] = charge (preL); br[(size_t) w] = charge (preR);
        if (++w >= size) w = 0;
        if (mode <= 0) return;
        ph1 += 0.513f / sr; if (ph1 >= 1.0f) ph1 -= 1.0f;
        ph2 += 0.863f / sr; if (ph2 >= 1.0f) ph2 -= 1.0f;
        float m = 0.0f;
        if (mode == 1) m = 0.0005f * tri (ph1);
        else if (mode == 2) m = 0.0011f * tri (ph2);
        else m = 0.0005f * tri (ph1) + 0.0011f * tri (ph2);
        const float base = 0.0035f * sr;
        const float wl = read (bl, base + m * sr), wr = read (br, base - m * sr);
        postL += a15 * (wl - postL); postR += a15 * (wr - postR);
        const float wet = 0.6f * mix, dry = 1.0f - 0.6f * mix;
        l = 1.2f * (dry * 0.67f * l + wet * postL);
        r = 1.2f * (dry * 0.67f * r + wet * postR);
    }
};

// ------------------------------------------------------------------------------------------ DCO synth HPF
/** The DCO polysynth's 4-position high-pass: 0 = +10 dB low shelf below ~150 Hz, 1 = flat,
    2 = 1-pole at ~240 Hz, 3 = 1-pole at ~720 Hz. */
struct Hpf4
{
    float z = 0.0f;
    float process (float x, int pos, float sr)
    {
        static constexpr float hz[4] = { 150.0f, 0.0f, 240.0f, 720.0f };
        if (pos == 1) { z = 0.0f; return x; }
        const float a = 1.0f - std::exp (-kTwoPi * hz[std::clamp (pos, 0, 3)] / sr);
        z += a * (x - z);
        if (pos == 0) return x + 2.16f * z;   // +10 dB below the corner
        return x - z;
    }
};
} // namespace mb::dsp::analog
