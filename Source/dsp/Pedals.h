#pragma once
#include "DspCommon.h"
#include "Analog.h"

/*  The three classic pedal circuits acid basslines are pushed through (docs/research/pedals.md), modelled
    in "volts" (1.0 full-scale ≈ 0.5 V at the pedal input) and run at 2× around the clipping:
    - Overdrive: op-amp clipping amp whose gain only applies above 720 Hz, diodes in its feedback (soft,
      added to the clean signal), a drive-dependent HF roll-off, then the 723 Hz tone stage (mid hump).
    - Distortion: high-gain op-amp with two gain legs (60 Hz and 1.5 kHz corners), its gain-bandwidth
      limit darkening the top at high gain, hard silicon diodes to ground, then the sweepable low-pass Filter.
    - Fuzz: input booster, two transistor clipping stages with diodes in their feedback and coupling
      high-passes between, then the passive tone stack (low-pass 408 Hz blended with high-pass 1.8 kHz). */

namespace mb::dsp::pedal
{
struct Pole   // one-pole TPT with cached coefficient
{
    float s = 0.0f, G = 0.0f, hz = -1.0f;
    void set (float f, float sr) { if (std::abs (f - hz) > 1.0e-3f) { hz = f; const float g = std::tan (kPi * clamp (f, 5.0f, 0.45f * sr) / sr); G = g / (1.0f + g); } }
    float lp (float x) { const float v = (x - s) * G, y = v + s; s = flush (y + v); return y; }
    float hp (float x) { return x - lp (x); }
    void reset() { s = 0.0f; }
};

/** Silicon diode pair: soft knee near 0.6 V (antiparallel, in a feedback loop). */
inline float diodeSoft (float v) { return 0.6f * fastTanh (v / 0.6f); }
/** Diodes to ground after a series resistor: a much harder knee at the same voltage. */
inline float diodeHard (float v) { const float a = std::abs (v) / 0.6f; return v / std::pow (1.0f + a * a * a * a, 0.25f); }

struct Overdrive
{
    Pole pre, roll, toneLp, outHp;
    float sr = 96000.0f;
    void prepare (float rate) { sr = rate; }
    void reset() { pre.reset(); roll.reset(); toneLp.reset(); outHp.reset(); }
    float process (float x, float drive, float tone)
    {
        const float rd = 500.0e3f * drive * drive;              // audio-taper drive pot
        const float gain = 1.0f + (51.0e3f + rd) / 4.7e3f;
        pre.set (720.0f, sr);
        roll.set (1.0f / (6.2832f * (51.0e3f + rd) * 51.0e-12f), sr);
        const float v = x * 0.5f;
        const float boosted = roll.lp ((gain - 1.0f) * pre.hp (v));   // the gain leg only sees > 720 Hz
        const float y = v + diodeSoft (boosted);                         // feedback diodes: soft, added to the clean part
        toneLp.set (723.0f, sr);
        const float lp = toneLp.lp (y);
        const float t = lp + (0.15f + 1.2f * tone) * (y - lp);           // tone: treble over the 723 Hz low-pass
        outHp.set (25.0f, sr);
        return outHp.hp (t) * 1.0f;
    }
};

struct Distortion
{
    Pole leg60, leg1k5, gbw, filt, outHp;
    float sr = 96000.0f;
    void prepare (float rate) { sr = rate; }
    void reset() { leg60.reset(); leg1k5.reset(); gbw.reset(); filt.reset(); outHp.reset(); }
    float process (float x, float drive, float tone)
    {
        const float rd = 100.0e3f * drive * drive;
        leg60.set (60.0f, sr); leg1k5.set (1539.0f, sr);
        const float v = x * 0.5f;
        float a = v + (rd / 560.0f) * leg60.hp (v) + (rd / 47.0f) * leg1k5.hp (v);
        const float g = 1.0f + rd / 47.0f;
        gbw.set (std::min (16000.0f, 1.0e6f / g), sr);                 // the slow op-amp: less top at high gain
        a = gbw.lp (clamp (a, -4.2f, 4.2f));                           // rails
        const float clipped = diodeHard (a);
        filt.set (482.0f * std::pow (66.0f, tone), sr);                // Filter: 482 Hz (dark) … 32 kHz (open)
        outHp.set (25.0f, sr);
        return outHp.hp (filt.lp (clipped)) * 0.75f;
    }
};

struct Fuzz
{
    Pole c1, c2, c3, lp, hp, outHp;
    float sr = 96000.0f;
    void prepare (float rate) { sr = rate; }
    void reset() { c1.reset(); c2.reset(); c3.reset(); lp.reset(); hp.reset(); outHp.reset(); }
    float process (float x, float drive, float tone)
    {
        c1.set (80.0f, sr); c2.set (100.0f, sr); c3.set (120.0f, sr);
        const float sustain = 2.0f + 40.0f * drive * drive;              // the input stage's gain (Sustain)
        float v = c1.hp (x * 0.5f) * sustain;
        v = diodeSoft (c2.hp (v) * 30.0f);                              // clipping stage 1
        v = diodeSoft (c3.hp (v) * 30.0f);                              // clipping stage 2
        lp.set (408.0f, sr); hp.set (1800.0f, sr);
        const float y = (1.0f - tone) * lp.lp (v) + tone * hp.hp (v);    // tone stack: the mid scoop
        outHp.set (25.0f, sr);
        return outHp.hp (y) * 1.3f;
    }
};

/** The pedal board for one channel: type 0 Overdrive, 1 Distortion, 2 Fuzz; amount = Drive, tone = Tone,
    mix blends with the clean signal. Runs the circuit at 2×. */
struct Board
{
    Overdrive od; Distortion ds; Fuzz fz;
    analog::Oversampler2x os;
    void prepare (float sr) { od.prepare (2.0f * sr); ds.prepare (2.0f * sr); fz.prepare (2.0f * sr); }
    /** The 2× path's delay at 1× (up and down half-bands, 7.5 samples each): the dry signal waits as long. */
    static constexpr int kLatency = analog::Oversampler2x::C;
    std::array<float, kLatency + 1> dry {};
    int dp = 0;
    float delayedDry = 0.0f;   // the input kLatency samples ago (aligned with the last output)
    void reset() { od.reset(); ds.reset(); fz.reset(); os.reset(); dry.fill (0.0f); delayedDry = 0.0f; }
    /** Always runs (a constant latency: switching the board in and out would jump by kLatency samples). */
    float process (float x, int type, float amount, float tone, float mix)
    {
        dry[(size_t) dp] = x;
        dp = (dp + 1) % (kLatency + 1);
        delayedDry = dry[(size_t) dp];
        auto tick = [&] (float v)
        {
            switch (type) { case 1: return ds.process (v, amount, tone); case 2: return fz.process (v, amount, tone); default: return od.process (v, amount, tone); }
        };
        float a, b;
        os.upsample (x, a, b);
        if (amount > 0.0005f) { a = tick (a); b = tick (b); }
        const float y = os.downsample (a, b);
        return delayedDry + (y - delayedDry) * mix;   // dry and wet in step: no comb filter at mixes between
    }
};
} // namespace mb::dsp::pedal
