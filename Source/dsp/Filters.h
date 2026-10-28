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

} // namespace mb::dsp
