#pragma once
#include "DspCommon.h"
#include "Analog.h"

/*  The bass-line voice's filter, written from the circuit (docs/research/synths.md):
    a diode ladder whose input is a differential pair feeding current into the bottom node; four
    capacitor nodes (the bottom one half the value of the others) coupled through diode pairs, the top
    node returning to ground through the top pair. Its small-signal transfer function is
        H(s) = 1 / (s⁴ + 6.727 s³ + 14.142 s² + 9.514 s + 1)    (s normalised to ωc = 1.1892 g/C),
    the resonant peak sits at 1.189 ωc and the loop self-oscillates at a feedback gain of 17.0.
    Every branch carries a tanh current (diodes / the input pair saturate); the resonance loop runs
    through the coupling high-pass that thins the bass as resonance rises. Solved implicitly
    (trapezoidal, each tanh linearised at the previous sample's operating point). */

namespace mb::dsp::bass
{
struct DiodeLadder
{
    static constexpr double kCritical = 17.0;    // feedback gain where the ladder starts to sing
    static constexpr double kPeakRatio = 1.1893; // resonant peak / ωc
    double s[4] {}, dIn = 0.0, d12 = 0.0, d23 = 0.0, d34 = 0.0, d4 = 0.0;
    double c[4] {}, k = 0.0, makeup = 1.0, gHp = 0.0, zHp = 0.0, fs = 192000.0;
    uint32_t noise = 0x2468ace1u;

    void prepare (double rate) { fs = rate; reset(); }
    void reset() { for (auto& v : s) v = 0.0; dIn = d12 = d23 = d34 = d4 = zHp = 0.0; }

    /** peakHz: where the resonance sings; res01: 0…1 (≈ 0.97 starts self-oscillation); fbHpHz: the
        coupling high-pass inside the resonance loop. */
    void set (double peakHz, double res01, double fbHpHz)
    {
        const double wc = 2.0 * 3.14159265358979 * std::clamp (peakHz, 10.0, 0.4 * fs) / kPeakRatio;
        const double wd = 2.0 * fs * std::tan (wc / (2.0 * fs));          // pre-warped
        const double gU = wd / 1.1892;                                    // g / C
        static constexpr double C[4] = { 0.5, 1.0, 1.0, 1.0 };
        for (int i = 0; i < 4; ++i) c[i] = gU / (2.0 * fs * C[i]);        // h / 2C · g
        const double r = std::clamp (res01, 0.0, 1.0);
        // up to ≈ 1.25 × critical at the top: the loop's coupling high-pass raises the gain needed to sing
        // (more so with more Bass Comp); the tanh branches then bound the oscillation
        k = kCritical * std::pow (r, 1.3) * (1.0 + 0.25 * r * r * r * r);
        makeup = 1.0 + 0.3 * k;   // the passband falls as 1/(1+k): partly made up after the core (the input stays at circuit level, so resonance keeps its bite)
        const double t = std::tan (3.14159265358979 * std::clamp (fbHpHz, 5.0, 2000.0) / fs);
        gHp = t / (1.0 + t);
    }

    static double gain (double v) { return (double) analog::tanhOverX ((float) v); }

    double process (double x)
    {
        noise = noise * 1664525u + 1013904223u;
        x = x + ((double) (noise >> 9) * (1.0 / 8388608.0) - 1.0) * 1.0e-6;   // circuit noise starts the whistle
        // branch conductances at their last operating points
        const double Tin = gain (dIn), T12 = gain (d12), T23 = gain (d23), T34 = gain (d34), T4 = gain (d4);
        // feedback high-pass written as alpha·v4 + beta (alpha, beta known this sample)
        const double alpha = 1.0 - gHp, beta = -(1.0 - gHp) * zHp;
        // The system is tridiagonal plus one feedback corner (node 1 ← node 4). Substitute upwards from
        // the top node: v4 = a3·v3 + b3, v3 = a2·v2 + b2, v2 = a1·v1 + b1, so v4 = A·v1 + B; then node 1's
        // equation (which also sees v4 through the feedback) gives v1 and everything follows.
        const double r0 = s[0] + c[0] * Tin * (x - k * beta);
        const double d3 = 1.0 + c[3] * (T34 + T4);
        const double a3 = c[3] * T34 / d3, b3 = s[3] / d3;                       // v4 = a3 v3 + b3
        const double d2 = 1.0 + c[2] * (T23 + T34) - c[2] * T34 * a3;
        const double a2 = c[2] * T23 / d2, b2 = (s[2] + c[2] * T34 * b3) / d2;  // v3 = a2 v2 + b2
        const double d1 = 1.0 + c[1] * (T12 + T23) - c[1] * T23 * a2;
        const double a1 = c[1] * T12 / d1, b1 = (s[1] + c[1] * T23 * b2) / d1; // v2 = a1 v1 + b1
        const double A = a3 * a2 * a1, B = a3 * (a2 * b1 + b2) + b3;             // v4 = A v1 + B
        const double fb = c[0] * Tin * k * alpha;
        double v[4];
        v[0] = (r0 + c[0] * T12 * b1 - fb * B) / (1.0 + c[0] * T12 - c[0] * T12 * a1 + fb * A);
        v[1] = a1 * v[0] + b1;
        v[2] = a2 * v[1] + b2;
        v[3] = a3 * v[2] + b3;
        if (! std::isfinite (v[3])) { reset(); return 0.0; }
        const double u = x - k * (alpha * v[3] + beta);
        // trapezoidal state update and the operating points for the next sample
        for (int i = 0; i < 4; ++i) s[i] = 2.0 * v[i] - s[i];
        const double lp = gHp * (v[3] - zHp) + zHp;
        zHp = 2.0 * lp - zHp;
        dIn = u; d12 = v[0] - v[1]; d23 = v[1] - v[2]; d34 = v[2] - v[3]; d4 = v[3];
        return v[3] * makeup;
    }
};

/** The accent sweep circuit: the accented main envelope charges a capacitor through a diode
    (≈ 3 ms with the pot anticlockwise, ≈ 147 ms clockwise) and the capacitor drains through ≈ 100 ms,
    so consecutive accents start from a charged cap and sweep higher. `sweep` is the pot position:
    anticlockwise passes the sharp envelope, clockwise the smoothed capacitor voltage. */
struct AccentSweep
{
    double cap = 0.0, charge = 0.0, drain = 0.0;
    void prepare (double fs, double sweep)
    {
        charge = 1.0 - std::exp (-1.0 / (fs * (0.003 + 0.144 * sweep)));
        drain = std::exp (-1.0 / (fs * 0.1));
    }
    double process (double accentedMeg, double sweep)
    {
        if (accentedMeg > cap) cap += (accentedMeg - cap) * charge;
        cap *= drain;
        return (1.0 - sweep) * accentedMeg + sweep * 1.6 * cap;
    }
};
} // namespace mb::dsp::bass
