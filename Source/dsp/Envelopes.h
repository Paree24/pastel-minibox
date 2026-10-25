#pragma once
#include "DspCommon.h"

namespace mb::dsp
{
/** Analog-style ADSR with RC curves. Times are seconds; the release always reaches idle. */
struct Adsr
{
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle;
    float value = 0.0f, sr = 48000.0f;
    float aCoef = 0, dCoef = 0, rCoef = 0, sus = 1.0f;

    void prepare (float rate) { sr = rate; }
    static float coef (float seconds, float sr, float ratio)
    {
        seconds = std::max (seconds, 0.0005f);
        return std::exp (-std::log ((1.0f + ratio) / ratio) / (seconds * sr));
    }
    void set (float a, float d, float s, float r)
    {
        aCoef = coef (a, sr, 0.3f);
        dCoef = coef (d, sr, 0.0001f);
        rCoef = coef (r, sr, 0.0001f);
        sus = clamp (s, 0.0f, 1.0f);
    }
    void gateOn (bool legato = false)
    {
        if (legato && stage != Idle && stage != Release) return;
        stage = Attack;   // retrigger continues from the current value: no dip, no click
    }
    void gateOff() { if (stage != Idle) stage = Release; }
    void kill() { stage = Idle; value = 0.0f; }
    bool active() const { return stage != Idle; }

    float process()
    {
        switch (stage)
        {
            case Attack:
                value = 1.3f + (value - 1.3f) * aCoef;
                if (value >= 1.0f) { value = 1.0f; stage = Decay; }
                break;
            case Decay:
                value = sus + (value - sus) * dCoef;
                if (std::abs (value - sus) < 1.0e-4f) { value = sus; stage = Sustain; }
                break;
            case Sustain: value += (sus - value) * 0.002f; break;   // follows live sustain edits
            case Release:
                value = -0.0001f + (value + 0.0001f) * rCoef;
                if (value <= 0.0f) { value = 0.0f; stage = Idle; }
                break;
            case Idle: value = 0.0f; break;
        }
        return value;
    }
};

/** Attack-free decay with an optional accent "sweep capacitor": consecutive accents charge it
    further, and it drains at its own rate. */
struct DecayEnv
{
    float value = 0.0f, coef = 0.999f, sr = 48000.0f;
    float sweep = 0.0f, sweepCoef = 0.9995f;
    void prepare (float rate) { sr = rate; }
    void setDecay (float seconds) { coef = std::exp (-1.0f / (std::max (seconds, 0.002f) * sr)); }
    void setSweepDecay (float seconds) { sweepCoef = std::exp (-1.0f / (std::max (seconds, 0.01f) * sr)); }
    void trigger (bool accent, float sweepAmount)
    {
        value = 1.0f;
        if (accent) sweep = std::min (1.6f, sweep + sweepAmount);
    }
    float process() { value *= coef; sweep *= sweepCoef; return value; }
};

/** Four rates and four levels (6-operator style). Level 4 is the release target. */
struct RateLevelEnv
{
    float rates[4] { 0.9f, 0.6f, 0.5f, 0.6f };   // 0..1, higher = faster
    float levels[4] { 1.0f, 0.8f, 0.7f, 0.0f };  // 0..1
    int stage = -1;  // -1 idle, 0..2 attack-to-sustain, 3 release
    float value = 0.0f, sr = 48000.0f, rateScale = 1.0f;

    void prepare (float rate) { sr = rate; }
    static float rateToSeconds (float r) { return 0.0015f * std::pow (2.0f, (1.0f - clamp (r, 0.0f, 1.0f)) * 14.5f); }
    void gateOn() { stage = 0; }
    void gateOff() { if (stage >= 0) stage = 3; }
    bool active() const { return stage >= 0; }
    void kill() { stage = -1; value = 0.0f; }

    float process()
    {
        if (stage < 0) return 0.0f;
        const float target = levels[stage];
        const float secs = rateToSeconds (rates[stage]) / rateScale;
        const float rising = target > value;
        if (rising)
        {
            value += (1.0f / (secs * sr)) * 1.2f;
            if (value >= target) { value = target; if (stage < 2) ++stage; }
        }
        else
        {
            // decays are linear in dB
            const float dbPerSample = 96.0f / (secs * sr);
            float db = gainToDb (std::max (value, 1.0e-5f)) - dbPerSample;
            const float tdb = gainToDb (std::max (target, 1.0e-5f));
            if (db <= tdb || value - target < 1.0e-5f)
            {
                value = target;
                if (stage < 2) ++stage;
                else if (stage == 3 && target <= 1.0e-4f) { stage = -1; value = 0.0f; }
            }
            else value = dbToGain (db);
        }
        return value;
    }
};

/** LFO: tri, saw down, saw up, square, sample-and-hold, noise, sine. Free or synced, with delay
    (fade-in) and key trigger. The output is slewed slightly so square edges never click. */
struct Lfo
{
    enum Shape { Tri, SawDown, SawUp, Square, SampleHold, SmoothNoise, Sine };
    float phase = 0.0f, inc = 0.0f, held = 0.0f, prevHeld = 0.0f, out = 0.0f;
    float fade = 1.0f, fadeInc = 1.0f, sr = 48000.0f;
    Rng rng { 12345u };
    OnePole slew;

    void prepare (float rate) { sr = rate; slew.setCutoff (180.0f, rate); }
    void setRate (float hz) { inc = clamp (hz, 0.0f, 80.0f) / sr; }
    void setDelay (float seconds) { fadeInc = seconds <= 0.001f ? 1.0f : 1.0f / (seconds * sr); }
    void retrigger() { phase = 0.0f; fade = fadeInc >= 1.0f ? 1.0f : 0.0f; held = rng.bipolar(); }
    void syncTo (double beatsPos, double beatsPerCycle)
    {
        const double p = beatsPos / std::max (beatsPerCycle, 1.0e-6);
        phase = (float) (p - std::floor (p));
    }

    float process (int shape)
    {
        phase += inc;
        if (phase >= 1.0f) { phase -= 1.0f; prevHeld = held; held = rng.bipolar(); }
        float v;
        switch (shape)
        {
            case SawDown: v = 1.0f - 2.0f * phase; break;
            case SawUp: v = 2.0f * phase - 1.0f; break;
            case Square: v = phase < 0.5f ? 1.0f : -1.0f; break;
            case SampleHold: v = held; break;
            case SmoothNoise: v = lerp (prevHeld, held, phase * phase * (3.0f - 2.0f * phase)); break;
            case Sine: v = std::sin (kTwoPi * phase); break;
            default: v = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase; break;
        }
        if (fade < 1.0f) fade = std::min (1.0f, fade + fadeInc);
        out = slew.lp (v) * fade;
        return out;
    }
};
} // namespace mb::dsp
