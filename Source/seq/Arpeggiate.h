#pragma once
#include "model/Project.h"
#include <map>
#include <random>

namespace mb::arp
{
/** The arpeggiator's rates (PlaySettings::arpRate: 1/4, 1/4 D, 1/4 T … 1/64 T), in quarter notes. */
inline double rateBeats (int r)
{
    static constexpr double beats[15] = { 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0,
                                          0.125, 0.1875, 1.0 / 12.0, 0.0625, 0.09375, 1.0 / 24.0 };
    return beats[std::clamp (r, 0, 14)];
}

/** Written notes arpeggiated with a track's Arp settings (rate, mode, octaves, gate), as the live arpeggiator plays
    them: the notes starting together (within `tolerance` ticks) are a chord; it becomes its notes in turn, one per
    rate step, from its start for as long as its longest note lasts. Each note keeps its pitch's velocity. A lone note
    stays as it is (with one octave there is nothing to arpeggiate). `ppq`: the clip's ticks per quarter note. */
inline std::vector<ClipNote> arpeggiate (std::vector<ClipNote> in, const PlaySettings& ps, int ppq, int tolerance, uint32_t seed = 1)
{
    std::sort (in.begin(), in.end(), [] (const ClipNote& a, const ClipNote& b) { return a.start != b.start ? a.start < b.start : a.note < b.note; });
    const int oct = std::clamp (ps.arpOct + 1, 1, 4);
    const int step = std::max (1, (int) std::lround (rateBeats (ps.arpRate) * ppq));
    const int len = std::max (1, (int) std::lround (step * std::clamp (ps.arpGate, 5.0f, 100.0f) / 100.0));
    std::mt19937 rng (seed);
    std::vector<ClipNote> out;
    for (size_t i = 0; i < in.size();)
    {
        size_t j = i;
        while (j < in.size() && in[j].start - in[i].start <= tolerance) ++j;
        if (j - i == 1 && oct == 1) { out.push_back (in[i]); i = j; continue; }
        const int start = in[i].start;
        int end = start;
        std::vector<int> played;                 // the chord's pitches in the order written (Played)
        std::map<int, ClipNote> byPitch;
        for (size_t k = i; k < j; ++k)
        {
            end = std::max (end, in[k].end());
            if (! byPitch.count (in[k].note)) { played.push_back (in[k].note); byPitch[in[k].note] = in[k]; }
        }
        std::vector<int> pool;
        for (auto& [n, _] : byPitch) pool.push_back (n);   // ascending
        const auto& src = ps.arpMode == 5 ? played : pool;
        std::vector<int> ex;
        for (int o = 0; o < oct; ++o) for (int n : src) if (n + 12 * o <= 127) ex.push_back (n + 12 * o);
        const int N = (int) ex.size();
        auto pick = [&] (int k) -> int
        {
            if (N == 1) return ex[0];
            const int c = 2 * N - 2, p = k % c;
            switch (ps.arpMode)
            {
                case 1: return ex[(size_t) (N - 1 - k % N)];                                         // Down
                case 2: return p < N ? ex[(size_t) p] : ex[(size_t) (c - p)];                        // Up-Down
                case 3: return p < N ? ex[(size_t) (N - 1 - p)] : ex[(size_t) (p - (N - 1))];        // Down-Up
                case 4: return ex[(size_t) (rng() % (unsigned) N)];                                  // Random
                default: return ex[(size_t) (k % N)];                                                // Up, Played
            }
        };
        int k = 0;
        for (int t = start; t < end; t += step, ++k)
        {
            const int pitch = pick (k);
            auto it = byPitch.find (pitch);
            for (int o = 1; it == byPitch.end() && o < oct; ++o) it = byPitch.find (pitch - 12 * o);   // an octave copy: its note's velocity
            ClipNote n = it != byPitch.end() ? it->second : in[i];
            n.note = (uint8_t) pitch; n.start = t; n.length = std::min (len, end - t);
            n.flags = 0; n.id = 0;
            out.push_back (n);
        }
        i = j;
    }
    return out;
}
} // namespace mb::arp
