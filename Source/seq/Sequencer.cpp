#include "Sequencer.h"

namespace mb
{
void Sequencer::start (Project& p, bool fromTop)
{
    if (fromTop) { step = 0; stepPhase = 0.0; needBoundary = true; beatPos = 0.0; seekTarget = 0; }
    else needBoundary = stepPhase <= 0.0;
    playing = true;
    pass = 0;
    chanceSeed = (uint32_t) rng.nextInt();
    pending.clear();
    clickPhase = 1.0e12;   // click on the first beat
    clickBeat = 0;
    const double spq = sampleRate * 60.0 / std::max (20.0, p.bpm);
    countInSamplesLeft = (recordArmed && p.countIn) ? (int) (p.barQuarters() * spq) : 0;
}

void Sequencer::stop()
{
    playing = false;
    pending.clear();
    countInSamplesLeft = 0;
}

void Sequencer::push (int64_t time, const SeqEvent& e)
{
    if (pending.size() < pending.capacity()) pending.push_back ({ time, e });
}

void Sequencer::generate (Project& p, DeviceLookup& devs, int n, std::vector<SeqEvent>& out)
{
    switchedThisBlock = false;
    if (! playing) return;
    const double spq = sampleRate * 60.0 / std::max (20.0, p.bpm);
    const double beatLen = spq * 4.0 / p.unit;

    // metronome clicks (and the count-in bar, which always clicks)
    auto clicks = [&] (int from, int to, bool force)
    {
        if (! (p.metronome || force)) { clickPhase += to - from; return; }
        double pos = from;
        while (true)
        {
            const double toBeat = clickPhase >= beatLen ? 0.0 : beatLen - clickPhase;
            if (pos + toBeat >= to) { clickPhase += to - pos; break; }
            pos += toBeat;
            SeqEvent e; e.kind = SeqEvent::Click; e.offset = (int) pos; e.value = (clickBeat % std::max (1, p.beats)) == 0 ? 1.0f : 0.0f;
            out.push_back (e);
            ++clickBeat;
            clickPhase = 0.0;
        }
    };

    int start = 0;
    if (countInSamplesLeft > 0)
    {
        const int take = std::min (n, countInSamplesLeft);
        clicks (0, take, true);
        countInSamplesLeft -= take;
        if (countInSamplesLeft > 0) { now += n; return; }
        start = take;
        clickPhase = 1.0e12; clickBeat = 0;
    }

    clicks (start, n, false);

    double pos = start;
    double stepLen = p.stepQuarters (activePattern) * spq;
    while (true)
    {
        const double remaining = needBoundary ? 0.0 : (1.0 - stepPhase) * stepLen;
        if (pos + remaining < (double) n)
        {
            pos += remaining;
            boundary (p, devs, (int) std::lround (pos), out);
            stepLen = p.stepQuarters (activePattern) * spq;
            stepPhase = 0.0;
            if (remaining <= 0.0 && ! needBoundary) pos += 1.0e-9;
        }
        else
        {
            stepPhase += ((double) n - pos) / stepLen;
            break;
        }
    }

    // emit scheduled events that fall inside this block
    for (size_t i = 0; i < pending.size();)
    {
        if (pending[i].time < now + n)
        {
            auto e = pending[i].e;
            e.offset = (int) std::clamp<int64_t> (pending[i].time - now, 0, n - 1);
            out.push_back (e);
            pending[i] = pending.back();
            pending.pop_back();
        }
        else ++i;
    }
    now += n;
    beatPos += (n - start) / spq;
}

void Sequencer::boundary (Project& p, DeviceLookup& devs, int offset, std::vector<SeqEvent>& out)
{
    juce::ignoreUnused (out);
    entered = needBoundary;   // nothing was scheduled ahead of this step (play, seek, pattern switch)
    bool wrapped = false;
    if (needBoundary) { step = std::clamp (seekTarget, 0, std::max (0, p.patternSteps (activePattern) - 1)); seekTarget = 0; needBoundary = false; }
    else
    {
        int next = nextStep (p, step);
        if (next <= step && step + 1 >= p.patternSteps (activePattern) && devs.growAtEnd (activePattern)) next = step + 1;   // a take still playing: on into the new bar
        wrapped = next <= step;
        if (wrapped) ++pass;   // the pattern (or its loop range) starts again
        step = next;
    }
    int spb = p.stepsPerBar (activePattern);
    if (step % spb == 0 && pendingBeats > 0)
    {
        p.beats = pendingBeats; p.unit = pendingUnit;
        pendingBeats = pendingUnit = 0;
        spb = p.stepsPerBar (activePattern);
        step = 0;
    }
    if (queuedPattern >= 0)
    {
        // performance mode (and Pattern end): when the playing pattern (or its loop) has played to its end
        const int q = p.perform ? 1 : p.switchQuantize;
        if (q == 2 || (q == 0 && step % spb == 0) || (q == 1 && wrapped))
        {
            lastSwitchedFrom = activePattern;
            activePattern = queuedPattern;
            queuedPattern = -1;
            step = 0;
            pass = 0;
            switchedThisBlock = true;
            entered = true;
        }
    }
    const double spq = sampleRate * 60.0 / std::max (20.0, p.bpm);
    scheduleStep (p, devs, step, offset, p.stepQuarters (activePattern) * spq);
}

int Sequencer::nextStep (const Project& p, int si) const
{
    const auto& pt = p.patterns[(size_t) activePattern];
    const int total = p.patternSteps (activePattern);
    const bool loop = pt.loopOn && pt.loopEnd > pt.loopStart && pt.loopStart < total;
    const int n = si + 1;
    if (loop && n == std::min (pt.loopEnd, total)) return pt.loopStart;
    if (n >= total) return loop ? pt.loopStart : 0;
    return n;
}

void Sequencer::scheduleStep (Project& p, DeviceLookup& devs, int si, int offset, double stepLen)
{
    const int pat = activePattern;
    auto& pattern = p.patterns[(size_t) pat];
    const int64_t t0 = now + offset;
    const double swingDelay = (si & 1) ? (p.swing - 0.5) * 2.0 * stepLen : 0.0;
    for (int t = 0; t < kTracks; ++t)
    {
        auto* dev = devs.deviceAt (pat, t);
        if (dev == nullptr) continue;
        auto& track = pattern.slots[(size_t) t].track;

        // parameter locks: cleared at every step, then this step's values applied
        if (! track.locks.empty())
        {
            SeqEvent c; c.kind = SeqEvent::ClearLocks; c.pattern = pat; c.track = t;
            push (t0, c);
            for (auto& lane : track.locks)
            {
                const float v = lane.values[(size_t) si];
                if (std::isnan (v)) continue;
                SeqEvent l; l.kind = SeqEvent::Lock; l.pattern = pat; l.track = t; l.param = lane.param; l.value = v;
                push (t0, l);
            }
        }

        auto note = [&] (int nn, float vel, int flags, double delay, double len)
        {
            SeqEvent on; on.pattern = pat; on.track = t;
            on.note.type = NoteEvent::NoteOn; on.note.note = nn; on.note.velocity = vel; on.note.flags = flags;
            push (t0 + (int64_t) std::llround (std::max (0.0, delay)), on);
            SeqEvent off = on; off.note.type = NoteEvent::NoteOff;
            push (t0 + (int64_t) std::llround (std::max (1.0, delay + len)), off);
        };

        if (dev->laneCount() > 0)
        {
            for (int lane = 0; lane < kLanes; ++lane)
            {
                const auto& ds = track.lanes[(size_t) lane][(size_t) si];
                if (! ds.on || ! trigConditionPasses (ds.cond, pass) || rng.nextInt (100) >= ds.probability) continue;
                const double delay = swingDelay + ds.microTiming / 48.0 * stepLen;
                const int r = std::clamp ((int) ds.ratchet, 1, 4);
                for (int k = 0; k < r; ++k)
                    note (dev->laneNote (lane), ds.velocity / 127.0f, ds.flags, delay + k * stepLen / r, stepLen / r * 0.8);
            }
            continue;
        }

        if (track.pianoRoll)
        {
            // a piano-roll clip: the notes that start inside this step, at their exact position
            const int stT = p.stepTicks (pat), a = si * stT, b = a + stT;
            // controller lanes: the points in this step; at a start (play, seek, the loop's first step) each lane's
            // value is chased from the last point before
            auto control = [&] (int lane, float value, double delay)
            {
                SeqEvent c; c.pattern = pat; c.track = t; c.note.type = NoteEvent::Control; c.note.note = lane; c.note.velocity = value;
                push (t0 + (int64_t) std::llround (std::max (0.0, delay)), c);
            };
            if (! track.cc.empty())
            {
                const auto& ptn = p.patterns[(size_t) pat];
                const int loopFirst = ptn.loopOn && ptn.loopEnd > ptn.loopStart ? ptn.loopStart : 0;
                if (entered || si == loopFirst)
                {
                    std::array<int, 256> lastIdx; lastIdx.fill (-1);
                    for (int k = 0; k < (int) track.cc.size() && track.cc[(size_t) k].tick <= a; ++k) lastIdx[track.cc[(size_t) k].lane] = k;
                    for (int l = 0; l < 256; ++l)
                        if (lastIdx[(size_t) l] >= 0 && track.cc[(size_t) lastIdx[(size_t) l]].tick < a) control (l, track.cc[(size_t) lastIdx[(size_t) l]].value, 0.0);
                }
                for (const auto& pt : track.cc)
                    if (pt.tick >= a && pt.tick < b) control (pt.lane, pt.value, (pt.tick - a) / (double) stT * stepLen);
            }
            for (const auto& cn : track.clip)
            {
                if (cn.muted || cn.start < a || cn.start >= b) continue;
                const double delay = (cn.start - a) / (double) stT * stepLen + (cn.start == a ? swingDelay : 0.0);
                note (cn.note, cn.vel / 127.0f, cn.flags, delay, std::max (1.0, cn.length / (double) stT * stepLen));
            }
            continue;
        }
        // a step plays (condition, probability) the same way for its early notes and its on-time ones
        auto plays = [&] (const Step& st, int index, int ps)
        {
            return st.noteCount > 0 && trigConditionPasses (st.cond, ps) && chance (pat, t, index, ps) < st.probability;
        };
        // notes timed before their step (negative micro-timing + offset) are scheduled one step ahead
        auto emit = [&] (const Step& st, double base, bool early)
        {
            const int r = std::clamp ((int) st.ratchet, 1, 4);
            for (int k = 0; k < r; ++k)
                for (int i = 0; i < st.noteCount; ++i)
                {
                    const int d = st.microTiming + st.off[i];
                    if (st.isMuted (i) || (d < 0) != early) continue;   // muted notes stay in the pattern but don't play
                    // each note of a chord has its own length and velocity
                    double len = st.len[i] / 128.0 * stepLen;
                    if (st.flags & (kSlide | kTie)) len = stepLen + 0.004 * sampleRate;   // overlaps the next note: slide / tie
                    note (st.notes[i], st.vel[i] / 127.0f, st.flags, base + d / (double) kTicksPerStep * stepLen + k * stepLen / r,
                          r > 1 ? std::min (len, stepLen / r * 0.9) : len);
                }
        };
        const auto& s = track.steps[(size_t) si];
        if (plays (s, si, pass)) { emit (s, swingDelay, false); if (entered) emit (s, swingDelay, true); }   // early notes: now, as soon as possible
        const int ni = nextStep (p, si);
        const int np = ni <= si ? pass + 1 : pass;
        const auto& ns = track.steps[(size_t) ni];
        if (plays (ns, ni, np)) emit (ns, stepLen + ((ni & 1) ? (p.swing - 0.5) * 2.0 * stepLen : 0.0), true);
    }
}
} // namespace mb
