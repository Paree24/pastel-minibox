#pragma once
#include "DspCommon.h"
#include "Oscillators.h"

/*  Drum voice models. Each one follows the circuit or architecture of a reference instrument from the
    research notes (docs/research/drums.md): what excites it, what rings, how pitch and level move,
    and which part of the spectrum each component occupies. All are written for this project. */

namespace mb::dsp::drum
{
/** Controls shared by every model (the part's knobs, read once per block). */
/** exp (x) for an argument that rarely changes (a per-sample decay factor from a fixed time): recomputed only when x
    changes, so the result is exactly std::exp's. */
struct ExpCache
{
    float x = std::numeric_limits<float>::quiet_NaN(), v = 1.0f;
    float operator() (float a) { if (a != x) { x = a; v = std::exp (a); } return v; }
};

struct Ctl
{
    float pitch = 50.0f, bend = 0.5f, bendTime = 0.05f, decay = 0.5f, tone = 0.5f, click = 0.4f;
    float accent = 0.0f;   // 0 = normal, 1 = full accent (already scaled by the part's Accent amount)
    int layA = 0, layB = 3, layMod = 0, layEnv = 1;
    float layMix = 0.3f, layRatio = 2.0f;
};

// ------------------------------------------------------------------------------------------ helpers
/** Zero-delay state-variable filter that tolerates per-sample frequency / damping changes. */
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f, lp = 0.0f, bp = 0.0f, hp = 0.0f, lastHz = -1.0f, g = 0.0f;
    void tick (float x, float hz, float k, float sr)
    {
        if (hz != lastHz) { lastHz = hz; g = std::tan (kPi * clamp (hz, 5.0f, sr * 0.45f) / sr); }   // fixed-frequency filters skip the tan
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flush (2.0f * v1 - ic1); ic2 = flush (2.0f * v2 - ic2);
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = lp = bp = hp = 0.0f; }
};

/** Damping that gives a resonator a −60 dB time of `t60` seconds at `hz`. */
inline float dampFor (float hz, float t60) { return clamp (13.8155f / (kTwoPi * std::max (hz, 1.0f) * std::max (t60, 0.002f)), 0.0005f, 2.0f); }
inline float t60Coef (float t60, float sr) { return std::exp (-6.9078f / (std::max (t60, 0.001f) * sr)); }

/** First-order TPT low/high-pass. */
struct Tpt1
{
    float s = 0.0f, lastHz = -1.0f, G = 0.0f;
    float lp (float x, float hz, float sr)
    {
        if (hz != lastHz) { lastHz = hz; const float g = std::tan (kPi * clamp (hz, 5.0f, sr * 0.45f) / sr); G = g / (1.0f + g); }
        const float v = (x - s) * G, y = v + s;
        s = flush (y + v);
        return y;
    }
    float hp (float x, float hz, float sr) { return x - lp (x, hz, sr); }
    void reset() { s = 0.0f; }
};

/** Analog noise (transistor junction): roughly Gaussian. */
inline float analogNoise (Rng& r) { return (r.bipolar() + r.bipolar() + r.bipolar()) * 0.577f; }

/** Digital noise: a maximal-length shift register clocked every sample, binary ±1. */
struct Lfsr
{
    uint32_t s = 0x1u;
    float next() { const uint32_t bit = ((s >> 0) ^ (s >> 3)) & 1u; s = (s >> 1) | (bit << 30); return (s & 1u) ? 1.0f : -1.0f; }
};

/** A triangle rounded by back-to-back diodes: near-sine at drive 1, towards a square with more drive. */
inline float diodeRound (float tri, float drive)
{
    const float g = 1.55f * (1.0f + 4.0f * drive);
    return fastTanh (tri * g) / fastTanh (g);
}

inline float bandTri (double phase, double dt)
{
    // band-limited triangle (BLAMP at both corners)
    const float p = (float) phase, d = (float) dt;
    float v = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
    v += 4.0f * d * polyBlamp (p, d);
    float t2 = p + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
    v -= 4.0f * d * polyBlamp (t2, d);
    return v;
}

inline float blepSquare (double phase, double dt)
{
    const float p = (float) phase, d = (float) dt;
    float v = p < 0.5f ? 1.0f : -1.0f;
    v += polyBlep (p, d);
    float t2 = p + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
    v -= polyBlep (t2, d);
    return v;
}

// ------------------------------------------------------------------------------------------ bridged-T
/** The 1980 machine's bridged-T voice: a band-pass resonator kicked by a 1 ms trigger pulse through a
    diode-clipped low-shelf pulse shaper. For the first few milliseconds an envelope raises the centre
    frequency and Q (the punch); afterwards the centre frequency follows the ringing level slightly
    (leakage into the tuning transistor: the "sigh"). Retriggers add to the ringing state. */
struct BridgedT
{
    ExpCache ec0, ec1;   // per-sample decay factors from fixed times: exp only when they change
    Svf res;
    Tpt1 shelf;
    float sr = 48000.0f, pulseAmp = 0.0f, follower = 0.0f, attackEnv = 0.0f, gain = 1.0f;
    int t = 1 << 30;

    void trigger (float amp) { t = 0; pulseAmp = amp; }
    /** shift: attack frequency rise (1 = +100 %), sigh: pitch rise at full level, t60: ring time. */
    float process (float f0, float t60, float shift, float sigh, float extraIn = 0.0f)
    {
        const int pulseLen = (int) (0.001f * sr);
        const float p = t < pulseLen ? pulseAmp : 0.0f;
        // pulse shaper: low shelf (DC gain 0.3) then the diode clamps negative swings at a diode drop
        float s = p - 0.7f * shelf.lp (p, 1100.0f, sr);
        if (s < 0.0f) s = -0.05f * (1.0f - std::exp (s / 0.05f));
        // attack envelope: high while the trigger is on, settles ~5 ms after it falls
        attackEnv = t < pulseLen ? 1.0f : attackEnv * ec0 (-1.0f / (0.0018f * sr));
        const float hz = f0 * (1.0f + shift * attackEnv) * (1.0f + sigh * std::min (1.0f, follower));
        const float k = dampFor (hz, t60) * (1.0f - 0.6f * attackEnv);
        res.tick (s + extraIn, hz, k, sr);
        // normalised by the shaped pulse's area (≈ 0.3 × 1 ms) × ω: a full trigger rings at about unit level
        const float y = res.bp / (kTwoPi * hz * 0.0003f) * gain;
        follower = std::max (std::abs (y), follower * ec1 (-1.0f / (0.035f * sr)));
        ++t;
        return y;
    }
    void reset() { res.reset(); shelf.reset(); follower = attackEnv = 0.0f; t = 1 << 30; }
};

// ------------------------------------------------------------------------------------------ kick models
/** 1980 kick: bridged-T at ~50 Hz, decay sets the ring time, Click = attack punch (frequency/Q jump),
    Bend = sigh depth, Tone = passive output low-pass, op-amp rounding adds the 2nd/3rd harmonics. */
struct Kick808
{
    BridgedT bt;
    Tpt1 toneLp, outHp;
    float sr = 48000.0f;
    void prepare (float rate) { sr = rate; bt.sr = rate; bt.gain = 1.0f; }
    void trigger (const Ctl& c) { bt.trigger (0.55f + 0.45f * c.accent); }
    float process (const Ctl& c)
    {
        const int t0 = bt.t;
        float y = bt.process (c.pitch, c.decay, 0.4f + 1.4f * c.click, 0.45f * c.bend);
        // op-amp rounding fitted to the measured harmonics: H2 ≈ −19 dB, H3 ≈ −29 dB at a normal hit
        // (an accented hit drives it harder: more harmonics, as on the circuit)
        const float u = y / 0.85f;
        y = (fastTanh (0.6f * u + 0.175f * u * u) - fastTanh (0.0f)) * 1.5f;
        // the trigger pulse also reaches the output past the body (the circuit's click), through the Tone low-pass:
        // Click above 50 % brings it in (below, the click is only what the pulse rings in the body, as it was)
        if (c.click > 0.5f && t0 < (int) (0.0006f * sr)) y += 4.0f * (c.click - 0.5f) * (0.55f + 0.45f * c.accent);
        y = toneLp.lp (y, 180.0f * std::pow (60.0f, c.tone), sr);
        return outHp.hp (y, 22.0f, sr);
    }
    bool done() const { return bt.follower < 2.0e-5f && bt.t > (int) (0.01f * sr); }
    void reset() { bt.reset(); toneLp.reset(); outHp.reset(); }
};

/** 1983 kick: triangle VCO rounded by diodes, an envelope pushes it up at note start (Tune = its decay
    time), body holds near full level before decaying, attack = a short pulse plus low-passed noise (Click = its level,
    Tone = its decay). */
struct Kick909
{
    ExpCache ec0;   // per-sample decay factors from fixed times: exp only when they change
    double phase = 0.25, dt = 0.0;
    float sr = 48000.0f, penv = 0.0f, amp = 0.0f;
    int t = 1 << 30;
    Tpt1 clickLp, outHp;
    Lfsr lfsr;
    void prepare (float rate) { sr = rate; }
    void trigger (const Ctl& c) { phase = 0.25; penv = 1.0f; amp = 0.85f + 0.15f * c.accent; t = 0; }
    float process (const Ctl& c)
    {
        const float tm = (float) t / sr;
        penv *= ec0 (-1.0f / (std::max (0.002f, c.bendTime) * sr));
        const float hz = c.pitch * (1.0f + 13.0f * c.bend * penv);   // bend 0.45: ≈ 6× at the start (≈ 300 Hz → 50 Hz, τ ≈ 10 ms)
        dt = std::min (0.45, (double) hz / sr);
        phase += dt; if (phase >= 1.0) phase -= 1.0;
        const float body = diodeRound (bandTri (phase, dt), 0.0f);
        const float hold = std::min (0.06f, 0.12f * c.decay);
        const float env = tm < hold ? 1.0f : std::exp (-6.9f * (tm - hold) / std::max (0.02f, c.decay));
        const float pulse = t < (int) (0.0004f * sr) ? 1.0f : 0.0f;
        // Tone = the click's decay: 6 ms at 0.4 (the fixed time it had), ≈ 1.6 ms at 0 to ≈ 43 ms at 1
        const float clickTau = 0.006f * std::pow (27.0f, c.tone - 0.4f);
        const float nz = clickLp.lp (lfsr.next(), 5000.0f, sr) * std::exp (-tm / clickTau);
        const float clk = c.click * (0.9f * pulse + 0.7f * nz);
        ++t;
        return outHp.hp ((body * env + clk) * amp, 20.0f, sr);
    }
    bool done() const { return t > (int) (12.0f * sr); }
    void reset() { t = 1 << 30; clickLp.reset(); outHp.reset(); }
};

/** Resonating-filter kick: a 2-pole low-pass driven past self-oscillation is the voice; its cutoff is
    bent down from above (Bend, Bend Time), a pulse kicks it (Click = pulse level), the amp envelope
    shapes it, a multi-stage drive and a 12 dB tone low-pass follow. */
struct KickResonator
{
    ExpCache ec0;   // per-sample decay factors from fixed times: exp only when they change
    Svf osc, toneLp;
    float sr = 48000.0f, penv = 0.0f, amp = 0.0f, accAmp = 1.0f;
    int t = 1 << 30;
    Rng rng { 4242u };
    void prepare (float rate) { sr = rate; }
    void trigger (const Ctl& c) { penv = 1.0f; t = 0; accAmp = 0.8f + 0.2f * c.accent; }
    float process (const Ctl& c)
    {
        const float tm = (float) t / sr;
        penv *= ec0 (-1.0f / (std::max (0.003f, c.bendTime) * sr));
        const float hz = c.pitch * std::exp2 (4.0f * c.bend * penv);
        const float pulse = t < (int) (0.0006f * sr) ? 2.5f * (0.3f + c.click) : 0.0f;
        // negative damping makes it sing; the saturating band-pass state sets the singing level
        osc.tick (pulse + analogNoise (rng) * 1.0e-4f, hz, -0.04f, sr);
        osc.ic1 = 1.2f * fastTanh (osc.ic1 / 1.2f);
        float y = osc.lp;
        const float a = std::min (1.0f, tm / 0.0015f);
        amp = a * std::exp (-6.9f * tm / std::max (0.03f, c.decay));
        y *= amp * accAmp * 0.8f;
        // tone: 12 dB low-pass
        toneLp.tick (y, 250.0f * std::pow (48.0f, c.tone), 1.2f, sr);
        ++t;
        return 6.0f * toneLp.lp + 0.6f * c.click * pulse * std::exp (-tm / 0.0008f) * 0.15f;
    }
    bool done() const { return t > (int) (0.05f * sr) && amp < 1.0e-4f; }
    void reset() { osc.reset(); toneLp.reset(); t = 1 << 30; amp = 0.0f; }
};

// ------------------------------------------------------------------------------------------ snares
/** 1980 snare: two bridged-T resonators about an octave apart, plus white noise high-passed into the
    2–8 kHz band with its own short envelope ("snappy"); the snappy contour also nudges the resonators. */
struct Snare808
{
    ExpCache ec0;   // per-sample decay factors from fixed times: exp only when they change
    BridgedT lo, hi;
    Svf noiseHp;
    Tpt1 noiseLp;
    Rng rng { 808u };
    float sr = 48000.0f, nEnv = 0.0f, acc = 1.0f;
    int t = 1 << 30;
    void prepare (float rate) { sr = rate; lo.sr = hi.sr = rate; lo.gain = 1.0f; hi.gain = 1.0f; }
    void trigger (const Ctl& c) { acc = 0.6f + 0.4f * c.accent; lo.trigger (acc); hi.trigger (acc); nEnv = acc; t = 0; }
    float process (const Ctl& c)
    {
        const float a = std::min (1.0f, (float) t / (0.0004f * sr));
        nEnv *= ec0 (-1.0f / ((0.018f + 0.05f * c.decay) * sr));
        const float body = lo.process (c.pitch, 0.8f * c.decay + 0.04f, 0.15f, 0.02f, nEnv * 0.004f)
                         + (0.05f + 0.3f * c.tone) * hi.process (c.pitch * 1.98f, 0.55f * c.decay + 0.03f, 0.1f, 0.0f);
        noiseHp.tick (analogNoise (rng), 1800.0f, 1.2f, sr);
        const float nz = noiseLp.lp (noiseHp.hp, 9000.0f, sr) * nEnv * a;
        ++t;
        return 0.75f * body + (2.4f * c.click) * nz;
    }
    bool done() const { return t > (int) (0.02f * sr) && lo.follower < 2.0e-5f && hi.follower < 2.0e-5f && nEnv < 1.0e-4f; }
    void reset() { lo.reset(); hi.reset(); noiseHp.reset(); noiseLp.reset(); t = 1 << 30; nEnv = 0.0f; }
};

/** 1983 snare: two diode-rounded triangle VCOs on one pitch CV with a short pitch bump at the start;
    each through its own single-transistor VCA (a decaying envelope that stops conducting below a
    threshold, so accent = louder and longer); digital noise in two paths: a quiet short low band and
    the main high-passed "snappy" blast that holds ~24 ms before decaying (Tone sets that decay). */
struct Snare909
{
    ExpCache ec0, ec1, ec2, ec3;   // per-sample decay factors from fixed times: exp only when they change
    double ph1 = 0.25, ph2 = 0.25;
    float sr = 48000.0f, e1 = 0.0f, e2 = 0.0f, bump = 0.0f, nA = 0.0f, nB = 0.0f, acc = 1.0f;
    int t = 1 << 30;
    Lfsr lfsr;
    Tpt1 aLp, bHp, bLp;
    void prepare (float rate) { sr = rate; }
    void trigger (const Ctl& c)
    {
        acc = 0.7f + 0.3f * c.accent;
        e1 = e2 = acc; nA = 1.0f; nB = acc; t = 0; ph1 = ph2 = 0.25;
    }
    static float vca (float env) { return std::max (0.0f, env - 0.04f) / 0.96f; }   // transistor threshold
    float process (const Ctl& c)
    {
        const float tm = (float) t / sr;
        // pitch bump: ~0.6 ms rise, ~4 ms fall
        bump = tm < 0.0006f ? tm / 0.0006f : bump * ec0 (-1.0f / (0.004f * sr));
        const float f1 = c.pitch * (1.0f + 1.2f * c.bend * bump), f2 = f1 * 1.62f;
        const double d1 = std::min (0.45, (double) f1 / sr), d2 = std::min (0.45, (double) f2 / sr);
        ph1 += d1; if (ph1 >= 1.0) ph1 -= 1.0;
        ph2 += d2; if (ph2 >= 1.0) ph2 -= 1.0;
        const float tau1 = 0.03f + 0.15f * c.decay;
        e1 *= ec1 (-1.0f / (tau1 * sr));
        e2 *= ec2 (-1.0f / (0.55f * tau1 * sr));
        const float body = diodeRound (bandTri (ph1, d1), 0.1f) * vca (e1) + 0.55f * diodeRound (bandTri (ph2, d2), 0.1f) * vca (e2);
        const float n = lfsr.next();
        nA *= ec3 (-1.0f / (0.006f * sr));
        const float pathA = aLp.lp (n, 4500.0f, sr) * nA * 0.25f;
        const float holdT = 0.024f;
        const float decB = 0.015f + 0.12f * c.tone;
        const float envB = (tm < holdT ? nB : nB * std::exp (-(tm - holdT) / decB));
        const float pathB = bLp.lp (bHp.hp (n, 1100.0f, sr), 11000.0f, sr) * envB * 0.55f;
        ++t;
        return 0.7f * body + c.click * 1.2f * (pathA + pathB);
    }
    bool done() const { return t > (int) (2.0f * sr) || (t > (int) (0.03f * sr) && e1 < 0.03f && nB * std::exp (-((float) t / sr - 0.024f) / 0.28f) < 1.0e-4f); }
    void reset() { t = 1 << 30; aLp.reset(); bHp.reset(); bLp.reset(); }
};

// ------------------------------------------------------------------------------------------ clap
/** Hand clap: band-passed noise gated by a few sawtooth pulses ~11 ms apart (sharp rise, sloping fall),
    then a softer, darker, slowly decaying pseudo-reverb of the same noise. Click = burst spacing. */
struct Clap
{
    Svf bp, revBp;
    Tpt1 revLp, hp;
    Rng rng { 909u };
    Lfsr lfsr;
    bool digital = false;   // 1983 machine: shift-register noise, four pulses, a brighter band
    float sr = 48000.0f, acc = 1.0f;
    int t = 1 << 30;
    void prepare (float rate) { sr = rate; }
    void trigger (const Ctl& c) { t = 0; acc = 0.75f + 0.25f * c.accent; }
    float process (const Ctl& c)
    {
        const float tm = (float) t / sr;
        const float sp = 0.0085f + 0.006f * (1.0f - c.click);
        const int k = (int) (tm / sp);
        const int pulses = digital ? 4 : 3;
        float gate = 0.0f;
        if (k < pulses) { const float ph = (tm - (float) k * sp) / sp; gate = (1.0f - ph) * (1.0f - ph) * (k == pulses - 1 ? 1.0f : 0.85f); }
        const float revStart = ((float) pulses - 0.4f) * sp;
        const float rev = tm < revStart ? 0.0f : 0.2f * std::exp (-6.9f * (tm - revStart) / std::max (0.05f, c.decay));
        const float n = digital ? lfsr.next() * 0.6f : analogNoise (rng);
        const float q = 0.9f - 0.6f * c.tone;   // tone: wider → narrower band
        bp.tick (n, digital ? c.pitch * 1.3f : c.pitch, digital ? q * 1.3f : q, sr);
        revBp.tick (n, c.pitch * (digital ? 1.1f : 0.85f), q * 1.4f, sr);
        const float direct = hp.hp (bp.bp * q, 500.0f, sr) * gate * 2.2f;
        const float tail = revLp.lp (revBp.bp * q * 1.4f, 3500.0f, sr) * rev * 1.8f;
        ++t;
        return (direct + tail) * acc * 3.0f;
    }
    bool done() const { return t > (int) (6.0f * sr); }
    void reset() { bp.reset(); revBp.reset(); revLp.reset(); hp.reset(); t = 1 << 30; }
};

// ------------------------------------------------------------------------------------------ toms / perc
/** 1983 tom: diode-rounded triangle with a pitch envelope (Bend, Bend Time) and a little noise. */
struct Tom909
{
    ExpCache ec0;   // per-sample decay factors from fixed times: exp only when they change
    double phase = 0.25;
    float sr = 48000.0f, penv = 0.0f, amp = 0.0f;
    int t = 1 << 30;
    Tpt1 nLp;
    Lfsr lfsr;
    void prepare (float rate) { sr = rate; }
    void trigger (const Ctl& c) { phase = 0.25; penv = 1.0f; amp = 0.8f + 0.2f * c.accent; t = 0; }
    float process (const Ctl& c)
    {
        const float tm = (float) t / sr;
        penv *= ec0 (-1.0f / (std::max (0.003f, c.bendTime) * sr));
        const float hz = c.pitch * (1.0f + 1.5f * c.bend * penv);
        const double d = std::min (0.45, (double) hz / sr);
        phase += d; if (phase >= 1.0) phase -= 1.0;
        const float env = std::exp (-6.9f * tm / std::max (0.03f, c.decay));
        const float body = diodeRound (bandTri (phase, d), 0.15f * c.tone);
        const float nz = nLp.lp (lfsr.next(), 2500.0f + 4000.0f * c.tone, sr) * std::exp (-tm / 0.03f) * 0.25f * c.click;
        ++t;
        return (body * env + nz) * amp;
    }
    bool done() const { return t > (int) (8.0f * sr); }
    void reset() { t = 1 << 30; nLp.reset(); }
};

/** 1980 tom / conga / rim / clave: a bridged-T ping (nearly pure, slight level-dependent sag) with an
    optional upper mode (Tone; ratio 3.6 = rim shot) and a high-passed noise tick (Click). */
struct Ping808
{
    ExpCache ec0;   // per-sample decay factors from fixed times: exp only when they change
    BridgedT main, upper;
    Svf nHp;
    Rng rng { 1980u };
    float sr = 48000.0f, nEnv = 0.0f;
    void prepare (float rate) { sr = rate; main.sr = upper.sr = rate; main.gain = 1.0f; upper.gain = 1.0f; }
    void trigger (const Ctl& c) { const float a = 0.6f + 0.4f * c.accent; main.trigger (a); upper.trigger (a); nEnv = a; }
    float process (const Ctl& c, float upperRatio = 3.6f)
    {
        nEnv *= ec0 (-1.0f / (0.004f * sr));
        const float m = main.process (c.pitch, c.decay, 0.12f, 0.03f * c.bend);
        const float u = c.tone > 0.01f ? upper.process (c.pitch * upperRatio, 0.35f * c.decay, 0.05f, 0.0f) * c.tone * (upperRatio > 3.0f ? 2.5f : 0.35f) : 0.0f;
        nHp.tick (analogNoise (rng), 3000.0f, 1.0f, sr);
        return m + u + nHp.hp * nEnv * c.click * 0.5f;
    }
    bool done() const { return main.follower < 2.0e-5f && upper.follower < 2.0e-5f && main.t > (int) (0.01f * sr); }
    void reset() { main.reset(); upper.reset(); nHp.reset(); }
};

/** Cowbell: two square oscillators at ~1 : 1.48 through a band-pass, with a fast drop then a long tail. */
struct Cowbell
{
    ExpCache ec0, ec1;   // per-sample decay factors from fixed times: exp only when they change
    double p1 = 0.0, p2 = 0.0;
    Svf bp;
    Tpt1 hp;
    float sr = 48000.0f, fast = 0.0f, slow = 0.0f;
    void prepare (float rate) { sr = rate; }
    void trigger (const Ctl& c) { const float a = 0.75f + 0.25f * c.accent; fast = a; slow = a * 0.32f; }
    float process (const Ctl& c)
    {
        const double d1 = std::min (0.45, (double) c.pitch / sr), d2 = std::min (0.45, (double) c.pitch * 1.476 / sr);
        p1 += d1; if (p1 >= 1.0) p1 -= 1.0;
        p2 += d2; if (p2 >= 1.0) p2 -= 1.0;
        const float s = 0.5f * (blepSquare (p1, d1) + blepSquare (p2, d2));
        bp.tick (s, 900.0f + 1800.0f * c.tone, 0.9f, sr);
        fast *= ec0 (-1.0f / (0.012f * sr));
        slow *= ec1 (-6.9f / (std::max (0.05f, 2.5f * c.decay) * sr));
        return hp.hp (bp.bp, 350.0f, sr) * (fast + slow) * 1.6f;
    }
    bool done() const { return fast + slow < 1.0e-4f; }
    void reset() { bp.reset(); hp.reset(); fast = slow = 0.0f; }
};

// ------------------------------------------------------------------------------------------ metal
/** 1980 metal: six square oscillators at the circuit's frequencies (scaled by Pitch / 420 Hz), summed
    into two band-passes (3.44 kHz, 7.1 kHz). Hats: both bands through one envelope and a high-pass.
    Cymbal: three bands with their own envelopes; band 3 through a resonant high-pass near 10.5 kHz. */
struct Metal808
{
    ExpCache ec0, ec1, ec2, ec3;   // per-sample decay factors from fixed times: exp only when they change
    static constexpr double kHz[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    double ph[6] {};
    Svf bp1, bp2, hp1, hp2, hp3;
    float sr = 48000.0f, e1 = 0.0f, e2 = 0.0f, e3 = 0.0f;
    int t = 1 << 30;
    bool cymbal = false;
    void prepare (float rate, Rng& r) { sr = rate; for (auto& p : ph) p = r.unipolar(); }
    void trigger (const Ctl& c) { const float a = 0.7f + 0.3f * c.accent; e1 = e2 = e3 = a; t = 0; }
    float process (const Ctl& c)
    {
        const double scale = c.pitch / 420.0;
        float sum = 0.0f;
        for (int i = 0; i < 6; ++i)
        {
            const double d = std::min (0.45, kHz[i] * scale / sr);
            ph[i] += d; if (ph[i] >= 1.0) ph[i] -= 1.0;
            sum += blepSquare (ph[i], d);
        }
        sum *= 1.0f / 6.0f;
        bp1.tick (sum, 3440.0f, 0.55f, sr);
        bp2.tick (sum, 7100.0f, 0.45f, sr);
        const float a = std::min (1.0f, (float) t / (0.0003f * sr));
        ++t;
        if (! cymbal)
        {
            e1 *= ec0 (-6.9f / (std::max (0.01f, c.decay) * sr));
            // the band-passed cluster plus the squares' own upper harmonics, through a steep high-pass
            const float x = (0.3f * bp1.bp + 0.7f * bp2.bp + 0.35f * sum) * e1 * a;
            hp1.tick (x, 6000.0f + 3000.0f * c.tone, 1.3f, sr);
            hp2.tick (hp1.hp, 7000.0f + 3000.0f * c.tone, 1.1f, sr);
            return hp2.hp * 5.0f;
        }
        // a fast first band and long upper bands: the measured fast drop then ~3 s ring
        e1 *= ec1 (-6.9f / (std::max (0.02f, 0.12f * c.decay) * sr));
        e2 *= ec2 (-6.9f / (std::max (0.02f, c.decay) * sr));
        e3 *= ec3 (-6.9f / (std::max (0.02f, 0.45f * c.decay) * sr));
        hp1.tick (bp1.bp * e1 * a, 2500.0f, 1.4f, sr);
        hp2.tick (bp2.bp * e2 * a, 5500.0f, 1.4f, sr);
        hp3.tick (bp2.bp * e3 * a, 10500.0f, 0.6f, sr);
        return (1.2f * hp1.hp + 0.7f * hp2.hp + (0.4f + 1.4f * c.tone) * hp3.hp) * 1.05f;
    }
    float level() const { return cymbal ? std::max ({ e1, e2, e3 }) : e1; }
    bool done() const { return t > (int) (0.005f * sr) && level() < 1.0e-4f; }
    void reset() { bp1.reset(); bp2.reset(); hp1.reset(); hp2.reset(); hp3.reset(); e1 = e2 = e3 = 0.0f; }
};


// ------------------------------------------------------------------------------------------ layered
/** Two-layer DSP voice: each layer is a source (sine, saw, HP / LP / BP noise whose filter follows the
    layer pitch), a pitch modulator (fall, rise, oscillate) and an amp envelope (linear, exponential,
    multi-peak). Bend = modulation amount, Bend Time = modulation time / rate. */
struct Layered
{
    double ph[2] {};
    Svf filt[2];
    Rng rng { 6000u };
    float sr = 48000.0f, acc = 1.0f, lastEnv = 0.0f;
    int t = 1 << 30;
    void prepare (float rate) { sr = rate; }
    void trigger (const Ctl& c) { t = 0; acc = 0.8f + 0.2f * c.accent; ph[0] = ph[1] = 0.0; lastEnv = 1.0f; }
    float env (int type, float tm, float dec)
    {
        const float att = 0.0015f;
        if (type == 0) return tm < att ? tm / att : std::max (0.0f, 1.0f - (tm - att) / std::max (0.01f, dec));   // linear
        if (type == 1) return std::min (1.0f, tm / att) * std::exp (-6.9f * tm / std::max (0.01f, dec));      // exponential
        const float sp = 0.011f;                                                                                // multi-peak
        const int k = std::min (2, (int) (tm / sp));
        const float local = tm - (float) k * sp;
        return k < 2 ? std::exp (-local / 0.0035f) : std::exp (-6.9f * local / std::max (0.01f, dec));
    }
    float process (const Ctl& c)
    {
        const float tm = (float) t / sr;
        float mod;
        const float mt = std::max (0.003f, c.bendTime);
        if (c.layMod == 0) mod = std::exp (-tm / mt);
        else if (c.layMod == 1) mod = -std::exp (-tm / mt);
        else mod = std::sin (kTwoPi * tm / (4.0f * mt));
        const float pm = std::exp2 (2.5f * c.bend * mod);
        auto layer = [&] (int i, int src, float hz)
        {
            const double d = std::min (0.45, (double) hz / sr);
            ph[i] += d; if (ph[i] >= 1.0) ph[i] -= 1.0;
            switch (src)
            {
                case 0: return std::sin (kTwoPi * (float) ph[i]);
                case 1: return (float) (2.0 * ph[i] - 1.0) - polyBlep ((float) ph[i], (float) d);
                case 2: filt[i].tick (analogNoise (rng), clamp (hz * 8.0f, 400.0f, 16000.0f), 1.2f, sr); return filt[i].hp;
                case 3: filt[i].tick (analogNoise (rng), clamp (hz * 4.0f, 60.0f, 14000.0f), 1.2f, sr); return filt[i].lp;
                default: filt[i].tick (analogNoise (rng), clamp (hz * 6.0f, 100.0f, 14000.0f), 0.5f - 0.35f * c.tone, sr); return filt[i].bp * 1.5f;
            }
        };
        const float e = env (c.layEnv, tm, c.decay);
        lastEnv = e;
        const float a = layer (0, c.layA, c.pitch * pm);
        const float b = layer (1, c.layB, c.pitch * c.layRatio * pm);
        ++t;
        return (a * (1.0f - c.layMix) + b * c.layMix) * e * acc;
    }
    bool done() const { return t > (int) (0.05f * sr) && lastEnv < 1.0e-4f; }
    void reset() { filt[0].reset(); filt[1].reset(); t = 1 << 30; }
};

// ------------------------------------------------------------------------------------------ beat box (analog)
/** Beat box kick (analog part of a 2013 compact beat box): a resonator rung by the trigger, a Click
    pulse mixed in, then a low-pass. Simpler than the 1980 circuit: no attack shift, no sigh. */
struct BoxKick
{
    Svf res;
    Tpt1 lp, clickHp, outHp;
    float sr = 48000.0f, pulseAmp = 0.0f, fol = 0.0f;
    int t = 1 << 30;
    void prepare (float rate) { sr = rate; }
    void trigger (const Ctl& c) { t = 0; pulseAmp = 0.7f + 0.3f * c.accent; }
    float process (const Ctl& c)
    {
        const float pulse = t < (int) (0.0007f * sr) ? pulseAmp : 0.0f;
        const float hz = c.pitch * (1.0f + 0.35f * c.bend * std::exp (-(float) t / (0.015f * sr)));
        res.tick (pulse, hz, dampFor (hz, c.decay), sr);
        const float body = res.bp / (kTwoPi * hz * 0.0007f) * 0.7f;
        const float clk = clickHp.hp (pulse, 600.0f, sr) * c.click * 0.8f;
        fol = std::max (std::abs (body), fol * 0.9995f);
        ++t;
        return outHp.hp (lp.lp (fastTanh (body + clk), 300.0f * std::pow (25.0f, c.tone), sr), 25.0f, sr);
    }
    bool done() const { return t > (int) (0.02f * sr) && fol < 2.0e-5f; }
    void reset() { res.reset(); lp.reset(); clickHp.reset(); outHp.reset(); fol = 0.0f; t = 1 << 30; }
};

/** Beat box snare: a resonator body plus noise through a resonant band-pass (Snappy = its level). */
struct BoxSnare
{
    ExpCache ec0;   // per-sample decay factors from fixed times: exp only when they change
    Svf res, nbp;
    Rng rng { 2013u };
    float sr = 48000.0f, pulseAmp = 0.0f, nEnv = 0.0f, fol = 0.0f;
    int t = 1 << 30;
    void prepare (float rate) { sr = rate; }
    void trigger (const Ctl& c) { t = 0; pulseAmp = 0.7f + 0.3f * c.accent; nEnv = pulseAmp; }
    float process (const Ctl& c)
    {
        const float pulse = t < (int) (0.0006f * sr) ? pulseAmp : 0.0f;
        res.tick (pulse, c.pitch, dampFor (c.pitch, 0.6f * c.decay + 0.03f), sr);
        const float body = res.bp / (kTwoPi * c.pitch * 0.0006f) * 0.6f;
        nEnv *= ec0 (-6.9f / ((0.08f + 0.5f * c.decay) * sr));
        nbp.tick (analogNoise (rng), 2500.0f + 4000.0f * c.tone, 0.7f, sr);
        fol = std::max (std::abs (body), fol * 0.9995f);
        ++t;
        return 0.7f * body + nbp.bp * 0.7f * nEnv * 2.0f * c.click;
    }
    bool done() const { return t > (int) (0.02f * sr) && fol < 2.0e-5f && nEnv < 1.0e-4f; }
    void reset() { res.reset(); nbp.reset(); t = 1 << 30; fol = nEnv = 0.0f; }
};

/** Beat box tom: a resonator rung through a band-pass, with a short swept start. */
struct BoxTom
{
    Svf res, bp;
    float sr = 48000.0f, pulseAmp = 0.0f, fol = 0.0f;
    int t = 1 << 30;
    void prepare (float rate) { sr = rate; }
    void trigger (const Ctl& c) { t = 0; pulseAmp = 0.7f + 0.3f * c.accent; }
    float process (const Ctl& c)
    {
        const float pulse = t < (int) (0.0006f * sr) ? pulseAmp : 0.0f;
        const float hz = c.pitch * (1.0f + 0.25f * c.bend * std::exp (-(float) t / (0.02f * sr)));
        res.tick (pulse, hz, dampFor (hz, c.decay), sr);
        const float body = res.bp / (kTwoPi * hz * 0.0006f) * 0.6f;
        bp.tick (body, hz * 1.4f, 1.0f - 0.6f * c.tone, sr);
        fol = std::max (std::abs (body), fol * 0.9995f);
        ++t;
        return 0.6f * body + 0.6f * bp.bp;
    }
    bool done() const { return t > (int) (0.02f * sr) && fol < 2.0e-5f; }
    void reset() { res.reset(); bp.reset(); t = 1 << 30; fol = 0.0f; }
};

/** Beat box hat: six oscillators (a cluster of its own, tighter than the 1980 one) into a resonant
    band-pass whose ring gives the "grain"; Pitch moves the cluster, Tone the resonance. */
struct BoxHat
{
    ExpCache ec0;   // per-sample decay factors from fixed times: exp only when they change
    static constexpr double kRatio[6] = { 1.0, 1.304, 1.466, 1.718, 2.053, 2.386 };
    double ph[6] {};
    Svf bp, hp;
    float sr = 48000.0f, env = 0.0f;
    void prepare (float rate, Rng& r) { sr = rate; for (auto& p : ph) p = r.unipolar(); }
    void trigger (const Ctl& c) { env = 0.75f + 0.25f * c.accent; }
    float process (const Ctl& c)
    {
        float sum = 0.0f;
        for (int i = 0; i < 6; ++i)
        {
            const double d = std::min (0.45, c.pitch * 1.25 * kRatio[i] / sr);
            ph[i] += d; if (ph[i] >= 1.0) ph[i] -= 1.0;
            sum += blepSquare (ph[i], d);
        }
        env *= ec0 (-6.9f / (std::max (0.01f, c.decay) * sr));
        bp.tick (sum / 6.0f, 8200.0f, 0.5f - 0.38f * c.tone, sr);
        hp.tick (bp.bp, 6500.0f, 1.3f, sr);
        return hp.hp * env * 4.0f;
    }
    bool done() const { return env < 1.0e-4f; }
    void reset() { bp.reset(); hp.reset(); env = 0.0f; }
};

// ------------------------------------------------------------------------------------------ PCM parts
/** A short recording played back like the PCM voices of the reference machines: speed changes pitch
    and length together (Pitch / reference pitch), a VCA envelope (Decay) and the stored bit depth. */
struct PcmVoice
{
    ExpCache ec0;   // per-sample decay factors from fixed times: exp only when they change
    const std::vector<float>* buf = nullptr;
    double pos = 1.0e12;
    float env = 0.0f, acc = 1.0f, sr = 48000.0f;
    void prepare (float rate) { sr = rate; }
    void trigger (const Ctl& c) { pos = 0.0; env = 1.0f; acc = 0.75f + 0.25f * c.accent; }
    float process (const Ctl& c, float refPitch)
    {
        if (buf == nullptr || pos >= (double) buf->size() - 2.0) { env = 0.0f; return 0.0f; }
        const int i = (int) pos;
        const float f = (float) (pos - i);
        const float x = (*buf)[(size_t) i] + ((*buf)[(size_t) i + 1] - (*buf)[(size_t) i]) * f;
        pos += std::clamp (c.pitch / refPitch, 0.25f, 4.0f);
        env *= ec0 (-6.9f / (std::max (0.02f, c.decay) * sr));
        return x * env * acc;
    }
    bool done() const { return env < 1.0e-4f || buf == nullptr || pos >= (double) buf->size() - 2.0; }
    void reset() { pos = 1.0e12; env = 0.0f; }
};

/** Builds the stored recordings (done once in prepare, never on the audio thread). */
namespace pcm
{
inline void quantise (std::vector<float>& b, int bits)
{
    float pk = 1.0e-9f;
    for (float v : b) pk = std::max (pk, std::abs (v));
    const float q = std::exp2 ((float) bits - 1.0f);
    for (auto& v : b) v = std::round (v / pk * 0.95f * q) / q;
}
/** Inharmonic metal: many partials (log-spread over [lo, hi] Hz) with their own decays, plus noise. */
inline std::vector<float> metal (float sr, float seconds, float lo, float hi, int partials, float tMin, float tMax,
                                 float noiseAmt, uint32_t seed)
{
    std::vector<float> b ((size_t) (seconds * sr), 0.0f);
    Rng r (seed);
    for (int k = 0; k < partials; ++k)
    {
        const float f = lo * std::pow (hi / lo, r.unipolar());
        const float t60 = tMin + (tMax - tMin) * r.unipolar() * r.unipolar();
        const float amp = (0.4f + 0.6f * r.unipolar()) / std::sqrt ((float) partials);
        const float w = kTwoPi * f / sr, cw = 2.0f * std::cos (w), dec = t60Coef (t60, sr);
        float y1 = std::sin (r.unipolar() * kTwoPi), y2 = std::sin (r.unipolar() * kTwoPi - w), g = amp;
        for (auto& v : b) { const float y = cw * y1 - y2; y2 = y1; y1 = y; v += y * g; g *= dec; }
    }
    Svf hp; float g = noiseAmt;
    const float nd = t60Coef (tMax * 0.5f, sr);
    for (auto& v : b) { hp.tick (analogNoise (r), lo, 1.2f, sr); v += hp.hp * g; g *= nd; }
    return b;
}
} // namespace pcm
} // namespace mb::dsp::drum
