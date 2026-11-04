#pragma once
#include "model/Project.h"
#include <vector>

/** Chords on a step, shared by the desktop's steps view and the phone's step sheet: the shapes (semitones up from
    the lowest note), a step's shape name, and setting a step to a shape. Note names as both apps show them. */
namespace mb::chords
{
struct Type { const char* name; std::vector<int> iv; };

inline const std::vector<Type>& types()
{
    static const std::vector<Type> t {
        { "Single", { 0 } }, { "Maj", { 0, 4, 7 } }, { "Min", { 0, 3, 7 } }, { "Dim", { 0, 3, 6 } },
        { "Aug", { 0, 4, 8 } }, { "Sus2", { 0, 2, 7 } }, { "Sus4", { 0, 5, 7 } },
        { "Maj7", { 0, 4, 7, 11 } }, { "7", { 0, 4, 7, 10 } }, { "m7", { 0, 3, 7, 10 } }, { "mMaj7", { 0, 3, 7, 11 } },
        { "dim7", { 0, 3, 6, 9 } }, { "m7\xe2\x99\xad" "5", { 0, 3, 6, 10 } }, { "Aug7", { 0, 4, 8, 10 } }, { "AugMaj7", { 0, 4, 8, 11 } },
        { "7sus2", { 0, 2, 7, 10 } }, { "7sus4", { 0, 5, 7, 10 } },
        { "Power", { 0, 7 } }, { "Octave", { 0, 12 } },
    };
    return t;
}

/** "C4", "Eb3", "Bb2": sharps for C# and F#, flats for the others (C4 = 60). */
inline juce::String noteName (int n)
{
    static const char* names[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    n = std::clamp (n, 0, 127);
    return juce::String (names[n % 12]) + juce::String (n / 12 - 1);
}

inline int lowest (const Step& st)
{
    int r = 127;
    for (int i = 0; i < st.noteCount; ++i) r = std::min (r, (int) st.notes[i]);
    return r;
}

/** The step's shape: its type's index, or -1 (not one of them). */
inline int typeOf (const Step& st)
{
    if (st.noteCount == 0) return -1;
    const int root = lowest (st);
    std::vector<int> shape;
    for (int i = 0; i < st.noteCount; ++i) shape.push_back (st.notes[i] - root);
    std::sort (shape.begin(), shape.end());
    const auto& t = types();
    for (size_t k = 0; k < t.size(); ++k) if (t[k].iv == shape) return (int) k;
    return -1;
}

/** "Min7", "Single", or "4 notes". */
inline juce::String nameOf (const Step& st)
{
    const int k = typeOf (st);
    return k >= 0 ? juce::String::fromUTF8 (types()[(size_t) k].name) : juce::String (st.noteCount) + " notes";
}

/** The step becomes a shape on its lowest note; its velocity, length, offset, flags, chance, ratchet, condition and
    micro-timing stay. */
inline void set (Step& st, const std::vector<int>& iv)
{
    if (st.noteCount == 0 || iv.empty()) return;
    const int root = lowest (st);
    const auto vel = st.vel[0]; const auto len = st.len[0]; const auto off = st.off[0];
    const auto flags = st.flags, prob = st.probability, rat = st.ratchet, cond = st.cond;
    const auto micro = st.microTiming;
    st.clear();
    for (int x : iv) st.addNote (std::clamp (root + x, 0, 127), vel, len);
    for (int i = 0; i < st.noteCount; ++i) st.off[i] = off;
    st.flags = flags; st.probability = prob; st.ratchet = rat; st.cond = cond; st.microTiming = micro;
}
} // namespace mb::chords
