#pragma once
#include "DspCommon.h"

namespace mb::dsp
{
/** Soft (tanh), tube (asymmetric with bias) and wavefold drive with a tone low-pass and mix. */
struct Drive
{
    enum Type { Soft, Tube, Fold };
    OnePole tone;
    DcBlock dc;
    float sr = 48000.0f;
    void prepare (float rate) { sr = rate; dc.prepare (rate); }
    void setTone (float t) { tone.setCutoff (800.0f * std::pow (22.0f, clamp (t, 0.0f, 1.0f)), sr); }
    float process (float x, int type, float amount, float mix)
    {
        if (amount <= 0.0005f && mix <= 0.0005f) return x;
        const float g = 1.0f + amount * 14.0f;
        float y;
        switch (type)
        {
            case Tube: y = fastTanh (x * g + 0.35f) - fastTanh (0.35f); y = dc.process (y); break;
            case Fold: y = std::sin (clamp (x * g * 0.8f, -12.0f, 12.0f)); break;
            default: y = fastTanh (x * g); break;
        }
        y = tone.lp (y) / (1.0f + amount * 0.6f);
        return x + (y - x) * mix;
    }
};

/** Drive on a stereo signal: each channel its own tone filter and DC blocker (one shared Drive would run them on
    an interleaved L, R, L, R stream). */
struct StereoDrive
{
    Drive l, r;
    void prepare (float rate) { l.prepare (rate); r.prepare (rate); }
    void setTone (float t) { l.setTone (t); r.setTone (t); }
    void process (float& L, float& R, int type, float amount, float mix) { L = l.process (L, type, amount, mix); R = r.process (R, type, amount, mix); }
};

/** Fractional delay line with integer index wrapping (a float index can round onto `size`). */
struct DelayLine
{
    std::vector<float> buf;
    int w = 0, size = 1;
    void prepare (int samples) { size = std::max (4, samples); buf.assign ((size_t) size, 0.0f); w = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x) { buf[(size_t) w] = x; if (++w >= size) w = 0; }
    float read (float delaySamples) const
    {
        delaySamples = clamp (delaySamples, 1.0f, (float) size - 3.0f);
        float rp = (float) w - delaySamples;
        while (rp < 0.0f) rp += (float) size;
        int i0 = (int) rp;
        const float f = rp - (float) i0;
        if (i0 >= size) i0 -= size;
        const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
        return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * f;
    }
};

/** Dual bucket-brigade style chorus: modes I, II, I+II with HF roll-off and faint noise. */
struct BbdChorus
{
    DelayLine dl, dr;
    float phase = 0.0f, sr = 48000.0f;
    OnePole lpL, lpR;
    Smoother depthSm, rateSm;
    Rng rng { 777u };
    void prepare (float rate)
    {
        sr = rate; dl.prepare ((int) (0.04f * rate)); dr.prepare ((int) (0.04f * rate));
        lpL.setCutoff (7500.0f, rate); lpR.setCutoff (7500.0f, rate);
        depthSm.setTime (60.0f, rate); rateSm.setTime (60.0f, rate);
    }
    void reset() { dl.clear(); dr.clear(); }
    /** mode 0 off, 1 = I, 2 = II, 3 = I+II */
    void process (float& l, float& r, int mode, float mix, float rateScale = 1.0f, float depthScale = 1.0f)
    {
        static constexpr float rates[4] = { 0.0f, 0.5f, 0.83f, 7.5f };
        static constexpr float depths[4] = { 0.0f, 1.6f, 2.4f, 0.35f };
        const int m = std::clamp (mode, 0, 3);
        const float rt = rateSm.tick (rates[m] * rateScale);
        const float dp = depthSm.tick (depths[m] * depthScale);
        dl.push (l + rng.bipolar() * 1.0e-5f);
        dr.push (r + rng.bipolar() * 1.0e-5f);
        if (m == 0 && dp < 0.01f) return;
        phase += rt / sr; if (phase >= 1.0f) phase -= 1.0f;
        const float lfo = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
        const float base = 0.0035f * sr;
        const float wl = lpL.lp (dl.read (base + lfo * dp * 0.001f * sr));
        const float wr = lpR.lp (dr.read (base - lfo * dp * 0.001f * sr));
        l = l * (1.0f - 0.5f * mix) + wl * mix * 0.85f;
        r = r * (1.0f - 0.5f * mix) + wr * mix * 0.85f;
    }
};

/** Stereo tape-style delay: synced or free time with ~80 ms glide, filtered feedback, wow,
    optional ducking against the dry input. */
struct TapeDelay
{
    DelayLine dl, dr;
    OnePole fbLpL, fbLpR, fbHpL, fbHpR, duckEnv;
    Smoother timeSm;
    float sr = 48000.0f, wow = 0.0f;
    void prepare (float rate, float maxSeconds = 2.5f)
    {
        sr = rate;
        dl.prepare ((int) (maxSeconds * rate) + 8); dr.prepare ((int) (maxSeconds * rate) + 8);
        timeSm.setTime (80.0f, rate); timeSm.reset (0.3f * rate);
        fbHpL.setCutoff (90.0f, rate); fbHpR.setCutoff (90.0f, rate);
        duckEnv.setCutoff (8.0f, rate);
    }
    void reset() { dl.clear(); dr.clear(); }
    void setTone (float t)
    {
        const float hz = 1200.0f * std::pow (12.0f, clamp (t, 0.0f, 1.0f));
        fbLpL.setCutoff (hz, sr); fbLpR.setCutoff (hz, sr);
    }
    /** Adds the wet signal into l/r. `pingPong` crosses the feedback. */
    void process (float inL, float inR, float& outL, float& outR, float seconds, float feedback, float mix,
                  float duck = 0.0f, bool pingPong = true)
    {
        // a synced time beyond the line (2 bars at a slow tempo) halves until it fits: still on the beat grid
        while (seconds > 2.4f) seconds *= 0.5f;
        const float t = timeSm.tick (clamp (seconds, 0.005f, 2.4f) * sr);
        wow += 0.35f / sr; if (wow >= 1.0f) wow -= 1.0f;
        const float mod = std::sin (kTwoPi * wow) * 0.0006f * sr;
        const float yl = dl.read (t + mod), yr = dr.read (t * (pingPong ? 1.0f : 1.003f) + mod);
        const float fb = clamp (feedback, 0.0f, 0.95f);
        const float sl = fbHpL.hp (fbLpL.lp (yl)), sr_ = fbHpR.hp (fbLpR.lp (yr));
        dl.push (flush (inL + (pingPong ? sr_ : sl) * fb));
        dr.push (flush (inR + (pingPong ? sl : sr_) * fb));
        float g = mix;
        if (duck > 0.0f)
        {
            const float e = duckEnv.lp (std::abs (inL) + std::abs (inR));
            g *= 1.0f - clamp (e * 3.0f * duck, 0.0f, 0.9f);
        }
        outL += yl * g; outR += yr * g;
    }
};

/** Plate-style reverb: pre-delay, four input diffusers, then an 8-line feedback delay network
    (Hadamard mixing) whose lines are slowly modulated so the tail never rings metallic, with damping
    in every line and the decay set as an RT60. */
struct PlateReverb
{
    static constexpr int N = 8;
    DelayLine lines[N], pre, diff[4];
    OnePole damp[N], inLp;
    float fbGain = 0.8f, sr = 48000.0f, preSamps = 576.0f, size = 1.0f;
    float lens[N] {}, modC[N] {}, modS[N] {}, modCos[N] {}, modSin[N] {}, diffLen[4] {};
    void prepare (float rate)
    {
        sr = rate;
        static constexpr float ms[N] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f };
        static constexpr float dms[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
        for (int i = 0; i < N; ++i)
        {
            lens[i] = ms[i] * 0.001f * rate;
            lines[i].prepare ((int) (lens[i] * 1.7f) + 64);
            // slow modulation per line as a rotating phasor (no sin() per sample)
            const float w = kTwoPi * (0.11f + 0.07f * (float) i) / rate, ph = kTwoPi * (float) i / N;
            modCos[i] = std::cos (w); modSin[i] = std::sin (w);
            modC[i] = std::cos (ph); modS[i] = std::sin (ph);
        }
        for (int i = 0; i < 4; ++i) { diffLen[i] = dms[i] * 0.001f * rate; diff[i].prepare ((int) diffLen[i] + 8); }
        pre.prepare ((int) (0.11f * rate));
        inLp.setCutoff (9000.0f, rate);
        set (0.6f, 0.5f);
    }
    void reset() { for (auto& l : lines) l.clear(); for (auto& d : diff) d.clear(); pre.clear(); }
    /** decay 0…1 → RT60 0.4…7.4 s; dampAmt 0 bright … 1 dark; sizeAmt 0…1 scales the room (line lengths);
        preSec: pre-delay up to 100 ms. */
    void set (float decay, float dampAmt, float sizeAmt = 1.0f, float preSec = 0.012f)
    {
        size = 0.6f + 0.4f * clamp (sizeAmt, 0.0f, 1.0f);
        const float rt60 = 0.4f + clamp (decay, 0.0f, 1.0f) * 7.0f;
        // per pass the signal travels a line's mean length (50.7 ms) as the lines are read: scaled by 0.6 + 0.4·size
        fbGain = std::pow (10.0f, -3.0f * 0.0507f * (0.6f + 0.4f * size) / rt60);
        for (auto& d : damp) d.setCutoff (12000.0f - 9000.0f * clamp (dampAmt, 0.0f, 1.0f), sr);
        preSamps = std::max (1.0f, clamp (preSec, 0.0f, 0.1f) * sr);
    }
    void process (float inL, float inR, float& outL, float& outR, float mix)
    {
        pre.push (inLp.lp (0.5f * (inL + inR)));
        float x = pre.read (preSamps);
        for (int i = 0; i < 4; ++i)   // all-pass diffusion: smears the attack into a dense wash
        {
            const float d = diff[i].read (diffLen[i]);
            const float v = x + 0.62f * d;
            diff[i].push (flush (v));
            x = d - 0.62f * v;
        }
        float o[N];
        const float depth = 0.0004f * sr;
        for (int i = 0; i < N; ++i)
        {
            const float c = modC[i] * modCos[i] - modS[i] * modSin[i], sn = modS[i] * modCos[i] + modC[i] * modSin[i];
            const float norm = 1.5f - 0.5f * (c * c + sn * sn);   // keeps the phasor on the unit circle
            modC[i] = c * norm; modS[i] = sn * norm;
            o[i] = lines[i].read (lens[i] * (0.6f + 0.4f * size) + depth * modS[i]);
        }
        // Hadamard mixing as a fast Walsh–Hadamard transform (3 butterfly stages)
        float m[N];
        for (int i = 0; i < N; ++i) m[i] = o[i];
        for (int len = 1; len < N; len <<= 1)
            for (int i = 0; i < N; i += len << 1)
                for (int j = i; j < i + len; ++j) { const float a = m[j], b = m[j + len]; m[j] = a + b; m[j + len] = a - b; }
        for (auto& v : m) v *= 0.35355339f;
        for (int i = 0; i < N; ++i)
            lines[i].push (flush (damp[i].lp (m[i] * fbGain) + (i & 1 ? x : -x) * 0.6f));
        const float l = o[0] + o[2] + o[4] + o[6], r = o[1] + o[3] + o[5] + o[7];
        outL += l * 0.25f * mix;
        outR += r * 0.25f * mix;
    }
};

/** Hermite interpolation and a vintage sample-rate/bit-depth reducer. */
struct Resampler
{
    static float hermite (const float* d, int n, double pos)
    {
        const int i = (int) pos;
        const float f = (float) (pos - i);
        auto at = [&] (int k) { k = std::clamp (k, 0, n - 1); return d[k]; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c = (x1 - xm1) * 0.5f;
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + (x2 - x0) * 0.5f;
        const float b = w + a;
        return ((a * f - b) * f + c) * f + x0;
    }

    float held = 0.0f, acc = 1.0f;
    /** Vintage character: holds samples at `rateHz` and quantises to `bits`. */
    float vintage (float x, float rateHz, float bits, float sr)
    {
        acc += rateHz / sr;
        if (acc >= 1.0f) { acc -= std::floor (acc); held = x; }
        const float q = std::exp2 (clamp (bits, 2.0f, 16.0f) - 1.0f);
        return std::round (held * q) / q;
    }
};

/** Tube or string resonator: a tuned delay line with a damping filter in the loop. */
struct Waveguide
{
    DelayLine line;
    OnePole dampLp;
    DcBlock dc;
    float sr = 48000.0f;
    void prepare (float rate) { sr = rate; line.prepare ((int) (rate / 20.0f) + 8); dc.prepare (rate); }
    void reset() { line.clear(); }
    /** type 0 = tube (inverting reflection, odd harmonics), 1 = string. */
    float process (float x, float hz, float decay, float dampAmt, int type)
    {
        const float period = sr / clamp (hz, 30.0f, 2000.0f);
        const float len = type == 0 ? period * 0.5f : period;
        dampLp.setCutoff (16000.0f * std::pow (0.04f, clamp (dampAmt, 0.0f, 1.0f)), sr);
        const float y = line.read (len);
        const float fb = (type == 0 ? -1.0f : 1.0f) * (0.80f + 0.195f * clamp (decay, 0.0f, 1.0f));
        line.push (flush (fastTanh (x + dampLp.lp (y) * fb)));
        return dc.process (y);
    }
};

/** RMS bus compressor with soft knee and auto make-up. */
struct GlueComp
{
    float env = 0.0f, gainDb = 0.0f, aCoef = 0, rCoef = 0, sr = 48000.0f, reduction = 0.0f;
    void prepare (float rate)
    {
        sr = rate;
        aCoef = std::exp (-1.0f / (0.010f * rate));
        rCoef = std::exp (-1.0f / (0.150f * rate));
    }
    void process (float& l, float& r, float amount)
    {
        if (amount <= 0.001f) { reduction = 0.0f; return; }
        const float p = 0.5f * (l * l + r * r);
        env = p > env ? p + (env - p) * aCoef : p + (env - p) * rCoef;
        const float db = 10.0f * std::log10 (env + 1.0e-12f);
        const float thr = -8.0f - 22.0f * amount, ratio = 2.0f + 4.0f * amount, knee = 6.0f;
        float over = db - thr, red = 0.0f;
        if (over > knee * 0.5f) red = over * (1.0f - 1.0f / ratio);
        else if (over > -knee * 0.5f) { const float o = over + knee * 0.5f; red = (1.0f - 1.0f / ratio) * o * o / (2.0f * knee); }
        reduction = red;
        const float makeup = 4.0f * amount;
        const float g = dbToGain (makeup - red);
        l *= g; r *= g;
    }
};

/** Stereo-linked peak limiter. With look-ahead the gain ramps down smoothly over the look-ahead window
    so it has reached its target when the peak arrives (no gain steps, no clamping); without it the
    gain still slews over ~0.3 ms. A soft knee above the ceiling catches what is left, never a hard clip. */
struct Limiter
{
    DelayLine dl, dr;
    std::vector<float> peaks;
    int la = 0, pi = 0;
    float gain = 1.0f, target = 1.0f, aCoef = 0.0f, rCoef = 0.0f, sr = 48000.0f, reduction = 0.0f;
    void prepare (float rate, bool lookAhead)
    {
        sr = rate;
        la = lookAhead ? (int) (0.0015f * rate) : 0;
        dl.prepare (la + 8); dr.prepare (la + 8);
        peaks.assign ((size_t) std::max (1, la), 1.0f);
        aCoef = std::exp (-1.0f / (std::max (1.0f, (float) la / 4.0f + 0.0003f * rate)));
        rCoef = std::exp (-1.0f / (0.12f * rate));
        gain = target = 1.0f;
    }
    int latency() const { return la; }
    static float softCeil (float x, float c)
    {
        // a safety net for what the gain stage missed: only the top 2 % below the ceiling bends (from 90 % it
        // coloured every loud mix, limiting or not)
        const float knee = 0.98f * c, a = std::abs (x);
        if (a <= knee) return x;
        return std::copysign (knee + (c - knee) * fastTanh ((a - knee) / (c - knee)), x);
    }
    void process (float& l, float& r, float ceilingDb)
    {
        const float ceil = dbToGain (ceilingDb);
        const float pk = std::max (std::abs (l), std::abs (r));
        float need = pk > ceil ? ceil / pk : 1.0f;
        if (la > 0)
        {
            peaks[(size_t) pi] = need; pi = (pi + 1) % la;
            for (float p : peaks) need = std::min (need, p);
            dl.push (l); dr.push (r);
            l = dl.read ((float) la); r = dr.read ((float) la);
        }
        // smooth gain: fast (over the look-ahead) going down, slow coming back up
        gain = need < gain ? need + (gain - need) * aCoef : need + (gain - need) * rCoef;
        reduction = -gainToDb (gain);
        l = softCeil (l * gain, ceil); r = softCeil (r * gain, ceil);
    }
};
} // namespace mb::dsp
