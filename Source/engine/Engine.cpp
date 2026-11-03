#include "Engine.h"

namespace mb
{
Engine::Engine (Mode m) : engineMode (m)
{
    events.reserve (4096);
}

Engine::~Engine() = default;

void Engine::prepare (double sr, int block)
{
    const ScopedEdit edit (*this);
    sampleRate = sr;
    maxBlock = std::max (block, 16);
    for (auto* v : { &slotL, &slotR, &auxAL, &auxAR, &auxBL, &auxBR, &clickBuf }) v->assign ((size_t) maxBlock, 0.0f);
    clickRing.assign (16384, 0.0f); clickRingPos = 0;
    for (auto& row : devices) for (auto& d : row) if (d) d->prepare (sr, maxBlock);
    seq.prepare (sr);
    masterBus.prepare (sr, maxBlock, proj.master.lookAhead);
    if (masterInsert != nullptr) masterInsert->prepare (sr, maxBlock);
    chunkMidi.ensureSize (4096);
    for (auto& in : inputs) in.prepare (sr);
}

void Engine::setLookAhead (bool on)
{
    proj.master.lookAhead = on;
    dsp::Limiter fresh;
    fresh.prepare ((float) sampleRate, on);   // allocation here, outside the lock
    {
        const ScopedEdit edit (*this);
        masterBus.swapLimiter (fresh);
    }
    // `fresh` (the old limiter now) is freed here, after the lock
}

void Engine::setMasterInsert (MasterInsert* m)
{
    if (m != nullptr) m->prepare (sampleRate, maxBlock);   // before it is reachable from the audio thread
    const ScopedEdit edit (*this);
    masterInsert = m;
}

std::unique_ptr<DeviceProcessor> Engine::replaceDevice (int pattern, int track, std::unique_ptr<DeviceProcessor> d)
{
    const auto* old = device (pattern, track);
    if (old != nullptr && d != nullptr && old->laneCount() > 0 && d->laneCount() > 0)
    {
        const ScopedEdit edit (*this);
        auto& tr = proj.patterns[(size_t) pattern].slots[(size_t) track].track;
        const auto before = tr.lanes;
        for (auto& lane : tr.lanes) std::fill (lane.begin(), lane.end(), DrumStep());
        for (int l = 0; l < old->laneCount() && l < kLanes; ++l)
        {
            const int to = d->laneForNote (old->laneNote (l));
            tr.lanes[(size_t) (to >= 0 && to < kLanes ? to : l)] = before[(size_t) l];
        }
    }
    return setDevice (pattern, track, std::move (d));
}

std::unique_ptr<DeviceProcessor> Engine::setDevice (int pattern, int track, std::unique_ptr<DeviceProcessor> d)
{
    if (d) d->prepare (sampleRate, maxBlock);   // allocation happens here, outside the lock
    {
        const ScopedEdit edit (*this);
        std::swap (devices[(size_t) pattern][(size_t) track], d);
        if (devices[(size_t) pattern][(size_t) track] == nullptr || d == nullptr || d->type() != devices[(size_t) pattern][(size_t) track]->type())
        {
            // a new instrument type: its step data no longer means the same thing
            auto& tr = proj.patterns[(size_t) pattern].slots[(size_t) track].track;
            tr.locks.clear();
        }
    }
    return d;   // freed by the caller, after the lock is released
}

void Engine::replaceProject (std::unique_ptr<Project> p, std::array<std::array<std::unique_ptr<DeviceProcessor>, kTracks>, kPatterns> devs)
{
    for (auto& row : devs) for (auto& d : row) if (d) d->prepare (sampleRate, maxBlock);
    {
        const ScopedEdit edit (*this);
        seq.stop();
        seq.resetPosition();
        seq.activePattern = 0;
        seq.queuedPattern = -1;
        std::swap (proj, *p);
        std::swap (devices, devs);
        ++projectGen;
        clearTails();
        masterBus.reset();
        recording = false;
        playingFlag = false;
        activePat = 0; queuedPat = -1; curStep = 0; progress = 0.0f;
        focusPattern = 0;
    }
    // `p` (old project) and `devs` (old devices) are freed here, outside the lock.
}

void Engine::newProject()
{
    replaceProject (std::make_unique<Project>(), {});
}

void Engine::play()
{
    if (recording.load()) beginTake();
    cmdPlay = 1;
}

void Engine::beginTake()
{
    if (takeOpen.load()) return;
    ++recordTake;   // Replace keeps only this take's notes
    // the audio thread repeats clips into bars a take adds: room for that without allocating.
    // Grown copies are built here, outside the audio lock (only this thread edits clips while no
    // take is open), and swapped in under it: the audio thread never waits on the allocations.
    std::vector<std::vector<ClipNote>> grown;
    grown.reserve ((size_t) (kPatterns * kTracks));
    for (auto& pt : proj.patterns)
        for (auto& sl : pt.slots)
            if (sl.track.pianoRoll && sl.track.clip.capacity() < sl.track.clip.size() * (size_t) (kMaxBars + 1) + 256)
            {
                std::vector<ClipNote> c;
                c.reserve (sl.track.clip.size() * (size_t) (kMaxBars + 1) + 256);   // doubling up to the longest
                c.assign (sl.track.clip.begin(), sl.track.clip.end());
                grown.push_back (std::move (c));
            }
    {
        const ScopedEdit edit (*this);
        size_t gi = 0;
        for (auto& pt : proj.patterns)
            for (auto& sl : pt.slots)
                if (sl.track.pianoRoll && gi < grown.size() && sl.track.clip.capacity() < sl.track.clip.size() * (size_t) (kMaxBars + 1) + 256)
                    sl.track.clip.swap (grown[gi++]);   // the old storage is freed with `grown`, after the lock
    }
    for (int p = 0; p < kPatterns; ++p) takeStartBars[(size_t) p] = proj.patterns[(size_t) p].lengthBars;
    takePattern = seq.activePattern;
    for (int t = 0; t < kTracks; ++t)
    {
        takeBefore[(size_t) t] = proj.patterns[(size_t) takePattern].slots[(size_t) t].track;
        takeBeforeClip[(size_t) t] = takeBefore[(size_t) t];
        takeBeforeClip[(size_t) t].convertToPianoRoll (proj.stepTicks (takePattern), proj.patternSteps (takePattern));
    }
    takeLastStep = -1;
    takeAuto.clear();
    takeAuto.reserve (4096);
    takeOpen = true;
}

void Engine::stop()
{
    endTake();   // stopping the transport ends a take too
    cmdStop = playingFlag.load() ? 1 : 2;
}

void Engine::endTake()
{
    if (! takeOpen) return;
    takeOpen = false;
    const ScopedEdit edit (*this);
    const int last = takeLastStep.exchange (-1);
    for (int p = 0; p < kPatterns; ++p)
    {
        auto& pt = proj.patterns[(size_t) p];
        if (pt.lengthBars <= takeStartBars[(size_t) p]) continue;
        // the doubling the take's notes reach (two one-bar chords: 2 bars, three: 4); a take without notes leaves the length as it was
        const int spb = proj.stepsPerBar (p);
        const int reach = p == seq.activePattern && last >= 0 ? last / spb + 1 : 0;
        int bars = takeStartBars[(size_t) p];
        while (bars < reach && bars < pt.lengthBars) bars *= 2;
        bars = std::clamp (bars, takeStartBars[(size_t) p], pt.lengthBars);
        const int stT = proj.stepTicks (p), from = bars * spb, to = pt.lengthBars * spb;
        // automation recorded in the bars that go folds back onto the kept ones, the later pass winning: turning a
        // knob keeps its motion without lengthening the pattern (or copying its clips)
        if (p == takePattern)
        {
            const int keptT = from * stT;
            for (auto& a : takeAuto)
                if (! a.lane && a.pos >= from && a.pos < to)
                    if (auto* l = pt.slots[(size_t) a.track].track.lockFor (a.key, true)) l->values[(size_t) (a.pos % from)] = a.value;
            // a lane: each later pass replaces the stretch it covered
            struct Span { int track, lane, pass, lo, hi; };
            std::vector<Span> spans;
            for (auto& a : takeAuto)
            {
                if (! a.lane || a.pos < keptT || a.pos >= to * stT) continue;
                const int pass = a.pos / keptT, at = a.pos % keptT;
                auto it = std::find_if (spans.begin(), spans.end(), [&] (const Span& s) { return s.track == a.track && s.lane == a.key && s.pass == pass; });
                if (it == spans.end()) spans.push_back ({ a.track, a.key, pass, at, at });
                else { it->lo = std::min (it->lo, at); it->hi = std::max (it->hi, at); }
            }
            std::stable_sort (spans.begin(), spans.end(), [] (const Span& x, const Span& y) { return x.pass < y.pass; });
            for (auto& s : spans)
            {
                auto& tr = pt.slots[(size_t) s.track].track;
                tr.erasePoints (s.lane, s.lo, s.hi + 1);
                for (auto& a : takeAuto)
                    if (a.lane && a.track == s.track && a.key == s.lane && a.pos >= keptT && a.pos < to * stT && a.pos / keptT == s.pass)
                        tr.setPoint (s.lane, a.pos % keptT, a.value);
            }
        }
        // the bars the take added past that go, with the copies made in them
        for (auto& sl : pt.slots)
        {
            auto& tr = sl.track;
            for (int s = from; s < to && s < kMaxSteps; ++s)
            {
                tr.steps[(size_t) s].clear();
                for (auto& lane : tr.lanes) lane[(size_t) s].clear();
                for (auto& l : tr.locks) l.values[(size_t) s] = std::numeric_limits<float>::quiet_NaN();
            }
            tr.clip.erase (std::remove_if (tr.clip.begin(), tr.clip.end(), [&] (const ClipNote& n) { return n.start >= from * stT && n.start < to * stT; }), tr.clip.end());
            tr.erasePoints (-1, from * stT, to * stT);
        }
        pt.lengthBars = bars;
    }
}

void Engine::setRecord (bool on)
{
    if (on && ! recording.load()) beginTake();
    if (! on && recording.load()) endTake();
    recording = on;
}

bool Engine::growAtEnd (int pat)
{
    // live recording in Replace: when the take is still playing at the pattern's end (a note in its last bar, or a
    // key held across the end) the pattern gets one more bar and playback runs on; otherwise it loops as always
    // while recording live without Overdub the pattern never loops back: it gets one more bar and the take goes on
    if (engineMode == Mode::Plugin || ! recording.load() || ! takeOpen.load() || proj.recordMode != 0 || proj.overdub || seq.countInSamplesLeft > 0) return false;
    auto& pt = proj.patterns[(size_t) pat];
    if ((pt.loopOn && pt.loopEnd > pt.loopStart) || pt.lengthBars >= proj.maxBarsFor (pat)) return false;
    // only notes lengthen a take: recording automation alone (knobs, macros, the mod wheel) loops the pattern
    bool notes = takeLastStep.load() >= 0;
    for (const auto& o : openNotes) notes = notes || o.used;
    if (! notes) return false;
    // the pattern doubles (1 → 2 → 4 → 8 → 16 bars): the other tracks repeat into the new bars; the recorded track's stay for the take
    const int spb = proj.stepsPerBar (pat), oldBars = pt.lengthBars, newBars = std::min (proj.maxBarsFor (pat), oldBars * 2);
    const int stT = proj.stepTicks (pat), barT = spb * stT, rec = focusTrack.load();
    for (int t = 0; t < kTracks; ++t)
    for (int nb = oldBars; nb < newBars; ++nb)
    {
        const int src = nb % oldBars;
        auto& tr = pt.slots[(size_t) t].track;
        if (t == rec)
        {
            // the recorded track: what it held before the take repeats into the new bar (the take is not copied)
            if (pat != takePattern) continue;
            const int startBars = std::max (1, takeStartBars[(size_t) pat]), sb = nb % startBars;
            const auto& before = takeBefore[(size_t) t];
            for (int i = 0; i < spb; ++i)
            {
                const auto from = (size_t) (sb * spb + i), to = (size_t) (nb * spb + i);
                if (! tr.pianoRoll && before.steps[from].noteCount > 0) tr.steps[to] = before.steps[from];
                for (size_t l = 0; l < tr.lanes.size(); ++l) if (before.lanes[l][from].on) tr.lanes[l][to] = before.lanes[l][from];
                for (auto& bl : before.locks)
                    if (! std::isnan (bl.values[from]))
                        for (auto& l : tr.locks) if (l.param == bl.param) l.values[to] = bl.values[from];
            }
            if (tr.pianoRoll)
                for (const auto& c : takeBeforeClip[(size_t) t].clip)
                    if (c.start >= sb * barT && c.start < (sb + 1) * barT && tr.clip.size() < tr.clip.capacity())
                    {
                        auto n = c;
                        n.start += (nb - sb) * barT;
                        tr.addClipNote (n);
                    }
            continue;
        }
        bool empty = true;
        for (int s = nb * spb; s < (nb + 1) * spb && empty; ++s)
        {
            empty = tr.steps[(size_t) s].noteCount == 0;
            for (auto& lane : tr.lanes) empty = empty && ! lane[(size_t) s].on;
        }
        for (auto& n : tr.clip) empty = empty && ! (n.start >= nb * barT && n.start < (nb + 1) * barT);
        if (! empty) continue;
        for (int i = 0; i < spb; ++i)
        {
            tr.steps[(size_t) (nb * spb + i)] = tr.steps[(size_t) (src * spb + i)];
            for (auto& lane : tr.lanes) lane[(size_t) (nb * spb + i)] = lane[(size_t) (src * spb + i)];
            for (auto& l : tr.locks) l.values[(size_t) (nb * spb + i)] = l.values[(size_t) (src * spb + i)];
        }
        const size_t n0 = tr.clip.size();
        for (size_t k = 0; k < n0 && tr.clip.size() < tr.clip.capacity(); ++k)   // within the room reserved at record start
            if (tr.clip[k].start >= src * barT && tr.clip[k].start < (src + 1) * barT)
            {
                auto c = tr.clip[k];
                c.start += (nb - src) * barT;
                tr.addClipNote (c);
            }
    }
    pt.lengthBars = newBars;
    return true;
}

void Engine::queuePattern (int p) { pendingQueue = p; }

void Engine::setTimeSignature (int beats, int unit)
{
    const ScopedEdit edit (*this);
    if (seq.isPlaying()) { seq.pendingBeats = beats; seq.pendingUnit = unit; }
    else { proj.beats = beats; proj.unit = unit; }
    for (int p = 0; p < kPatterns; ++p)
        proj.patterns[(size_t) p].lengthBars = std::min (proj.patterns[(size_t) p].lengthBars, proj.maxBarsFor (p));
}

void Engine::copyPattern (int from, int to)
{
    if (from == to) return;
    std::array<std::unique_ptr<DeviceProcessor>, kTracks> fresh;
    for (int t = 0; t < kTracks; ++t)
        if (auto* src = device (from, t))
        {
            juce::ValueTree s ("Device");
            src->save (s);
            fresh[(size_t) t] = createDevice (src->type());
            fresh[(size_t) t]->load (s);
            fresh[(size_t) t]->prepare (sampleRate, maxBlock);
        }
    // the copy is made (and the replaced pattern freed) outside the lock: under it, only swaps
    auto copy = std::make_unique<Pattern> (proj.patterns[(size_t) from]);
    std::array<std::unique_ptr<DeviceProcessor>, kTracks> old;
    {
        const ScopedEdit edit (*this);
        std::swap (proj.patterns[(size_t) to], *copy);
        for (int t = 0; t < kTracks; ++t) { old[(size_t) t] = std::move (devices[(size_t) to][(size_t) t]); devices[(size_t) to][(size_t) t] = std::move (fresh[(size_t) t]); }
    }
}

void Engine::clearPattern (int p)
{
    auto blank = std::make_unique<Pattern>();   // built and the old one freed outside the lock
    std::array<std::unique_ptr<DeviceProcessor>, kTracks> old;
    const ScopedEdit edit (*this);
    std::swap (proj.patterns[(size_t) p], *blank);
    for (int t = 0; t < kTracks; ++t) old[(size_t) t] = std::move (devices[(size_t) p][(size_t) t]);
    tailing[(size_t) p] = false;
}

void Engine::injectNote (int track, int note, float velocity, bool on, int pattern, bool audition)
{
    int s1, n1, s2, n2;
    uiFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 + n2 == 0) return;
    uiNotes[(size_t) (n1 > 0 ? s1 : s2)] = { pattern < 0 ? focusPattern.load() : pattern, track, note, velocity, on, audition };
    uiFifo.finishedWrite (1);
}

void Engine::setRoll (int track, int note, float velocity, double beats, bool on)
{
    if (! on)
    {
        for (auto& r : rolls) if (r.active.load() && r.track.load() == track && r.note.load() == note) r.active = false;
        return;
    }
    for (auto& r : rolls)
        if (! r.active.load())
        {
            r.pattern = focusPattern.load(); r.track = track; r.note = note; r.vel = velocity;
            r.beats = std::clamp (beats, 1.0 / 32.0, 4.0);
            r.startOffset = 0;
            r.started = false;   // the audio thread has finished with this slot: it is inactive
            r.active.store (true, std::memory_order_release);
            return;
        }
}

void Engine::runRolls (int n, std::vector<SeqEvent>& out)
{
    const bool synced = seq.isPlaying() && seq.countInSamplesLeft <= 0;
    const double b0 = synced ? seq.beatPosition() : beatClock;
    const double perSample = std::max (20.0, proj.bpm) / (60.0 * sampleRate);
    const double b1 = b0 + n * perSample;
    for (auto& r : rolls) runRoll (r, b0, b1, perSample, n, out, synced);
    for (auto& r : liveRolls) runRoll (r, b0, b1, perSample, n, out, synced);
}

void Engine::runRoll (Roll& r, double b0, double b1, double perSample, int n, std::vector<SeqEvent>& out, bool synced)
{
    auto at = [&] (double beat) { return std::clamp ((int) ((beat - b0) / perSample), 0, n - 1); };
    auto endNote = [&] (int off)
    {
        if (! r.offPending) return;
        r.offPending = false;
        inputKey (r.offPattern, r.offTrack, r.offNote, 0.0f, false, off, out, true);
    };
    const bool active = r.active.load (std::memory_order_acquire);
    if (! active || ! r.started) endNote (0);   // released (or the slot reused): the last repeat stops now
    if (! active) return;
    const double step = r.beats.load();
    const int pat = r.pattern.load(), tr = r.track.load(), note = r.note.load();
    const float vel = r.vel.load();
    auto* dev = device (pat, tr);
    const bool gated = dev != nullptr && dev->laneCount() == 0;   // a synth: each repeat holds for half the rate
    auto hit = [&] (int off, double beat)
    {
        endNote (off);
        inputKey (pat, tr, note, vel, true, off, out, true);
        if (gated)
        {
            r.offPending = true; r.offBeat = beat + step * 0.5;
            r.offPattern = pat; r.offTrack = tr; r.offNote = note;
        }
        else inputKey (pat, tr, note, vel, false, std::min (off + 1, n - 1), out, true);
    };
    if (! r.started)
    {
        // the first hit: stopped, at the press, the repeats counting from it; playing, on the nearest grid line (at
        // once when the press came just after one, at the next when it came just before): no flam at the start
        r.started = true;
        const int off = std::clamp (r.startOffset, 0, n - 1);
        const double at = b0 + off * perSample;
        if (! synced) { hit (off, at); r.nextBeat = at + step; }
        else
        {
            const double cell = std::floor (at / step + 1.0e-9);
            if (at / step - cell <= 0.5) { hit (off, at); r.nextBeat = (cell + 1.0) * step; }
            else r.nextBeat = (cell + 1.0) * step;   // waits for the line just ahead
        }
    }
    if (r.nextBeat < b0 - step || r.nextBeat > b1 + 2.0 * step)   // the grid jumped (Play, a seek): realign
        r.nextBeat = std::ceil (b0 / step - 1.0e-9) * step;
    // playing, the repeats swing with the pattern (the project's swing on its step grid)
    const double stepQ = synced ? proj.stepQuarters (seq.activePattern) : 0.0;
    auto swung = [&] (double beat) { return stepQ > 0.0 ? proj.swungSteps (beat / stepQ) * stepQ : beat; };
    while (true)
    {
        const double hb = swung (r.nextBeat);
        if (r.offPending && r.offBeat < b1 && r.offBeat <= hb) { endNote (at (r.offBeat)); continue; }
        if (hb >= b1) break;
        hit (at (hb), hb);
        r.nextBeat += step;
    }
}

int Engine::stepForRecording() const { return curStep.load(); }

void Engine::recordKnob (int pattern, int track, int param, float value)
{
    if (! (recording.load() && playingFlag.load())) return;
    const ScopedEdit edit (*this);
    auto& tr = proj.patterns[(size_t) pattern].slots[(size_t) track].track;
    const int step = std::clamp (curStep.load(), 0, kMaxSteps - 1);
    if (auto* lane = tr.lockFor (param, true)) lane->values[(size_t) step] = value;
    if (takeOpen.load() && pattern == takePattern) takeAuto.push_back ({ track, param, step, value, false });
}

void Engine::touchParam (int pattern, int track, int param, float value)
{
    if (! (recording.load() && playingFlag.load()) || countingIn.load()) return;
    recordKnob (pattern, track, param, value);
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (auto& t : touched) if (t.pattern == pattern && t.track == track && t.param == param) { t.value = value; t.ms = now; return; }
    touched.push_back ({ pattern, track, param, value, now });
}

void Engine::recordMacro (const DeviceProcessor* dev, int macro, float value)
{
    if (! (recording.load() && playingFlag.load()) || countingIn.load() || dev == nullptr) return;
    for (int p = 0; p < kPatterns; ++p)
        for (int t = 0; t < kTracks; ++t)
        {
            if (devices[(size_t) p][(size_t) t].get() != dev) continue;
            auto& tr = proj.patterns[(size_t) p].slots[(size_t) t].track;
            if (tr.pianoRoll && p == seq.activePattern)
            {
                // a clip: the macro's lane, at the playing position
                const ScopedEdit edit (*this);
                const int tick = std::clamp ((int) std::lround ((curStep.load() + stepPhaseNow.load()) * proj.stepTicks (p)), 0, proj.patternTicks (p));
                tr.setPoint (100 + macro, tick, value);
                if (takeOpen.load() && p == takePattern) takeAuto.push_back ({ t, 100 + macro, tick, value, true });
            }
            else
            {
                // steps: locks for every parameter the macro moves
                const auto& mm = const_cast<DeviceProcessor*> (dev)->macros()[(size_t) macro];
                for (int i = 0; i < mm.count; ++i) touchParam (p, t, mm.targets[(size_t) i].param, dev->get (mm.targets[(size_t) i].param));
            }
            return;
        }
}

void Engine::recordTouched()
{
    // a control still being turned writes its value on each step the playhead reaches (no gaps in a slow sweep)
    if (touched.empty()) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    touched.erase (std::remove_if (touched.begin(), touched.end(), [now] (const Touched& t) { return now - t.ms > 300.0; }), touched.end());
    if (! (recording.load() && playingFlag.load()) || countingIn.load()) { touched.clear(); return; }
    for (auto& t : touched) recordKnob (t.pattern, t.track, t.param, t.value);
}

void Engine::stopAllDevices (bool kill)
{
    for (auto& row : devices)
        for (auto& d : row)
            if (d)
            {
                NoteEvent e; e.type = kill ? NoteEvent::Panic : NoteEvent::AllNotesOff;
                d->handleEvent (e);
                d->clearLocks();
                d->resetExpression();   // controllers back to rest (bend centred, no vibrato, full expression)
            }
    if (kill) { masterBus.reset(); clearTails(); }
}

// ---------------------------------------------------------------------------------------------
void Engine::routeMidi (const juce::MidiMessage& m, int offset, std::vector<SeqEvent>& out)
{
    const bool plugin = engineMode == Mode::Plugin;
    const int ch = m.getChannel();
    // every channel plays the selected track (or, as an option, channels 1–8 play tracks 1–8)
    const int track = plugin ? 0 : ((proj.channelPerTrack && ch >= 1 && ch <= kTracks) ? ch - 1 : focusTrack.load());
    const int pattern = plugin ? 0 : focusPattern.load();
    if (m.isSustainPedalOn()) { sustainDown = true; return; }
    if (m.isSustainPedalOff())
    {
        sustainDown = false;
        for (int t = 0; t < kTracks; ++t)
            for (int n = 0; n < 128; ++n)
                if (sustained[(size_t) t][(size_t) n])
                {
                    sustained[(size_t) t][(size_t) n] = false;
                    // the key's release, held back by the pedal: through the input (chord / arp notes end, recording
                    // gets its length), on the track and pattern it was played on
                    inputKey (sustainedPattern[(size_t) t][(size_t) n], t, n, 0.0f, false, offset, out);
                }
        return;
    }
    if (m.isNoteOff() && sustainDown)
    {
        sustained[(size_t) track][(size_t) m.getNoteNumber()] = true;
        sustainedPattern[(size_t) track][(size_t) m.getNoteNumber()] = (int8_t) pattern;
        return;
    }
    if (m.isNoteOn()) sustained[(size_t) track][(size_t) m.getNoteNumber()] = false;
    if (m.isNoteOn() || m.isNoteOff())
        inputKey (pattern, track, m.getNoteNumber(), m.isNoteOn() ? m.getFloatVelocity() : 0.0f, m.isNoteOn(), offset, out);
    else if (m.isPitchWheel() || m.isChannelPressure() || (m.isController() && (m.getControllerNumber() == 1 || m.getControllerNumber() == 11)))
    {
        // performance controllers: pitch bend, mod wheel, expression, channel pressure (played and recorded)
        int lane = 0; float v = 0.0f;
        if (m.isPitchWheel()) { lane = DeviceProcessor::kBend; v = (m.getPitchWheelValue() - 8192) / 8192.0f; }
        else if (m.isChannelPressure()) { lane = DeviceProcessor::kPressure; v = m.getChannelPressureValue() / 127.0f; }
        else { lane = m.getControllerNumber(); v = m.getControllerValue() / 127.0f; }
        SeqEvent e; e.offset = offset; e.pattern = pattern; e.track = track; e.note.type = NoteEvent::Control; e.note.note = lane; e.note.velocity = v;
        pushEvent (out, e);
        if (! plugin) recordControl (track, lane, v, offset);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        SeqEvent e; e.offset = offset; e.pattern = pattern; e.track = track; e.note.type = NoteEvent::AllNotesOff;
        pushEvent (out, e);
        inputs[(size_t) track].reset();
        stepHeld[(size_t) track] = 0;
    }
}

void Engine::inputKey (int pattern, int track, int note, float vel, bool on, int offset, std::vector<SeqEvent>& out, bool fromRoll)
{
    const bool plugin = engineMode == Mode::Plugin;
    if (! fromRoll && ! plugin)
    {
        // note repeat: a key starts (or stops) a roll; its hits come back here from runRolls. A key released after
        // repeat was switched either way ends what its press started.
        bool rolled = false;
        for (auto& r : liveRolls)
            if (r.active.load (std::memory_order_relaxed) && r.track.load() == track && r.note.load() == note)
            {
                r.active.store (false, std::memory_order_relaxed);
                rolled = true;
            }
        if (on && noteRepeat.load (std::memory_order_relaxed))
        {
            for (auto& r : liveRolls)
                if (! r.active.load (std::memory_order_relaxed) && ! r.offPending)
                {
                    r.pattern = pattern; r.track = track; r.note = note; r.vel = vel;
                    r.beats = std::clamp (repeatBeats.load(), 1.0 / 32.0, 4.0);
                    r.startOffset = offset; r.started = false;
                    r.active.store (true, std::memory_order_relaxed);
                    break;
                }
            return;
        }
        if (! on && rolled) return;
    }
    auto* dev = device (pattern, track);
    auto direct = [&] (int n, float v, bool o, int off)
    {
        SeqEvent e; e.offset = off; e.pattern = pattern; e.track = track;
        e.note.type = o ? NoteEvent::NoteOn : NoteEvent::NoteOff; e.note.note = n; e.note.velocity = v;
        pushEvent (out, e);
    };
    const bool stepMode = ! plugin && recording.load() && proj.recordMode == 1;
    if (plugin || dev == nullptr || dev->laneCount() > 0)
    {
        // drums (and the plugin) take the keys as they are: no scale, chord or arp
        direct (note, vel, on, offset);
        if (stepMode) stepRecord (track, &note, 1, vel, on);
        else if (! plugin) recordNote (track, note, vel, on, offset);
        return;
    }
    if (inputPattern[(size_t) track] != pattern) { inputs[(size_t) track].releaseAll (offset); flushInput (track, out); }
    inputPattern[(size_t) track] = pattern;
    int chord[NoteInput::kMaxEmit];
    const int n = inputs[(size_t) track].key (note, vel, on, offset, proj.scale, proj.patterns[(size_t) pattern].slots[(size_t) track].play, chord);
    if (stepMode) stepRecord (track, chord, on ? n : 0, vel, on);
}

void Engine::flushInput (int track, std::vector<SeqEvent>& out)
{
    auto& in = inputs[(size_t) track];
    const bool liveRec = engineMode != Mode::Plugin && recording.load() && proj.recordMode == 0;
    for (auto& o : in.out)
    {
        SeqEvent e; e.offset = o.offset; e.pattern = inputPattern[(size_t) track]; e.track = track;
        e.note.type = o.on ? NoteEvent::NoteOn : NoteEvent::NoteOff; e.note.note = o.note; e.note.velocity = o.vel;
        pushEvent (out, e);
        if (liveRec) recordNote (track, o.note, o.vel, o.on, o.offset);   // live: what you hear is what is recorded (chords, arp)
    }
    in.out.clear();
}

void Engine::stepRecord (int track, const int* notes, int count, float vel, bool on)
{
    // Step recording: the keys held together form one entry at the cursor; when the last key is released the
    // cursor moves on by the step length, and the entry's gate spans that length.
    const int pat = focusPattern.load();
    auto* dev = device (pat, track);
    if (dev == nullptr) return;
    auto& tr = proj.patterns[(size_t) pat].slots[(size_t) track].track;
    const int total = std::max (1, proj.patternSteps (pat));
    const int len = Project::stepLengthSteps (proj.stepLength);
    const int cursor = ((stepRecordCursor.load() % total) + total) % total;
    const bool drum = dev->laneCount() > 0;
    auto& held = stepHeld[(size_t) track];
    if (on)
    {
        const bool fresh = held == 0;
        ++held;
        const auto v = (uint8_t) std::clamp ((int) (vel * 127.0f), 1, 127);
        if (drum)
        {
            if (const int l = dev->laneForNote (notes[0]); l >= 0) { auto& d = tr.lanes[(size_t) l][(size_t) cursor]; d.on = true; d.velocity = v; d.microTiming = 0; }
        }
        else if (tr.pianoRoll)
        {
            // a piano-roll track: the entry goes into its clip at the cursor (on the grid)
            const int stT = proj.stepTicks (pat);
            for (int i = 0; i < count; ++i)
            {
                RecEvent ev; ev.kind = RecEvent::On; ev.pattern = pat; ev.track = track; ev.note = notes[i]; ev.vel = v;
                ev.tick = cursor * stT; ev.len = std::max (1, len * stT - stT / 16);
                pushRec (ev);
                ev.kind = RecEvent::Off;
                pushRec (ev);
            }
        }
        else
        {
            auto& s = tr.steps[(size_t) cursor];
            if (fresh) s.clear();
            for (int i = 0; i < count; ++i) s.addNote (notes[i], v, std::max (16, len * 128 - 8));
            // a longer entry ties into the steps it covers: clear what was there
            for (int k = 1; fresh && k < len; ++k) tr.steps[(size_t) ((cursor + k) % total)].clear();
        }
        return;
    }
    if (held > 0 && --held == 0) stepRecordCursor = (cursor + len) % total;
}

bool Engine::recordPosition (int pat, int offset, double& pos) const
{
    const double spq = sampleRate * 60.0 / std::max (20.0, proj.bpm);
    const double stepLen = proj.stepQuarters (pat) * spq;
    if (offset < blockCountIn)
    {
        // during the count-in: a key struck up to a step before the first downbeat belongs to it
        pos = -(blockCountIn - offset) / stepLen;
        return pos >= -1.0;
    }
    pos = blockStartPos + (offset - blockCountIn) / stepLen;
    const int total = proj.patternSteps (pat);   // after this block's boundary (a grown take is longer)
    if (pos >= total) pos -= total;              // the pattern looped inside this block
    return true;
}

void Engine::recordNote (int track, int note, float vel, bool on, int offset)
{
    // live recording: into the playing pattern, at the nearest step (Quantize keeps some of the timing)
    if (! recording.load() || proj.recordMode != 0 || ! seq.isPlaying()) return;
    const int pat = seq.activePattern;
    auto* dev = device (pat, track);
    if (dev == nullptr) return;
    auto& tr = proj.patterns[(size_t) pat].slots[(size_t) track].track;
    const int total = proj.patternSteps (pat);
    const bool drum = dev->laneCount() > 0;
    int lane = -1;
    if (drum) lane = dev->laneForNote (note);   // the same mapping the device plays with
    const double spq = sampleRate * 60.0 / std::max (20.0, proj.bpm);
    const double stepLen = proj.stepQuarters (pat) * spq;
    double pos = 0.0;
    const bool inTake = recordPosition (pat, offset, pos);
    if (on)
    {
        if (! inTake) return;   // struck during the count-in
        if (pos < 0.0) pos = 0.0;   // early for the take's first downbeat: on it
        const double nearest = std::round (pos);
        int si = ((int) nearest % total + total) % total;
        // Replace grows the pattern at its end (growAtEnd): a note rounded onto the step after the end is the new bar's first
        const bool growing = ! proj.overdub && ! proj.patterns[(size_t) pat].loopOn && proj.patterns[(size_t) pat].lengthBars < proj.maxBarsFor (pat);
        const bool pastEnd = growing && nearest >= total;
        takeLastStep = std::max (takeLastStep.load(), pastEnd ? total : si);
        int micro = (int) std::clamp (std::round ((pos - nearest) * (1.0 - proj.recordQuantize) * 48.0), -24.0, 24.0);
        // a chord: keys struck within 60 ms of the first one go to its step, even across a step's midpoint
        auto& grp = chordGroup[(size_t) track];
        double since = pos - grp.pos;
        if (since < 0.0) since += total;
        if (grp.pattern == pat && grp.step >= 0 && since * stepLen < 0.06 * sampleRate) { si = grp.step; micro = grp.micro; }
        else grp = { pos, si, micro, pat };
        const auto v = (uint8_t) std::clamp ((int) (vel * 127.0f), 1, 127);
        if (drum)
        {
            if (lane < 0) return;
            auto& d = tr.lanes[(size_t) lane][(size_t) si];
            d.on = true; d.velocity = v; d.microTiming = (int8_t) micro;
            return;
        }
        if (! drum)
        {
            // melodic: into the track's piano-roll clip (docs/sequencer.md), via the message thread
            const int stT = proj.stepTicks (pat);
            RecEvent ev; ev.kind = RecEvent::On; ev.pattern = pat; ev.track = track; ev.note = note; ev.vel = v;
            ev.tick = std::max (0, (pastEnd ? total : si) * stT + micro * stT / kTicksPerStep);
            pushRec (ev);
            for (auto& o : openNotes) if (! o.used) { o = { track, note, pastEnd ? total : si, pos, true, true, absAt (pat, offset) }; break; }
            return;
        }
        auto& s = tr.steps[(size_t) si];   // recording adds; it never removes what was there
        s.addNote (note, v, 96);
        s.microTiming = (int8_t) micro;
        for (auto& o : openNotes) if (! o.used) { o = { track, note, si, pos, true, false, absAt (pat, offset) }; break; }
        return;
    }
    for (auto& o : openNotes)
        if (o.used && o.track == track && o.note == note) endOpenNote (o, pat, pos, absAt (pat, offset));
}

double Engine::absAt (int pat, int offset) const
{
    const double stepLen = proj.stepQuarters (pat) * sampleRate * 60.0 / std::max (20.0, proj.bpm);
    return blockAbs + std::max (0, offset - blockCountIn) / stepLen;
}

void Engine::closeOpenNotes()
{
    bool any = false;
    for (auto& o : openNotes) any = any || o.used;
    if (! any) return;
    const int pat = seq.activePattern;
    double pos = 0.0;
    recordPosition (pat, 0, pos);
    for (auto& o : openNotes) if (o.used) endOpenNote (o, pat, std::max (0.0, pos), absAt (pat, 0));
}

void Engine::endOpenNote (OpenNote& o, int pat, double pos, double abs)
{
    const int total = proj.patternSteps (pat);
    auto& tr = proj.patterns[(size_t) pat].slots[(size_t) o.track].track;
    const int track = o.track, note = o.note;
    double len = abs - o.startAbs;   // on the unwrapped clock: held across the loop point, even for several passes
    if (len <= 0.0) { len = pos - o.startPos; if (len < 0.0) len += total; }
    takeLastStep = std::max (takeLastStep.load(), o.step + std::max (0, (int) std::ceil (len - 0.05) - 1));   // the bars a held note reaches
    if (o.clip)
    {
        RecEvent ev; ev.kind = RecEvent::Off; ev.pattern = pat; ev.track = track; ev.note = note;
        ev.len = std::max (1, (int) std::lround (std::max (0.05, len) * proj.stepTicks (pat)));
        pushRec (ev);
        o.used = false;
        return;
    }
    tr.steps[(size_t) o.step].addNote (note, -1, std::clamp ((int) (std::max (0.05, len) * 128.0), 4, kMaxNoteLen));   // this note's length
    o.used = false;
}

void Engine::recordControl (int track, int lane, float value, int offset)
{
    if (! recording.load() || proj.recordMode != 0 || ! seq.isPlaying() || seq.countInSamplesLeft > 0) return;
    const int pat = seq.activePattern;
    auto* dev = device (pat, track);
    if (dev == nullptr || dev->laneCount() > 0) return;
    auto& last = lastRecordedCC[(size_t) track][(size_t) (lane & 255)];
    if (std::abs (last - value) < 0.004f) return;   // thinned: only changes
    last = value;
    double pos = 0.0;
    if (! recordPosition (pat, offset, pos)) return;
    pos = std::max (0.0, pos);
    RecEvent ev; ev.kind = RecEvent::CC; ev.pattern = pat; ev.track = track; ev.note = lane; ev.value = value;
    ev.tick = std::max (0, (int) std::lround (pos * proj.stepTicks (pat)));
    pushRec (ev);
}

void Engine::pushRec (const RecEvent& e)
{
    const auto scope = recFifo.write (1);
    if (scope.blockSize1 > 0) recEvents[(size_t) scope.startIndex1] = e;
    else if (scope.blockSize2 > 0) recEvents[(size_t) scope.startIndex2] = e;
}

bool Engine::applyRecorded()
{
    const int ready = recFifo.getNumReady();
    if (ready == 0) return false;
    if (takeIdsFor != recordTake.load()) { takeIds.clear(); takeIdsFor = recordTake.load(); }
    const ScopedEdit edit (*this);
    const auto scope = recFifo.read (ready);
    auto run = [this] (int start, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const auto& e = recEvents[(size_t) (start + i)];
            auto& tr = proj.patterns[(size_t) e.pattern].slots[(size_t) e.track].track;
            auto& open = recOpen[(size_t) e.track][(size_t) (e.note & 127)];
            if (e.kind == RecEvent::On)
            {
                if (! tr.pianoRoll)
                {
                    tr.convertToPianoRoll (proj.stepTicks (e.pattern), proj.patternSteps (e.pattern));
                    tr.clip.reserve (tr.clip.size() * (size_t) (kMaxBars + 1) + 256);   // room for doubling on the audio thread
                }
                ClipNote n;
                n.start = e.tick; n.length = std::max (1, e.len > 0 ? e.len : proj.stepTicks (e.pattern) / 4);
                n.note = (uint8_t) e.note; n.vel = (uint8_t) std::clamp (e.vel, 1, 127);
                // still held: silent in playback until the key is released (a loop coming round would otherwise
                // play its placeholder length and cut the note being held)
                n.muted = true;
                open = tr.addClipNote (n);
                takeIds.push_back (open);
            }
            else if (e.kind == RecEvent::CC)
            {
                if (! tr.pianoRoll) tr.convertToPianoRoll (proj.stepTicks (e.pattern), proj.patternSteps (e.pattern));
                tr.setPoint (e.note, e.tick, e.value);
                if (takeOpen.load() && e.pattern == takePattern) takeAuto.push_back ({ e.track, e.note, e.tick, e.value, true });
            }
            else if (e.kind == RecEvent::Off)
            {
                if (auto* n = tr.clipNote (open)) { n->length = std::max (1, e.len); n->muted = false; }
                open = 0;
            }
            else if (tr.pianoRoll)
            {
                // Replace: what was under the playhead goes (not what this take wrote)
                tr.clip.erase (std::remove_if (tr.clip.begin(), tr.clip.end(), [&] (const ClipNote& n)
                {
                    return n.start >= e.tick && n.start < e.end && std::find (takeIds.begin(), takeIds.end(), n.id) == takeIds.end();
                }), tr.clip.end());
                tr.erasePoints (-1, e.tick, e.end);
            }
        }
    };
    run (scope.startIndex1, scope.blockSize1);
    run (scope.startIndex2, scope.blockSize2);
    return true;
}

void Engine::applyEvent (const SeqEvent& e)
{
    if (e.kind == SeqEvent::Click)
    {
        clickEnv = 1.0f; clickFreq = e.value > 0.5f ? 1600.0f : 1050.0f; clickPhase = 0.0;
        return;
    }
    auto* d = device (e.pattern, e.track);
    if (d == nullptr) return;
    switch (e.kind)
    {
        case SeqEvent::Note:
            if (e.note.type == NoteEvent::Control)
            {
                // a controller (MIDI input, a clip's lane): expression, or a macro (100 + m) the lane automates
                if (e.note.note >= 100 && e.note.note < 108) d->setMacro (e.note.note - 100, e.note.velocity);
                else d->setExpression (e.note.note, e.note.velocity);
            }
            else d->handleEvent (e.note);
            break;
        case SeqEvent::ClearLocks: d->clearLocks(); break;
        case SeqEvent::Lock: d->setLock (e.param, e.value); break;
        default: break;
    }
}

void Engine::renderSub (float* L, float* R, int start, int len)
{
    if (len <= 0) return;
    const bool plugin = engineMode == Mode::Plugin;
    const double spq = sampleRate * 60.0 / std::max (20.0, proj.bpm);
    TransportInfo ti;
    ti.bpm = proj.bpm;
    ti.playing = seq.isPlaying();
    ti.beatPos = seq.beatPosition() + start / spq;
    if (plugin) { ti.bpm = hostBpm; ti.playing = hostPlaying; ti.beatPos = hostBeat + start / (sampleRate * 60.0 / std::max (20.0, hostBpm)); }
    const int active = plugin ? 0 : seq.activePattern;
    const int focus = focusPattern.load();
    // the playing pattern, the patterns still ringing out, the focused one (auditioned notes): each once
    std::array<int, kPatterns + 1> pats;
    int nPats = 0;
    auto addPat = [&] (int p) { if (p >= 0 && std::find (pats.begin(), pats.begin() + nPats, p) == pats.begin() + nPats) pats[(size_t) nPats++] = p; };
    addPat (active);
    if (! plugin) { for (int p = 0; p < kPatterns; ++p) if (tailing[(size_t) p]) addPat (p); addPat (focus); }

    for (int pi = 0; pi < nPats; ++pi)
    {
        const int p = pats[(size_t) pi];
        auto& pattern = proj.patterns[(size_t) p];
        bool anySolo = false;
        for (auto& s : pattern.slots) anySolo |= s.mixer.solo;
        for (int t = 0; t < (plugin ? 1 : kTracks); ++t)
        {
            auto* d = devices[(size_t) p][(size_t) t].get();
            if (d == nullptr) continue;
            d->outerArp = ! plugin && pattern.slots[(size_t) t].play.arpOn;   // before a sleeping device wakes on a key
            // sleeping: keeps state, costs no CPU; unless its track's effects still ring (a delay, a reverb)
            const bool fx = masterInsert != nullptr && masterInsert->trackActive (p, t);
            const bool asleep = d->isSilent();
            auto& fxTail = trackFxTail[(size_t) p][(size_t) t];
            if (! asleep) fxTail = fx ? (int) (4.0 * sampleRate) : 0;
            else if (fx && fxTail > 0) fxTail -= len;
            else continue;
            std::fill (slotL.begin() + start, slotL.begin() + start + len, 0.0f);
            std::fill (slotR.begin() + start, slotR.begin() + start + len, 0.0f);
            for (auto* v : { &auxAL, &auxAR, &auxBL, &auxBR }) std::fill (v->begin() + start, v->begin() + start + len, 0.0f);
            d->setAuxBuffers (auxAL.data() + start, auxAR.data() + start, auxBL.data() + start, auxBR.data() + start);
            d->setTransport (ti);
            // the plugin has no rack strip sends: its host Reverb Return scales the instrument's own Reverb card
            d->reverbScale = plugin ? proj.master.reverbReturn / 0.8f : 1.0f;
            if (! asleep) d->render (slotL.data() + start, slotR.data() + start, len);
            if (fx) masterInsert->processTrack (p, t, slotL.data() + start, slotR.data() + start, len, ti.bpm, ti.beatPos, ti.playing);
            const auto& m = pattern.slots[(size_t) t].mixer;
            const bool audible = ! m.mute && (! anySolo || m.solo);
            const float g = audible ? m.level * d->expressionGain : 0.0f;
            const float gl = g * std::sqrt (0.5f * (1.0f - m.pan)) * 1.41421f, gr = g * std::sqrt (0.5f * (1.0f + m.pan)) * 1.41421f;
            float pk = 0.0f;
            float* aL = masterBus.sendA (0) + start; float* aR = masterBus.sendA (1) + start;
            float* bL = masterBus.sendB (0) + start; float* bR = masterBus.sendB (1) + start;
            // a pattern ringing out after a switch fades over its last 50 ms before
            // the 4 s limit panics it (it used to be cut hard)
            const bool tail = ! plugin && tailing[(size_t) p] && p != active;
            const double tailEnd = tailSecs() * sampleRate, tailFade = 0.05 * sampleRate;
            for (int i = 0; i < len; ++i)
            {
                float tg = 1.0f;
                if (tail)
                {
                    const double left = tailEnd - (double) (tailSamples[(size_t) p] + start + i);
                    tg = left >= tailFade ? 1.0f : (float) std::max (0.0, left / tailFade);
                }
                const float l = slotL[(size_t) (start + i)] * gl * tg, r = slotR[(size_t) (start + i)] * gr * tg;
                const float ga = g * tg;
                L[start + i] += l; R[start + i] += r;
                aL[i] += l * m.sendA + auxAL[(size_t) (start + i)] * ga; aR[i] += r * m.sendA + auxAR[(size_t) (start + i)] * ga;
                bL[i] += l * m.sendB + auxBL[(size_t) (start + i)] * ga; bR[i] += r * m.sendB + auxBR[(size_t) (start + i)] * ga;
                pk = std::max ({ pk, std::abs (l), std::abs (r) });
            }
            slotPeak[p][t].store (std::max (pk, slotPeak[p][t].load() * 0.92f));
        }
    }
    // metronome click
    if (clickEnv > 1.0e-4f)
    {
        const float lvl = proj.clickLevel * 0.35f;
        const float decay = std::exp (-1.0f / (0.012f * (float) sampleRate));
        for (int i = start; i < start + len; ++i)
        {
            const float s = std::sin ((float) clickPhase) * clickEnv * lvl;
            clickPhase += dsp::kTwoPi * clickFreq / sampleRate;
            clickEnv *= decay;
            clickBuf[(size_t) i] += s;
        }
    }
}

void Engine::process (float* L, float* R, int n, const juce::MidiBuffer* midi)
{
    if (n <= maxBlock) { processBlock (L, R, n, midi); return; }
    // a block longer than prepared for (offline bounces, large device buffers): in maxBlock pieces, MIDI with them
    for (int pos = 0; pos < n; pos += maxBlock)
    {
        const int len = std::min (maxBlock, n - pos);
        chunkMidi.clear();
        if (midi != nullptr) chunkMidi.addEvents (*midi, pos, len, -pos);
        processBlock (L + pos, R + pos, len, &chunkMidi);
    }
}

void Engine::processBlock (float* L, float* R, int n, const juce::MidiBuffer* midi)
{
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);
    std::fill (clickBuf.begin(), clickBuf.begin() + n, 0.0f);
    const auto t0 = juce::Time::getHighResolutionTicks();
    const AudioLock::ScopedLockType sl (audioLock);
    const bool plugin = engineMode == Mode::Plugin;

    // transport commands
    if (const int s = cmdStop.exchange (0))
    {
        // Stop: back to the pattern's start (tails ring out); a second Stop also cuts the tails
        if (s == 1 && seq.isPlaying()) { seq.stop(); seq.resetPosition(); curStep = 0; progress = 0.0f; stopAllDevices (false); }
        else { seq.stop(); seq.resetPosition(); stopAllDevices (true); stepRecordCursor = 0; }
        playingFlag = false; countingIn = false;
    }
    if (cmdPlay.exchange (0) && ! plugin && ! seq.isPlaying())
    {
        seq.recordArmed = recording.load();
        seq.start (proj, false);
        playingFlag = true;
    }
    if (const int s = cmdSeek.exchange (-1); s >= 0 && ! plugin)
    {
        if (seq.isPlaying()) stopAllDevices (false);
        const int to = std::clamp (s, 0, std::max (0, proj.patternSteps (seq.activePattern) - 1));
        seq.seek (to, to * proj.stepQuarters (seq.activePattern));
        curStep = seq.currentStep();
    stepPhaseNow = (float) seq.currentStepPhase();
    }
    if (cmdToStart.exchange (0))
    {
        stepRecordCursor = 0;
        stepHeld.fill (0);
        if (seq.isPlaying() && ! plugin)
        {
            stopAllDevices (false);
            const bool armed = seq.recordArmed;
            seq.recordArmed = false;   // no count-in when jumping back
            seq.start (proj, true);
            seq.recordArmed = armed;
        }
        else { seq.resetPosition(); curStep = 0; progress = 0.0f; }
    }
    if (const int d = cmdStepMove.exchange (0))
    {
        const int total = std::max (1, proj.patternSteps (focusPattern.load()));
        stepRecordCursor = ((stepRecordCursor.load() + d) % total + total) % total;
    }
    if (const int c = cmdStep.exchange (0))
    {
        const int pat = focusPattern.load(), total = std::max (1, proj.patternSteps (pat));
        const int len = Project::stepLengthSteps (proj.stepLength);
        int cur = ((stepRecordCursor.load() % total) + total) % total;
        if (c == 1) cur = (cur + len) % total;   // rest: leave a gap
        else
        {
            cur = ((cur - len) % total + total) % total;   // back: step back and clear what was there
            auto& tr = proj.patterns[(size_t) pat].slots[(size_t) focusTrack.load()].track;
            for (int k = 0; k < len; ++k)
            {
                const int si = (cur + k) % total;
                tr.steps[(size_t) si].clear();
                for (auto& lane : tr.lanes) lane[(size_t) si].clear();
            }
        }
        stepRecordCursor = cur;
    }
    const int q = pendingQueue.exchange (-2);
    if (q != -2 && ! plugin)
    {
        if (! seq.isPlaying() && q >= 0) { seq.activePattern = q; seq.resetPosition(); focusPattern = q; }
        else seq.queuedPattern = (q == seq.activePattern) ? -1 : q;
    }

    events.clear();
    blockLen = n;
    blockStartPos = seq.position();
    blockCountIn = seq.isPlaying() ? seq.countInSamplesLeft : 0;
    {
        // the unwrapped clock: a key held for several passes gets its whole length
        blockAbs = absSteps;
        const double stepLen = proj.stepQuarters (seq.activePattern) * sampleRate * 60.0 / std::max (20.0, proj.bpm);
        if (seq.isPlaying()) absSteps += std::max (0, n - blockCountIn) / stepLen; else absSteps = 0.0;
    }
    if (! plugin && ! recording.load()) closeOpenNotes();   // the take ended with keys still held
    masterBus.clearSends (n);
    if (! plugin)
    {
        seq.generate (proj, *this, n, events);
        if (seq.switchedThisBlock)
        {
            const int from = seq.lastSwitchedFrom;
            for (auto& d : devices[(size_t) from]) if (d) { d->allNotesOff(); d->clearLocks(); }
            tailing[(size_t) from] = true;
            tailSamples[(size_t) from] = 0;
            tailing[(size_t) seq.activePattern] = false;   // playing again: no longer a tail
            if (focusPattern.load() == from) focusPattern = seq.activePattern;
        }
    }
    if (midi != nullptr)
        for (const auto meta : *midi)
            routeMidi (meta.getMessage(), std::clamp (meta.samplePosition, 0, n - 1), events);
    {
        int s1, n1, s2, n2;
        uiFifo.prepareToRead (uiFifo.getNumReady(), s1, n1, s2, n2);
        // a key released in the same block it was pressed (a tap shorter than the audio callback): its off goes a
        // sample after its on; at the same instant the offs-first order below would play the off first and the
        // note would ring on
        std::array<std::pair<int, int>, 32> struck;
        int numStruck = 0;
        auto offsetFor = [&] (int track, int note, bool on)
        {
            if (on) { if (numStruck < (int) struck.size()) struck[(size_t) numStruck++] = { track, note }; return 0; }
            for (int k = 0; k < numStruck; ++k) if (struck[(size_t) k] == std::make_pair (track, note)) return std::min (1, n - 1);
            return 0;
        };
        auto take = [&] (int start, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                const auto& u = uiNotes[(size_t) (start + i)];
                const int off = offsetFor (u.track, u.note, u.on);
                if (u.audition)
                {
                    SeqEvent ev; ev.offset = off; ev.pattern = plugin ? 0 : u.pattern; ev.track = plugin ? 0 : u.track;
                    ev.note.type = u.on ? NoteEvent::NoteOn : NoteEvent::NoteOff; ev.note.note = u.note; ev.note.velocity = u.vel;
                    pushEvent (events, ev);
                    continue;
                }
                inputKey (plugin ? 0 : u.pattern, plugin ? 0 : u.track, u.note, u.vel, u.on, off, events);
            }
        };
        take (s1, n1); take (s2, n2);
        uiFifo.finishedRead (n1 + n2);
    }
    if (! plugin) runRolls (n, events);
    // the selected track changed: whatever the old one's input was sounding stops (arp, latched chords)
    if (! plugin && (focusTrack.load() != lastFocusTrack || focusPattern.load() != lastFocusPattern))
    {
        inputs[(size_t) lastFocusTrack].releaseAll (0);
        lastFocusTrack = focusTrack.load(); lastFocusPattern = focusPattern.load();
    }
    // live input: arp steps and strummed chord notes, then everything it emitted this block
    {
        const bool synced = seq.isPlaying() && seq.countInSamplesLeft <= 0;
        for (int t = 0; t < (plugin ? 1 : kTracks); ++t)
        {
            inputs[(size_t) t].tick (n, proj.patterns[(size_t) inputPattern[(size_t) t]].slots[(size_t) t].play, synced, seq.beatPosition(), proj.bpm);
            flushInput (t, events);
        }
    }
    // insertion sort (stable, allocation-free): locks before notes, note-offs before note-ons at the same instant
    auto rank = [] (const SeqEvent& e) { return e.kind == SeqEvent::ClearLocks ? 0 : e.kind == SeqEvent::Lock ? 1
                                                : (e.kind == SeqEvent::Note && e.note.type != NoteEvent::NoteOn) ? 2 : 3; };
    for (size_t i = 1; i < events.size(); ++i)
    {
        const SeqEvent cur = events[i];
        size_t j = i;
        while (j > 0 && (events[j - 1].offset > cur.offset || (events[j - 1].offset == cur.offset && rank (events[j - 1]) > rank (cur))))
        {
            events[j] = events[j - 1];
            --j;
        }
        events[j] = cur;
    }

    int pos = 0;
    for (const auto& e : events)
    {
        if (e.offset > pos) { renderSub (L, R, pos, e.offset - pos); pos = e.offset; }
        applyEvent (e);
    }
    renderSub (L, R, pos, n - pos);

    // tail handoff: outgoing devices ring until silent or 4 s, then sleep
    for (int tp = 0; tp < kPatterns; ++tp)
    {
        if (! tailing[(size_t) tp]) continue;
        tailSamples[(size_t) tp] += n;
        bool silent = true;
        for (auto& d : devices[(size_t) tp]) if (d && ! d->isSilent()) silent = false;
        if (silent || tailSamples[(size_t) tp] > (int64_t) (tailSecs() * sampleRate))
        {
            for (auto& d : devices[(size_t) tp]) if (d) { NoteEvent e; e.type = NoteEvent::Panic; d->handleEvent (e); }
            tailing[(size_t) tp] = false;
        }
    }

    const bool clockRuns = seq.isPlaying();
    if (clockRuns && ! clockWasPlaying) beatClock = 0.0;   // Play: the effects' grid starts with the pattern
    clockWasPlaying = clockRuns;
    masterBus.process (L, R, n, proj.master, proj.bpm, masterInsert, beatClock, clockRuns);
    {   // the click, after the bus (renderSub wrote it to clickBuf), as late as the bus's look-ahead makes the music
        const int ring = (int) clickRing.size(), lat = std::clamp (masterBus.latency(), 0, ring - 1);
        for (int i = 0; i < n; ++i)
        {
            clickRing[(size_t) clickRingPos] = clickBuf[(size_t) i];
            const float c = clickRing[(size_t) ((clickRingPos - lat + ring) % ring)];
            clickRingPos = (clickRingPos + 1) % ring;
            L[i] += c; R[i] += c;
        }
    }
    beatClock += n * std::max (20.0, proj.bpm) / (60.0 * sampleRate);

    // publish state for the UI
    activePat = plugin ? 0 : seq.activePattern;
    queuedPat = seq.queuedPattern;
    curStep = seq.currentStep();
    stepPhaseNow = (float) seq.currentStepPhase();
    countingIn = seq.countInSamplesLeft > 0;
    const int total = proj.patternSteps (seq.activePattern);
    progress = seq.isPlaying() ? (float) ((seq.currentStep() + seq.currentStepPhase()) / std::max (1, total)) : 0.0f;
    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    cpuLoad = (float) (secs / (n / sampleRate));
}
} // namespace mb
