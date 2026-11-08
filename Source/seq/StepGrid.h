#pragma once
#include "model/Project.h"
#include "seq/Scales.h"
#include <vector>

/** The synth step grid (the drum sequencer's shape for melodic tracks; docs/sequencer.md): steps against the scale's
    notes, with an octave per step (an octave sequencer: 0-8, counted from the scale's root), a length in steps, accent
    and glide. Shared by the desktop's Steps view and the phone's Seq so both edit the same way. */
namespace mb::stepgrid
{
constexpr int kOctaves = 9;   // 0 … 8

struct Scale
{
    int root = 0;
    std::vector<int> iv { 0, 2, 4, 5, 7, 9, 11 };
    Scale() = default;
    explicit Scale (const ScaleSettings& s)
    {
        const auto& f = Scales::factoryScales();
        root = ((s.root % 12) + 12) % 12;
        iv = f[(size_t) std::clamp (s.scale, 0, (int) f.size() - 1)].intervals;
        if (iv.empty()) iv = { 0 };
    }
    int degrees() const { return (int) iv.size(); }
};

/** A note's place: its scale degree (an off-scale note: the degree below it, `exact` false) and octave. */
struct Place { int degree = 0, octave = 0; bool exact = true; };

inline Place placeOf (int note, const Scale& sc)
{
    const int rel = note - sc.root;
    const int oct = (rel >= 0 ? rel / 12 : (rel - 11) / 12) - 1;   // octave n: the root at 12 (n + 1) + root
    const int pc = rel - 12 * (oct + 1);                           // 0 … 11 above the root
    Place p;
    p.octave = oct;
    p.degree = 0;
    for (int d = 0; d < sc.degrees(); ++d) if (sc.iv[(size_t) d] <= pc) p.degree = d;
    p.exact = sc.iv[(size_t) p.degree] == pc;
    return p;
}

inline int noteAt (int degree, int octave, const Scale& sc)
{
    degree = std::clamp (degree, 0, sc.degrees() - 1);
    return std::clamp (12 * (octave + 1) + sc.root + sc.iv[(size_t) degree], 0, 127);
}

/** The step's octave: its lowest note's (−1: no note). */
inline int octaveOf (const Step& st, const Scale& sc)
{
    if (st.noteCount == 0) return -1;
    int lo = 127;
    for (int i = 0; i < st.noteCount; ++i) lo = std::min (lo, (int) st.notes[i]);
    return placeOf (lo, sc).octave;
}

/** Whether the step has a note on this degree (any octave in the step's chord). */
inline int noteOnDegree (const Step& st, int degree, const Scale& sc)
{
    for (int i = 0; i < st.noteCount; ++i) if (placeOf (st.notes[i], sc).degree == degree) return st.notes[i];
    return -1;
}

/** A tap on a cell: the note goes if it is there; else it is added at the step's octave (an empty step: `octave`).
    A monophonic instrument (`mono`) replaces the step's note instead of building a chord. Returns true when on. */
inline bool toggle (Step& st, int degree, int octave, const Scale& sc, bool mono, int velocity = 100)
{
    if (const int n = noteOnDegree (st, degree, sc); n >= 0)
    {
        st.removeNote (n);
        return false;
    }
    const int oct = st.noteCount > 0 ? octaveOf (st, sc) : octave;
    const int note = noteAt (degree, oct, sc);
    if (st.noteCount == 0 || mono)
    {
        // a new or replaced note keeps the step's settings (velocity, length, flags, chance …)
        const bool had = st.noteCount > 0;
        const auto vel = st.vel[0]; const auto len = st.len[0];
        while (st.noteCount > 0) st.removeNote (st.notes[0]);
        st.addNote (note, had ? vel : velocity, had ? len : 96);
    }
    else st.addNote (note);   // a chord: the new note takes the step's velocity and length
    return true;
}

/** The step's notes into an octave (a chord moves together). */
inline void setOctave (Step& st, int octave, const Scale& sc)
{
    const int cur = octaveOf (st, sc);
    if (cur < 0) return;
    const int by = 12 * (std::clamp (octave, 0, kOctaves - 1) - cur);
    for (int i = 0; i < st.noteCount; ++i) st.notes[i] = (uint8_t) std::clamp (st.notes[i] + by, 0, 127);
}

/** Length in steps: 1 is the usual gate (¾ of the step); longer notes ring on to ¾ of their last step. */
inline int lengthSteps (const Step& st) { return st.noteCount ? std::max (1, (st.maxLen() + 127) / 128) : 0; }
inline void setLengthSteps (Step& st, int steps) { st.setLength (std::clamp (steps, 1, 64) * 128 - 32); }

/** Every step an octave down / up (each one stays inside 0-8). */
inline void shiftAll (Track& tr, int total, int dir, const Scale& sc)
{
    for (int s = 0; s < total; ++s)
        if (auto& st = tr.steps[(size_t) s]; st.noteCount > 0) setOctave (st, octaveOf (st, sc) + dir, sc);
}
} // namespace mb::stepgrid
