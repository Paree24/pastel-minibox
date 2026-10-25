#pragma once
#include <array>
#include <algorithm>
#include "dsp/DspCommon.h"

namespace mb
{
/** Arpeggiator: Off, Up, Down, Up-Dn, Rand over 1–3 octaves at a synced rate with a gate.
    Runs from the transport position when playing (counted from the phrase's first key),
    otherwise from its own clock at the same tempo. */
struct Arpeggiator
{
    enum Mode { Off, Up, Down, UpDown, Random };
    std::array<int, 16> keys {};
    int numKeys = 0, step = 0, sounding = -1;
    double samplesToNext = 0.0, samplesToOff = -1.0;
    // transport sync: while it plays, steps are slots counted in transport
    // beats from the phrase's first key (anchor); `slot` is the last that fired
    long long slot = 0;
    double anchor = 0.0, expectBeat = 0.0;
    bool fresh = false, wasGrid = false;
    float slotRate = -1.0f;
    dsp::Rng rng { 5150u };

    void addKey (int n)
    {
        for (int i = 0; i < numKeys; ++i) if (keys[(size_t) i] == n) return;
        if (numKeys < 16) keys[(size_t) numKeys++] = n;
        std::sort (keys.begin(), keys.begin() + numKeys);
        if (numKeys == 1) { step = 0; samplesToNext = 0.0; fresh = true; }
    }
    void removeKey (int n)
    {
        for (int i = 0; i < numKeys; ++i)
            if (keys[(size_t) i] == n) { std::copy (keys.begin() + i + 1, keys.begin() + numKeys, keys.begin() + i); --numKeys; return; }
    }
    void clear() { numKeys = 0; }

    int noteForStep (int mode, int octaves) const
    {
        const int total = numKeys * std::max (1, octaves);
        if (total == 0) return -1;
        int idx;
        switch (mode)
        {
            case Down: idx = total - 1 - (step % total); break;
            case UpDown:
            {
                const int span = std::max (1, 2 * total - 2);
                const int p = step % span;
                idx = p < total ? p : span - p;
                break;
            }
            case Random: idx = (int) (const_cast<dsp::Rng&> (rng).unipolar() * (float) total) % total; break;
            default: idx = step % total; break;
        }
        return keys[(size_t) (idx % numKeys)] + 12 * (idx / numKeys);
    }
};
} // namespace mb
