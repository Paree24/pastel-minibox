#pragma once
#include "DspCommon.h"

namespace mb::dsp
{
inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

inline float polyBlamp (float t, float dt)
{
    if (t < dt) { t = t / dt - 1.0f; return -1.0f / 3.0f * t * t * t; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt + 1.0f; return 1.0f / 3.0f * t * t * t; }
    return 0.0f;
}

/** Band-limited saw / pulse / triangle with hard-sync and a free-running random start phase. */
struct PolyBlepOsc
{
    float phase = 0.0f, inc = 0.0f;
    bool wrapped = false;   // set on the sample the phase wrapped (sync source, sub-oscillator clock)

    void randomise (Rng& r) { phase = r.unipolar(); }
    void setFreq (float hz, float sr) { inc = clamp (hz / sr, 0.0f, 0.49f); }

    void advance()
    {
        phase += inc;
        wrapped = phase >= 1.0f;
        if (wrapped) phase -= 1.0f;
    }

    float saw() const { return 2.0f * phase - 1.0f - polyBlep (phase, inc); }

    float pulse (float pw) const
    {
        pw = clamp (pw, 0.03f, 0.97f);
        float v = phase < pw ? 1.0f : -1.0f;
        v += polyBlep (phase, inc);
        float t2 = phase - pw; if (t2 < 0.0f) t2 += 1.0f;
        v -= polyBlep (t2, inc);
        return v - (2.0f * pw - 1.0f);   // DC-free so pulse width never shifts the level
    }

    float tri() const
    {
        float v = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
        v += 4.0f * inc * polyBlamp (phase, inc);
        float t2 = phase + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
        v -= 4.0f * inc * polyBlamp (t2, inc);
        return v;
    }

    float sine() const { return std::sin (kTwoPi * phase); }
};

/** Sub-oscillator clocked by the main oscillator's wraps: square −1 or −2 octaves, or a 25 % pulse −2. */
struct SubOsc
{
    int count = 0;
    float last = 0.0f;
    OnePole smooth;

    void prepare (float sr) { smooth.setCutoff (9000.0f, sr); }
    float process (bool mainWrapped, int type)   // 0 = −1 sq, 1 = −2 sq, 2 = −2 25 %
    {
        if (mainWrapped) count = (count + 1) & 3;
        float v;
        if (type == 0) v = (count & 1) ? -1.0f : 1.0f;
        else if (type == 1) v = (count & 2) ? -1.0f : 1.0f;
        else v = count == 0 ? 1.0f : -0.33f;
        last = smooth.lp (v);
        return last;
    }
};

struct Noise
{
    Rng rng;
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    float white() { return rng.bipolar(); }
    float pink()
    {
        const float w = rng.bipolar();
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        const float p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
        return p * 0.11f;
    }
};

/** Phase-modulation operator: 4096-entry sine table, optional extra waveforms, self feedback. */
struct SineTable
{
    static constexpr int N = 4096;
    float t[N + 1];
    SineTable() { for (int i = 0; i <= N; ++i) t[i] = std::sin (kTwoPi * (float) i / N); }
    float lookup (float ph) const
    {
        ph -= std::floor (ph);
        const float x = ph * N;
        const int i = (int) x;
        return t[i] + (t[i + 1] - t[i]) * (x - (float) i);
    }
    static const SineTable& get() { static SineTable s; return s; }
};

struct SineOp
{
    float phase = 0.0f, inc = 0.0f, prev1 = 0.0f, prev2 = 0.0f;
    void setFreq (float hz, float sr) { inc = clamp (hz / sr, 0.0f, 0.49f); }

    /** wave: 0 sine, 1 half-sine, 2 square-ish, 3 saw-ish. `pm` is phase modulation in cycles. */
    float process (float pm, int wave)
    {
        const auto& tab = SineTable::get();
        const float ph = phase + pm;
        float s = tab.lookup (ph);
        switch (wave)
        {
            case 1: s = s > 0.0f ? s : 0.0f; s = s * 2.0f - 0.64f; break;
            case 2: s = fastTanh (s * 4.0f); break;
            case 3: s = s + 0.5f * tab.lookup (ph * 2.0f) + 0.33f * tab.lookup (ph * 3.0f); s *= 0.55f; break;
            default: break;
        }
        phase += inc; if (phase >= 1.0f) phase -= 1.0f;
        prev2 = prev1; prev1 = s;
        return s;
    }
    float feedbackSignal() const { return 0.5f * (prev1 + prev2); }
};
} // namespace mb::dsp
