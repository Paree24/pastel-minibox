#include "NoteInput.h"
#include "Arpeggiate.h"
#include <algorithm>

namespace mb
{
namespace
{
const std::vector<int>& intervalsFor (const ScaleSettings& s)
{
    const auto& f = Scales::factoryScales();
    return f[(size_t) std::clamp (s.scale, 0, (int) f.size() - 1)].intervals;
}

double rateBeats (int r) { return arp::rateBeats (r); }   // one table with the written-notes arpeggiator
} // namespace

void NoteInput::prepare (double sampleRate)
{
    sr = sampleRate;
    out.reserve (512);
    reset();
}

void NoteInput::reset()
{
    for (auto& v : voices) v = Voice();
    numVoices = 0;
    std::fill (std::begin (outRefCount), std::end (outRefCount), 0);
    numPendingOns = 0;
    clearPool();
    arpNote = -1; gateOffIn = -1; nextPpq = -1.0; arpImmediate = false;
}

void NoteInput::releaseAll (int offset)
{
    numPendingOns = 0;
    for (int n = 0; n < 128; ++n)
        if (outRefCount[n] > 0) { outRefCount[n] = 0; emit (offset, n, 0.0f, false); }
    stopArpNote (offset);
    for (auto& v : voices) v = Voice();
    numVoices = 0;
    clearPool();
}

int NoteInput::buildChord (int snapped, const ScaleSettings& sc, const PlaySettings& ps, int* chord)
{
    if (! ps.chordOn) { chord[0] = snapped; return 1; }
    const auto& iv = intervalsFor (sc);
    const auto& facts = Scales::factoryScales();
    const bool powerDefault = facts[(size_t) std::clamp (sc.scale, 0, (int) facts.size() - 1)].powerChords;
    const int deg = Scales::scaleDegree (snapped, sc.root, iv);
    int rel[kMaxEmit] { 0 };
    int nRel = 1;
    if (deg >= 0)
    {
        const int forced = ps.chordType[(size_t) (deg % 12)];
        if (forced == 0) nRel = 1;                                               // Single
        else if (forced > 0) nRel = Scales::forcedChordIntervals (forced, rel);  // explicit chord for this degree
        else if (powerDefault) nRel = Scales::forcedChordIntervals (7, rel);     // power chords for pentatonic-style scales
        else nRel = Scales::diatonicTriad (deg, iv, rel);                        // Auto: the diatonic triad
    }
    else if (ps.chordType[0] > 0) nRel = Scales::forcedChordIntervals (ps.chordType[0], rel);   // out of scale (lock off)
    int count = 0;
    for (int k = 0; k < nRel; ++k) { const int n = snapped + rel[k]; if (n >= 0 && n <= 127) chord[count++] = n; }
    applyInversionBass (chord, count, ps.inversion, ps.invLower, ps.bassBelow);
    std::sort (chord, chord + count);
    return (int) (std::unique (chord, chord + count) - chord);
}

void NoteInput::applyInversionBass (int* chord, int& count, int inversion, bool lower, bool bassBelow)
{
    if (count > 1)
    {
        for (int k = 0; k < inversion && k < 2; ++k)
        {
            const int up = chord[0] + 12;
            if (up > 127) break;
            std::rotate (chord, chord + 1, chord + count);
            chord[count - 1] = up;
        }
        std::sort (chord, chord + count);
    }
    if (lower && count > 0)   // the same shape an octave down
    {
        int down[kMaxEmit], nd = 0;
        for (int k = 0; k < count; ++k) if (chord[k] - 12 >= 0) down[nd++] = chord[k] - 12;
        if (nd > 0) { std::copy (down, down + nd, chord); count = nd; }
    }
    if (bassBelow && count > 0 && count < kMaxEmit && chord[0] - 12 >= 0)
    {
        std::copy_backward (chord, chord + count, chord + count + 1);
        chord[0] = chord[1] - 12;
        ++count;
    }
}

void NoteInput::voiceOn (int offset, int note, float vel)
{
    if (outRefCount[note]++ > 0) emit (offset, note, 0.0f, false);   // re-strike a note two keys share
    emit (offset, note, vel, true);
}

void NoteInput::voiceOff (int offset, int note)
{
    if (outRefCount[note] > 0 && --outRefCount[note] == 0) emit (offset, note, 0.0f, false);
}

void NoteInput::releaseVoiceNote (int offset, int note)
{
    for (int i = 0; i < numPendingOns; ++i)
        if (pendingOns[i].note == note)   // strummed but not sounded yet: cancel it
        {
            std::copy (pendingOns + i + 1, pendingOns + numPendingOns, pendingOns + i);
            --numPendingOns;
            return;
        }
    voiceOff (offset, note);
}

void NoteInput::addToPool (const int* notes, int count)
{
    for (int k = 0; k < count; ++k)
    {
        const int n = notes[k] & 127;
        if (poolRef[n]++ == 0)
        {
            const int at = (int) (std::lower_bound (pool, pool + poolSize, n) - pool);
            std::copy_backward (pool + at, pool + poolSize, pool + poolSize + 1);
            pool[at] = n; ++poolSize;
            played[playedSize++] = n;
        }
    }
}

void NoteInput::removeFromPool (const int* notes, int count)
{
    for (int k = 0; k < count; ++k)
    {
        const int n = notes[k] & 127;
        if (poolRef[n] > 0 && --poolRef[n] <= 0)
        {
            poolRef[n] = 0;
            poolSize = (int) (std::remove (pool, pool + poolSize, n) - pool);
            playedSize = (int) (std::remove (played, played + playedSize, n) - played);
        }
    }
}

void NoteInput::clearPool()
{
    poolSize = playedSize = 0;
    std::fill (std::begin (poolRef), std::end (poolRef), 0);
}

int NoteInput::pickArpNote (const PlaySettings& ps)
{
    if (poolSize == 0) return -1;
    const int mode = ps.arpMode, oct = std::clamp (ps.arpOct + 1, 1, 4);
    const int* src = mode == 5 ? played : pool;
    const int srcSize = mode == 5 ? playedSize : poolSize;
    int N = 0;
    for (int o = 0; o < oct; ++o)
        for (int i = 0; i < srcSize; ++i) { const int v = src[i] + 12 * o; if (v <= 127) expanded[N++] = v; }
    if (N == 0) return -1;
    switch (mode)
    {
        case 1: return expanded[N - 1 - (arpStep % N)];                                                  // Down
        case 2: { if (N == 1) return expanded[0]; const int c = 2 * N - 2, p = arpStep % c; return p < N ? expanded[p] : expanded[c - p]; }        // Up-Down
        case 3: { if (N == 1) return expanded[0]; const int c = 2 * N - 2, p = arpStep % c; return p < N ? expanded[N - 1 - p] : expanded[p - (N - 1)]; }   // Down-Up
        case 4: return expanded[(int) (rng() % (unsigned) N)];                                        // Random
        default: return expanded[arpStep % N];                                                         // Up, Played
    }
}

void NoteInput::stopArpNote (int offset)
{
    if (arpNote >= 0) emit (offset, arpNote, 0.0f, false);
    arpNote = -1; gateOffIn = -1;
}

int NoteInput::key (int inNote, float vel, bool on, int offset, const ScaleSettings& sc, const PlaySettings& ps, int* chordOut)
{
    inNote &= 127;
    const bool arpOn = ps.arpOn;
    if (arpOn != prevArpOn)   // the arp switched while keys are held: hand them over instead of leaving them stuck
    {
        if (arpOn)
        {
            clearPool();
            for (auto& v : voices)
                if (v.active) { for (int k = 0; k < v.numEmitted; ++k) releaseVoiceNote (offset, v.emitted[k]); addToPool (v.emitted, v.numEmitted); }
        }
        else { clearPool(); stopArpNote (offset); for (auto& v : voices) v = Voice(); numVoices = 0; }
        prevArpOn = arpOn;
    }
    if (! ps.arpLatch && prevLatch && arpOn)   // latch released: keep only the keys still held
    {
        clearPool();
        for (auto& v : voices) if (v.active) addToPool (v.emitted, v.numEmitted);
    }
    prevLatch = ps.arpLatch;

    if (on)
    {
        const auto& iv = intervalsFor (sc);
        int snapped = inNote;
        // White keys (play the scale's degrees) and Lock (snap off-scale keys) are separate project-wide toggles
        if (sc.whiteKeys) snapped = Scales::degreeMapNote (inNote, sc.root, iv);
        else if (sc.lock) snapped = Scales::snapNote (inNote, sc.root, iv, sc.snap);
        int chord[kMaxEmit];
        const int size = buildChord (snapped, sc, ps, chord);
        const bool poolWasEmpty = poolSize == 0;
        auto& old = voices[inNote];
        if (old.active)   // the same key again: release its old notes first
        {
            if (arpOn) removeFromPool (old.emitted, old.numEmitted);
            else for (int k = 0; k < old.numEmitted; ++k) releaseVoiceNote (offset, old.emitted[k]);
            old.active = false; --numVoices;
        }
        if (arpOn)
        {
            if (ps.arpLatch && numVoices == 0 && ! poolWasEmpty) { clearPool(); arpStep = 0; }   // a new latched chord replaces the old
            addToPool (chord, size);
            arpVel = vel;
            if (poolWasEmpty) { arpStep = 0; arpImmediate = true; arpImmediatePos = offset; }   // the first step fires on the key
        }
        else
        {
            const int strum = (int) (ps.strumMs * sr / 1000.0);
            for (int i = 0; i < size; ++i)
            {
                if (i == 0 || strum <= 0 || numPendingOns >= 64) voiceOn (offset, chord[i], vel);
                else pendingOns[numPendingOns++] = { clock + offset + (int64_t) i * strum, chord[i], vel };
            }
        }
        auto& v = voices[inNote];
        v.active = true; ++numVoices;
        v.numEmitted = size;
        std::copy (chord, chord + size, v.emitted);
        if (chordOut != nullptr) std::copy (chord, chord + size, chordOut);
        return size;
    }
    auto& v = voices[inNote];
    if (! v.active) return 0;
    if (arpOn)
    {
        if (! ps.arpLatch)
        {
            removeFromPool (v.emitted, v.numEmitted);
            if (poolSize == 0) { stopArpNote (offset); nextPpq = -1.0; }
        }
    }
    else for (int k = 0; k < v.numEmitted; ++k) releaseVoiceNote (offset, v.emitted[k]);
    v.active = false; --numVoices;
    return 0;
}

void NoteInput::tick (int n, const PlaySettings& ps, bool synced, double ppq, double bpm, const Project* swingOf)
{
    if (ps.arpOn && poolSize > 0)
    {
        const double interval = rateBeats (ps.arpRate);
        const double swingFrac = std::clamp (ps.arpSwing, 0.0f, 60.0f) / 100.0;
        const double gateFrac = std::clamp (ps.arpGate / 100.0f, 0.1f, 1.0f);
        const double spb = 60.0 / std::max (20.0, bpm) * sr;
        const double tickSamples = interval * spb;
        auto swingBeats = [&] { return (arpStep % 2) == 1 ? swingFrac * interval * 0.5 : 0.0; };
        auto fire = [&] (int at, double stepLen)
        {
            if (arpNote >= 0) emit ((gateOffIn >= 0 && gateOffIn <= at) ? gateOffIn : at, arpNote, 0.0f, false);
            arpNote = -1; gateOffIn = -1;
            const int note = pickArpNote (ps);
            if (note >= 0)
            {
                emit (at, note, arpVel, true);
                arpNote = note;
                if (gateFrac < 0.98) gateOffIn = at + std::max (1, (int) (stepLen * gateFrac));
            }
            ++arpStep;
        };
        if (synced != arpWasSynced) { if (synced) nextPpq = -1.0; else samplesToNextTick = tickSamples; arpWasSynced = synced; }
        if (arpImmediate)
        {
            arpImmediate = false;
            fire (arpImmediatePos, tickSamples);
            if (synced) nextPpq = ppq + arpImmediatePos / spb + interval;
            else samplesToNextTick = arpImmediatePos + tickSamples;
        }
        int guard = 0;
        if (synced)
        {
            const double endPpq = ppq + n / spb;
            if (nextPpq < 0.0 || nextPpq < ppq - interval * 0.5 || nextPpq > endPpq + interval)
                nextPpq = std::ceil (ppq / interval - 1.0e-9) * interval;
            while (guard++ < 64)
            {
                // its own swing, then the project's (the off-beat 1/16s or 1/8s late, as the steps play)
                const double swing = swingBeats();
                const double at = swingOf != nullptr ? swingOf->swungQuarters (nextPpq + swing) : nextPpq + swing;
                if (at >= endPpq) break;
                const double next = swingOf != nullptr ? swingOf->swungQuarters (nextPpq + interval) : nextPpq + interval;
                fire (std::clamp ((int) ((at - ppq) * spb), 0, n - 1), std::max (1.0, (next - at) * spb));
                nextPpq += interval;
            }
        }
        else
        {
            if (samplesToNextTick < 0.0) samplesToNextTick = 0.0;
            while (guard++ < 64)
            {
                const double swing = swingBeats() * spb, at = samplesToNextTick + swing;
                if (at >= n) break;
                fire (std::clamp ((int) at, 0, n - 1), tickSamples - swing);
                samplesToNextTick += tickSamples;
            }
            samplesToNextTick -= n;
        }
        if (gateOffIn >= 0 && arpNote >= 0)
        {
            if (gateOffIn < n) { emit (gateOffIn, arpNote, 0.0f, false); arpNote = -1; gateOffIn = -1; }
            else gateOffIn -= n;
        }
    }
    else
    {
        arpImmediate = false;
        if (arpNote >= 0 && ! (ps.arpOn && ps.arpLatch)) stopArpNote (0);
        nextPpq = -1.0;
    }
    // strummed chord notes due in this block
    const int64_t end = clock + n;
    int keep = 0;
    for (int i = 0; i < numPendingOns; ++i)
    {
        const auto& p = pendingOns[i];
        if (p.time < end) voiceOn ((int) std::max<int64_t> (0, p.time - clock), p.note, p.vel);
        else pendingOns[keep++] = p;
    }
    numPendingOns = keep;
    clock = end;
}
} // namespace mb
