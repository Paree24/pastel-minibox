#pragma once
#include "MasterInsert.h"
#include "dsp/Effects.h"
#include "model/Project.h"
#include <atomic>

namespace mb
{
/** Shared sends and the master chain: Send A → Delay, Send B → Reverb, then Glue comp → Limiter → Volume.
    All patterns feed the same delay and reverb, so tails ring across a pattern switch. */
class MasterBus
{
public:
    void prepare (double sr, int maxBlock, bool lookAhead);
    void reset();
    float* sendA (int ch) { return sends[(size_t) ch].data(); }
    float* sendB (int ch) { return sends[2 + (size_t) ch].data(); }
    void clearSends (int n);
    /** Mixes the returns into L/R and runs the master chain in place. */
    void process (float* L, float* R, int n, const MasterSettings& m, double bpm, MasterInsert* insert = nullptr, double ppq = 0.0, bool playing = false);
    int latency() const { return limiter.latency(); }

    std::atomic<float> peakL { 0.0f }, peakR { 0.0f }, glueReduction { 0.0f };

private:
    std::array<std::vector<float>, 4> sends;
    dsp::TapeDelay delay;
    dsp::PlateReverb reverb;
    dsp::GlueComp glue;
    dsp::Limiter limiter;
    dsp::OnePole hpL, hpR;     // 20 Hz clean-up
    dsp::Smoother volSm;
    double sampleRate = 48000.0;
};
} // namespace mb
