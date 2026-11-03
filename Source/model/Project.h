#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>
#include <algorithm>

namespace mb
{
constexpr int kTracks = 8, kPatterns = 8, kMaxBars = 16, kMaxSteps = 512, kLanes = 8, kMaxLocks = 8;
constexpr int kMaxNoteLen = 65535;   // 1/128 step: a note can last ~511 steps (the longest pattern)
constexpr int kTicksPerStep = 48;    // a step's micro-timing and per-note offsets are in 1/48 step
constexpr int kAccentFlag = 1, kSlideFlag = 2, kTieFlag = 4;   // as StepFlags (devices)
constexpr int kPPQ = 1920;           // piano-roll clips: ticks per quarter note (a 1/32 step is 240, a step's 1/48 a whole tick)

struct Step
{
    uint8_t noteCount = 0;          // 0 = rest; up to 6 (chords)
    uint8_t notes[6] {};
    uint8_t vel[6] { 100, 100, 100, 100, 100, 100 };    // per note, 1–127
    uint16_t len[6] { 96, 96, 96, 96, 96, 96 };        // per note, 1/128 of a step (>128 rings into later steps)
    int8_t off[6] {};                                   // per note, ±24 ticks (1/48 step) from the step: early or late
    int8_t microTiming = 0;         // ±24 (±1/2 step in 1/48 increments)
    uint8_t probability = 100;      // 0–100 %
    uint8_t ratchet = 1;            // 1–4 repeats
    uint8_t flags = 0;              // kAccent | kSlide | kTie
    uint8_t cond = 0;               // trig condition (trigConditionName): 0 = always
    uint8_t muted = 0;              // per note (bit i = notes[i]): kept but not played
    uint8_t noteLock = 0;           // per note: locked against edits (moving, resizing, deleting, functions)

    bool isMuted (int i) const { return (muted >> i) & 1; }
    void setMuted (int i, bool m) { muted = (uint8_t) (m ? (muted | (1 << i)) : (muted & ~(1 << i))); }
    bool isLocked (int i) const { return (noteLock >> i) & 1; }
    void setLocked (int i, bool l) { noteLock = (uint8_t) (l ? (noteLock | (1 << i)) : (noteLock & ~(1 << i))); }
    int indexOf (int n) const { for (int i = 0; i < noteCount; ++i) if (notes[i] == n) return i; return -1; }
    bool hasNote (int n) const { return indexOf (n) >= 0; }
    /** Adds a note (velocity and length of its own); an existing note keeps its values unless given. */
    void addNote (int n, int velocity = -1, int length = -1)
    {
        int i = indexOf (n);
        if (i < 0)
        {
            if (noteCount >= 6) return;
            i = noteCount++;
            notes[i] = (uint8_t) n;
            vel[i] = noteCount > 1 ? vel[0] : (uint8_t) 100;
            len[i] = noteCount > 1 ? len[0] : (uint16_t) 96;
            off[i] = 0;
            setMuted (i, false);
            setLocked (i, false);
        }
        if (velocity > 0) vel[i] = (uint8_t) std::clamp (velocity, 1, 127);
        if (length > 0) len[i] = (uint16_t) std::clamp (length, 1, kMaxNoteLen);
    }
    void removeNote (int n)
    {
        const int i = indexOf (n);
        if (i < 0) return;
        for (int j = i; j < noteCount - 1; ++j) { notes[j] = notes[j + 1]; vel[j] = vel[j + 1]; len[j] = len[j + 1]; off[j] = off[j + 1]; setMuted (j, isMuted (j + 1)); setLocked (j, isLocked (j + 1)); }
        off[noteCount - 1] = 0;
        setMuted (noteCount - 1, false);
        setLocked (noteCount - 1, false);
        --noteCount;
        if (noteCount == 0) clear();
    }
    /** The step's loudest note and longest length (for lanes and the step grid). */
    uint8_t velocity() const { uint8_t v = 0; for (int i = 0; i < noteCount; ++i) v = std::max (v, vel[i]); return noteCount > 0 ? v : (uint8_t) 100; }
    uint16_t maxLen() const { uint16_t l = 0; for (int i = 0; i < noteCount; ++i) l = std::max (l, len[i]); return noteCount > 0 ? l : (uint16_t) 96; }
    void setVelocity (int v) { for (auto& x : vel) x = (uint8_t) std::clamp (v, 1, 127); }
    void setLength (int l) { for (auto& x : len) x = (uint16_t) std::clamp (l, 1, kMaxNoteLen); }
    /** Where note i starts, in ticks from the pattern start (this step is `index`). */
    int startTick (int index, int i) const { return index * kTicksPerStep + microTiming + off[i]; }
    void clear() { *this = Step(); }
};

struct DrumStep
{
    bool on = false;
    uint8_t velocity = 100;
    uint8_t probability = 100;
    uint8_t ratchet = 1;
    int8_t microTiming = 0;
    uint8_t flags = 0;
    uint8_t cond = 0;               // trig condition, as Step::cond
    void clear() { *this = DrumStep(); }
};

/** Trig conditions (groovebox style): a step plays on the Nth of every M passes of its pattern. */
constexpr int kNumTrigConditions = 12;
inline const char* trigConditionName (int c)
{
    static const char* n[kNumTrigConditions] = { "Always", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "First", "Not first" };
    return n[std::clamp (c, 0, kNumTrigConditions - 1)];
}
/** Whether a step with condition `c` plays on pass `pass` (0 = the first pass since play). */
inline bool trigConditionPasses (int c, int pass)
{
    if (c <= 0) return true;
    if (c == 10) return pass == 0;
    if (c == 11) return pass > 0;
    static const int nm[10][2] = { { 0, 0 }, { 1, 2 }, { 2, 2 }, { 1, 3 }, { 2, 3 }, { 3, 3 }, { 1, 4 }, { 2, 4 }, { 3, 4 }, { 4, 4 } };
    const int n = nm[c][0], m = nm[c][1];
    return pass % m == n - 1;
}

struct LockLane
{
    int param = -1;                     // device parameter index
    std::vector<float> values;          // NaN = no lock
    LockLane() : values ((size_t) kMaxSteps, std::numeric_limits<float>::quiet_NaN()) {}
    bool any() const { for (float v : values) if (! std::isnan (v)) return true; return false; }
};

struct Mixer
{
    float level = 0.8f, pan = 0.0f, sendA = 0.0f, sendB = 0.0f;
    bool mute = false, solo = false;
};

/** A note of a piano-roll clip: anywhere, any length (kPPQ ticks), any number per pitch. */
struct ClipNote
{
    int start = 0, length = kPPQ / 4;
    uint8_t note = 60, vel = 100;
    uint8_t flags = 0;               // kAccent | kSlide | kTie (kept from a converted step sequence)
    uint8_t probability = 100;       // 0–100 %, and a trig condition: a converted step keeps its own
    uint8_t cond = 0;
    bool muted = false, locked = false;   // locked: kept from edits
    uint32_t id = 0;                 // stable while editing (the selection refers to it)
    int end() const { return start + length; }
};

/** A point of a clip's controller lane: from `tick` on the lane holds `value` (pitch bend −1…1, others 0…1).
    Lanes: 0 pitch bend, 1 mod wheel, 11 expression, 128 channel pressure, 100–107 macros 1–8. */
struct CCPoint { int tick = 0; uint8_t lane = 0; float value = 0.0f; };

/** A track of a pattern: a step sequence (steps, or drum lanes) or a piano-roll clip (docs/sequencer.md). */
struct Track
{
    std::vector<Step> steps;
    std::array<std::vector<DrumStep>, kLanes> lanes;
    std::vector<LockLane> locks;
    bool pianoRoll = false;          // the notes are the clip, not the steps
    std::vector<ClipNote> clip;
    std::vector<CCPoint> cc;         // the clip's controller lanes, sorted by tick
    uint32_t nextClipId = 1;
    /** Sets a lane's value from `tick` (replacing a point there). */
    void setPoint (int lane, int tick, float value)
    {
        for (auto& p : cc) if (p.lane == lane && p.tick == tick) { p.value = value; return; }
        CCPoint p { tick, (uint8_t) lane, value };
        cc.insert (std::upper_bound (cc.begin(), cc.end(), p, [] (const CCPoint& a, const CCPoint& b) { return a.tick < b.tick; }), p);
    }
    void erasePoints (int lane, int from, int to)   // [from, to); lane −1: every lane
    {
        cc.erase (std::remove_if (cc.begin(), cc.end(), [&] (const CCPoint& p) { return (lane < 0 || p.lane == lane) && p.tick >= from && p.tick < to; }), cc.end());
    }
    uint32_t addClipNote (ClipNote n) { n.id = nextClipId++; clip.push_back (n); return n.id; }
    ClipNote* clipNote (uint32_t id) { for (auto& n : clip) if (n.id == id) return &n; return nullptr; }
    void removeClipNote (uint32_t id) { clip.erase (std::remove_if (clip.begin(), clip.end(), [id] (const ClipNote& n) { return n.id == id; }), clip.end()); }
    bool hasStepNotes() const { for (auto& s : steps) if (s.noteCount > 0) return true; return false; }
    /** Everything this track plays in its pattern goes: steps, drum hits, the clip, locks, controller lanes. It stays
        a step or a clip track as it was (the editor in view stays). */
    void clearAll()
    {
        for (auto& s : steps) s.clear();
        for (auto& l : lanes) for (auto& d : l) d.clear();
        locks.clear(); clip.clear(); cc.clear();
    }
    /** Steps → clip (one way): every note at its exact position and length; the steps are cleared. */
    void convertToPianoRoll (int stepTicks, int totalSteps)
    {
        if (pianoRoll) return;
        for (int si = 0; si < std::min (totalSteps, (int) steps.size()); ++si)
        {
            const auto& st = steps[(size_t) si];
            for (int i = 0; i < st.noteCount; ++i)
            {
                ClipNote n;
                n.start = std::max (0, si * stepTicks + (st.microTiming + st.off[i]) * stepTicks / kTicksPerStep);
                n.length = (st.flags & (kSlideFlag | kTieFlag)) ? stepTicks + stepTicks / 16 : std::max (1, (int) std::lround (st.len[i] * stepTicks / 128.0));
                n.note = st.notes[i]; n.vel = st.vel[i]; n.muted = st.isMuted (i); n.locked = st.isLocked (i);
                n.flags = (uint8_t) (st.flags & (kAccentFlag | kSlideFlag | kTieFlag));
                n.probability = st.probability; n.cond = st.cond;   // the step's chance and condition stay with its notes
                for (int r = 1; r < std::clamp ((int) st.ratchet, 1, 4); ++r) { auto x = n; x.start += r * stepTicks / st.ratchet; x.length = std::min (x.length, stepTicks / st.ratchet); addClipNote (x); }
                if (st.ratchet > 1) n.length = std::min (n.length, stepTicks / st.ratchet);
                addClipNote (n);
            }
        }
        for (auto& s : steps) s.clear();
        pianoRoll = true;
    }
    /** Clip → steps (the way back): each note to its nearest step, its offset from it kept to 1/48 step, its length,
        velocity, mute and lock its own; notes starting together become a chord. Returns why it can't (nothing
        changes then): more than 6 notes or one pitch twice in a step, notes of one step differing in accent, slide,
        chance or condition, or controller lanes (steps have none). An empty clip always goes back. */
    juce::String convertToSteps (int stepTicks, int totalSteps)
    {
        if (! pianoRoll) return {};
        if (! cc.empty()) return "it has controller lanes (pitch bend, mod wheel …), which steps can't hold";
        const int stT = std::max (1, stepTicks), total = std::max (1, std::min (totalSteps, (int) steps.size()));
        std::vector<Step> out ((size_t) total);
        std::vector<bool> set ((size_t) total, false);
        auto sorted = clip;
        std::sort (sorted.begin(), sorted.end(), [] (const ClipNote& a, const ClipNote& b) { return a.start != b.start ? a.start < b.start : a.note < b.note; });
        for (const auto& n : sorted)
        {
            if (n.start >= total * stT) continue;   // past the pattern's end: never plays
            const int si = (int) std::floor ((n.start + stT * 0.5) / stT);
            const int off = (int) std::lround ((n.start - si * stT) * (double) kTicksPerStep / stT);
            auto& st = out[(size_t) (si % total)];
            const juce::String where = "step " + juce::String (si % total + 1);
            if (st.noteCount >= 6) return where + " would hold more than 6 notes";
            if (st.hasNote (n.note)) return where + " would hold the same note twice (a finer step resolution may fit it)";
            if (! set[(size_t) (si % total)])
            {
                set[(size_t) (si % total)] = true;
                st.flags = (uint8_t) (n.flags & (kAccentFlag | kSlideFlag | kTieFlag)); st.probability = n.probability; st.cond = n.cond;
            }
            else if (st.flags != (n.flags & (kAccentFlag | kSlideFlag | kTieFlag)) || st.probability != n.probability || st.cond != n.cond)
                return "the notes of " + where + " differ in accent, slide or chance";
            const int i = st.noteCount++;
            st.notes[i] = n.note; st.vel[i] = n.vel; st.off[i] = (int8_t) std::clamp (off, -24, 24);
            st.len[i] = (uint16_t) std::clamp ((int) std::lround (n.length * 128.0 / stT), 1, kMaxNoteLen);
            st.setMuted (i, n.muted); st.setLocked (i, n.locked);
        }
        for (auto& x : steps) x.clear();
        for (int i = 0; i < total; ++i) steps[(size_t) i] = out[(size_t) i];
        clip.clear();
        pianoRoll = false;
        return {};
    }
    Track()
    {
        steps.resize ((size_t) kMaxSteps);
        for (auto& l : lanes) l.resize ((size_t) kMaxSteps);
        locks.reserve (kMaxLocks);
    }
    void clear()
    {
        clip.clear();
        cc.clear();
        pianoRoll = false;
        for (auto& s : steps) s.clear();
        for (auto& l : lanes) for (auto& d : l) d.clear();
        locks.clear();
    }
    LockLane* lockFor (int param, bool create)
    {
        for (auto& l : locks) if (l.param == param) return &l;
        if (! create || (int) locks.size() >= kMaxLocks) return nullptr;
        locks.emplace_back();
        locks.back().param = param;
        return &locks.back();
    }
};

/** Project-wide key for live input: scale lock (snap to the scale, or white keys play its degrees). */
struct ScaleSettings
{
    bool lock = false;
    int root = 0;                    // C … B
    int scale = 1;                   // Scales::factoryScales() index (1 = Major)
    int snap = 0;                    // 0 nearest, 1 down, 2 up
    bool whiteKeys = false;
};

/** Per-track live input: chord mode and the arpeggiator (seq/NoteInput). */
struct PlaySettings
{
    bool chordOn = false;
    std::array<int8_t, 12> chordType { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };   // per scale degree: −1 Auto, 0 Single, 1… a chord
    int inversion = 0;               // root, 1st, 2nd
    bool invLower = false, bassBelow = false;
    float strumMs = 0.0f;            // 0–40 ms
    bool arpOn = false, arpLatch = false;
    int arpMode = 0;                 // Up, Down, Up-Down, Down-Up, Random, Played
    int arpRate = 6;                 // 1/4 … 1/64 T (6 = 1/16)
    int arpOct = 0;                  // 1–4 octaves
    float arpSwing = 0.0f, arpGate = 90.0f;   // %
};

/** An effect slot (a track insert, a master or performance effect) of the groovebox (efx/MasterFx, docs/master-fx.md): one of Pastel EFX Grid's
    effects by name ("" = empty), its controls (0–1 knob positions, in the effect's own order), mode, mix, bypass. */
struct FxSlot
{
    static constexpr int kParams = 16;
    juce::String effect;
    std::array<float, kParams> norm {};
    int mode = 0;
    float mix = 1.0f;
    bool bypass = false;
};
constexpr int kTrackFx = 3;   // effect slots on each track's device, before its level, pan and sends

struct SlotModel
{
    Mixer mixer;
    Track track;
    PlaySettings play;
    std::array<FxSlot, kTrackFx> fx;   // in series after the device
};

struct Pattern
{
    int lengthBars = 1;
    int stepRes = 1;                 // 0 = 1/8, 1 = 1/16, 2 = 1/32
    bool loopOn = false;             // play only loopStart … loopEnd (steps, end exclusive) of the pattern
    int loopStart = 0, loopEnd = 0;
    std::array<SlotModel, kTracks> slots;
};

struct MasterSettings
{
    float delayReturn = 0.8f, delayFeedback = 0.4f, delayTone = 0.5f;
    int delayDiv = 9;                // 3/16
    float reverbReturn = 0.8f, reverbDecay = 0.55f, reverbDamp = 0.45f;
    float glue = 0.3f, limitDb = -0.3f, volume = 0.8f;
    bool lookAhead = true;   // 1.5 ms: the limiter ramps down before a peak instead of clamping it
};

constexpr int kMasterFxSlots = 8, kPerformFx = 8;

struct Project
{
    juce::String name { "Untitled" };
    double bpm = 120.0;
    int beats = 4, unit = 4;          // time signature
    float swing = 0.5f;              // 0.5–0.75
    /** Swing as a time warp on the step grid (x in steps): in each pair of steps the first stretches by s and the
        second shrinks by it, so a note on the second step plays s steps late (as the steps do, s = (swing − 0.5) × 2),
        one between moves in proportion and the pair's edges stay. Used for clip notes and drum rolls, which are
        anywhere, not only on steps. */
    double swungSteps (double x) const
    {
        const double s = (swing - 0.5) * 2.0;
        if (s <= 0.0) return x;
        const double k = std::floor (x / 2.0) * 2.0, f = x - k;
        return k + (f < 1.0 ? f * (1.0 + s) : 1.0 + s + (f - 1.0) * (1.0 - s));
    }
    bool countIn = true;             // a bar of clicks before recording starts
    bool metronome = false;
    float clickLevel = 0.5f;
    int switchQuantize = 0;          // 0 = Bar, 1 = Pattern end, 2 = Immediate
    bool perform = false;            // performance mode: a picked pattern starts when the playing one ends
    float recordQuantize = 1.0f;     // 0–1 strength
    bool overdub = false;             // live recording: off = Replace (and a take longer than the pattern grows it); on = layer
    int recordMode = 0;              // 0 Live (real time), 1 Step (notes and chords written at the step cursor)
    int stepLength = 0;              // step input length: 0 = 1 step, 1 = 2, 2 = 4, 3 = 8
    bool channelPerTrack = false;    // MIDI channels 1–8 play tracks 1–8 (otherwise every channel plays the selected track)
    ScaleSettings scale;
    static int stepLengthSteps (int index) { return 1 << std::clamp (index, 0, 3); }
    MasterSettings master;
    std::array<FxSlot, kMasterFxSlots> masterFx;   // in series, left to right (standalone)
    std::array<FxSlot, kPerformFx> performFx;      // the 8 hold-to-play pads (filled with their effects by MasterFx)
    std::array<Pattern, kPatterns> patterns;

    // ---- timing helpers
    double barQuarters() const { return beats * 4.0 / unit; }
    static double resQuarters (int stepRes) { return stepRes == 0 ? 0.5 : (stepRes == 2 ? 0.125 : 0.25); }
    int stepsPerBar (int pattern) const
    {
        const auto& p = patterns[(size_t) pattern];
        return std::max (1, (int) std::lround (barQuarters() / resQuarters (p.stepRes)));
    }
    int patternSteps (int pattern) const
    {
        return std::min (kMaxSteps, stepsPerBar (pattern) * patterns[(size_t) pattern].lengthBars);
    }
    double stepQuarters (int pattern) const { return barQuarters() / stepsPerBar (pattern); }
    /** A step and the pattern in clip ticks (kPPQ a quarter). */
    int stepTicks (int pattern) const { return (int) std::lround (stepQuarters (pattern) * kPPQ); }
    int patternTicks (int pattern) const { return patternSteps (pattern) * stepTicks (pattern); }
    int maxBarsFor (int pattern) const { return std::max (1, std::min (kMaxBars, kMaxSteps / stepsPerBar (pattern))); }
    /** Change a pattern's length; growing it repeats the existing bars into the new, empty ones (1 → 2 bars
        copies bar 1, 2 → 4 copies bars 1–2), unless `copy` is false. Shrinking keeps the hidden bars. */
    void setLengthBars (int pattern, int bars, bool copy = true, int skipTrack = -1)
    {
        auto& pt = patterns[(size_t) pattern];
        bars = std::clamp (bars, 1, maxBarsFor (pattern));
        const int oldBars = pt.lengthBars, spb = stepsPerBar (pattern);
        pt.lengthBars = bars;
        if (! copy || bars <= oldBars) return;
        const int oldSteps = oldBars * spb, newSteps = std::min (kMaxSteps, bars * spb);
        const int barT = spb * stepTicks (pattern);
        for (int si = 0; si < kTracks; ++si)
        {
            if (si == skipTrack) continue;   // the track being recorded: its new bar stays empty
            auto& tr = pt.slots[(size_t) si].track;
            if (tr.pianoRoll)
            {
                // a clip: the notes of the old length repeat into each new, empty bar
                const auto old = tr.clip;
                for (int b = oldBars; b < bars; ++b)
                {
                    bool empty = true;
                    for (auto& n : old) empty = empty && ! (n.start >= b * barT && n.start < (b + 1) * barT);
                    if (! empty) continue;
                    const int src = b % oldBars;
                    for (auto n : old)
                        if (n.start >= src * barT && n.start < (src + 1) * barT) { n.start += (b - src) * barT; tr.addClipNote (n); }
                }
            }
            for (int b = oldBars; b < bars; ++b)
            {
                // only empty bars are filled: notes hidden by an earlier shrink come back as they were
                bool empty = true;
                for (int s = b * spb; s < std::min (newSteps, (b + 1) * spb) && empty; ++s)
                {
                    empty = tr.steps[(size_t) s].noteCount == 0;
                    for (auto& lane : tr.lanes) empty = empty && ! lane[(size_t) s].on;
                }
                if (! empty) continue;
                for (int s = b * spb; s < std::min (newSteps, (b + 1) * spb); ++s)
                {
                    tr.steps[(size_t) s] = tr.steps[(size_t) (s % oldSteps)];
                    for (auto& lane : tr.lanes) lane[(size_t) s] = lane[(size_t) (s % oldSteps)];
                    for (auto& l : tr.locks) l.values[(size_t) s] = l.values[(size_t) (s % oldSteps)];
                }
            }
        }
    }
};
} // namespace mb
