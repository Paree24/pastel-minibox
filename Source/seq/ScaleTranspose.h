#pragma once
#include "Scales.h"
#include "model/Project.h"

namespace mb
{
/** Changing the project's scale moves the notes with it (docs/sequencer.md): a new root shifts by the nearer
    interval (C → F up a fourth, F → C down one); a new mode keeps each note's degree (C Phrygian's D♭ becomes
    C Minor's D), an off-scale note keeping its distance from the degree below; scales of different sizes snap
    to the nearest note of the new scale. Chromatic (either side) changes nothing. */
namespace scaletranspose
{
inline bool isChromatic (const Scales::ScaleDef& d) { return d.intervals.size() >= 12; }

inline int note (int n, int fromRoot, const std::vector<int>& fromIv, int toRoot, const std::vector<int>& toIv)
{
    int shift = ((toRoot - fromRoot) % 12 + 12) % 12;
    if (shift > 6) shift -= 12;
    if (fromIv.size() == toIv.size() && ! fromIv.empty())
    {
        const int rel = n - fromRoot;
        const int oct = (int) std::floor (rel / 12.0);
        const int pc = rel - oct * 12;
        size_t deg = 0;
        for (size_t i = 0; i < fromIv.size(); ++i) if (fromIv[i] <= pc) deg = i;
        const int extra = pc - fromIv[deg];
        return std::clamp (fromRoot + shift + oct * 12 + toIv[deg] + extra, 0, 127);
    }
    return std::clamp (Scales::snapNote (std::clamp (n + shift, 0, 127), toRoot, toIv, 0), 0, 127);
}

/** Every melodic note of the project (steps and clips; `isDrum (pattern, track)` tracks are left alone). */
template <class IsDrum> bool project (Project& p, const ScaleSettings& from, const ScaleSettings& to, IsDrum&& isDrum)
{
    const auto& facts = Scales::factoryScales();
    const auto& a = facts[(size_t) std::clamp (from.scale, 0, (int) facts.size() - 1)];
    const auto& b = facts[(size_t) std::clamp (to.scale, 0, (int) facts.size() - 1)];
    if (isChromatic (a) || isChromatic (b) || (from.root == to.root && from.scale == to.scale)) return false;
    auto f = [&] (int n) { return note (n, from.root, a.intervals, to.root, b.intervals); };
    for (int pi = 0; pi < kPatterns; ++pi)
        for (int t = 0; t < kTracks; ++t)
        {
            if (isDrum (pi, t)) continue;
            auto& tr = p.patterns[(size_t) pi].slots[(size_t) t].track;
            for (auto& n : tr.clip) n.note = (uint8_t) f (n.note);
            for (auto& st : tr.steps)
            {
                if (st.noteCount == 0) continue;
                // each note moves; two that land on one pitch become one (the first keeps its values)
                Step out = st;
                out.noteCount = 0; out.muted = 0; out.noteLock = 0;
                for (int i = 0; i < st.noteCount; ++i)
                {
                    const int nn = f (st.notes[i]);
                    if (out.indexOf (nn) >= 0) continue;
                    const int k = out.noteCount++;
                    out.notes[k] = (uint8_t) nn; out.vel[k] = st.vel[i]; out.len[k] = st.len[i]; out.off[k] = st.off[i];
                    out.setMuted (k, st.isMuted (i)); out.setLocked (k, st.isLocked (i));
                }
                st = out;
            }
        }
    return true;
}
} // namespace scaletranspose
} // namespace mb
