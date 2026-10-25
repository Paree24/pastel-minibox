#include "MasterBus.h"

namespace mb
{
void MasterBus::prepare (double sr, int maxBlock, bool lookAhead)
{
    sampleRate = sr;
    for (auto& s : sends) s.assign ((size_t) maxBlock, 0.0f);
    delay.prepare ((float) sr);
    reverb.prepare ((float) sr);
    glue.prepare ((float) sr);
    limiter.prepare ((float) sr, lookAhead);
    hpL.setCutoff (20.0f, (float) sr); hpR.setCutoff (20.0f, (float) sr);
    volSm.setTime (20.0f, (float) sr);
}

void MasterBus::reset() { delay.reset(); reverb.reset(); }

void MasterBus::clearSends (int n)
{
    for (auto& s : sends) std::fill (s.begin(), s.begin() + n, 0.0f);
}

void MasterBus::process (float* L, float* R, int n, const MasterSettings& m, double bpm)
{
    const float delaySecs = (float) (dsp::divisionBeats ((float) m.delayDiv) * 60.0 / std::max (20.0, bpm));
    delay.setTone (m.delayTone);
    reverb.set (m.reverbDecay, m.reverbDamp);
    float pl = 0.0f, pr = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        float wl = 0.0f, wr = 0.0f;
        delay.process (sends[0][(size_t) i], sends[1][(size_t) i], wl, wr, delaySecs, m.delayFeedback, m.delayReturn);
        l += wl; r += wr;
        float rl = 0.0f, rr = 0.0f;
        reverb.process (sends[2][(size_t) i], sends[3][(size_t) i], rl, rr, m.reverbReturn * 1.4f);
        l += rl; r += rr;
        l = hpL.hp (l); r = hpR.hp (r);
        glue.process (l, r, m.glue);
        const float v = volSm.tick (m.volume);
        l *= v; r *= v;
        limiter.process (l, r, m.limitDb);
        L[i] = l; R[i] = r;
        pl = std::max (pl, std::abs (l)); pr = std::max (pr, std::abs (r));
    }
    peakL.store (std::max (pl, peakL.load() * 0.9f));
    peakR.store (std::max (pr, peakR.load() * 0.9f));
    glueReduction.store (glue.reduction);
}
} // namespace mb
