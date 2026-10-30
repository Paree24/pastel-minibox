#pragma once
#include <string>
#include <vector>
#include <array>
#include <initializer_list>

// Scale library, snapping and chord helpers: ported from the user's Midi Note Modifier
// (~/ai_experiments/midi-note-modifier/Source/Scales.h) at the user's request, for the groovebox's
// scale lock / chord mode / arp. The "Custom" placeholder entry is dropped (no user scales here).
namespace mb::Scales
{
    struct ScaleDef
    {
        std::string name;
        std::vector<int> intervals; // semitones from root, always includes 0
        bool powerChords = false;   // from [chord_types]: default chord is power (1-5)
    };

    inline const std::vector<ScaleDef>& factoryScales()
    {
        static const std::vector<ScaleDef> s = {
            { "Chromatic", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, true },
            { "Major", { 0, 2, 4, 5, 7, 9, 11 } },
            { "Dorian", { 0, 2, 3, 5, 7, 9, 10 } },
            { "Phrygian", { 0, 1, 3, 5, 7, 8, 10 } },
            { "Lydian", { 0, 2, 4, 6, 7, 9, 11 } },
            { "Mixolydian", { 0, 2, 4, 5, 7, 9, 10 } },
            { "Natural Minor", { 0, 2, 3, 5, 7, 8, 10 } },
            { "Locrian", { 0, 1, 3, 5, 6, 8, 10 } },
            { "Harmonic Minor", { 0, 2, 3, 5, 7, 8, 11 } },
            { "Melodic Minor", { 0, 2, 3, 5, 7, 9, 11 } },
            { "Harmonic Major", { 0, 2, 4, 5, 7, 8, 11 } },
            { "Phrygian Dominant", { 0, 1, 4, 5, 7, 8, 10 } },
            { "Mixolydian b6", { 0, 2, 4, 5, 7, 8, 10 } },
            { "Major Pentatonic", { 0, 2, 4, 7, 9 }, true },
            { "Minor Pentatonic", { 0, 3, 5, 7, 10 }, true },
            { "Major Blues", { 0, 2, 3, 4, 7, 9 }, true },
            { "Minor Blues", { 0, 3, 5, 6, 7, 10 }, true },
            { "Double Harmonic Major", { 0, 1, 4, 5, 7, 8, 11 } },
            { "Neapolitan Minor", { 0, 1, 3, 5, 7, 8, 11 } },
            { "Neapolitan Major", { 0, 1, 4, 5, 7, 9, 11 } },
            { "Hungarian Minor", { 0, 2, 3, 6, 7, 8, 11 } },
            { "Hungarian Major", { 0, 3, 4, 6, 7, 9, 10 } },
            { "Persian", { 0, 1, 4, 5, 6, 8, 11 } },
            { "Ukrainian Dorian", { 0, 2, 3, 6, 7, 9, 10 } },
            { "Hirajoshi", { 0, 2, 3, 7, 8 }, true },
            { "In Sen", { 0, 1, 5, 7, 10 }, true },
            { "Iwato", { 0, 1, 5, 6, 10 }, true },
            { "Kumoi", { 0, 2, 3, 7, 9 }, true },
            { "Yo", { 0, 2, 5, 7, 9 }, true },
            { "Egyptian", { 0, 2, 5, 7, 10 }, true },
            { "Japanese Ritsu", { 0, 2, 5, 7, 9 }, true },
            { "Japanese Akebono", { 0, 2, 3, 7, 8 }, true },
            { "Japanese Sakura", { 0, 1, 5, 7, 8 }, true },
            { "Japanese Miyako-bushi", { 0, 1, 3, 7, 8 }, true },
            { "Bhairav", { 0, 1, 4, 5, 7, 8, 11 } },
            { "Todi", { 0, 1, 3, 6, 7, 8, 11 } },
            { "Marwa", { 0, 1, 4, 6, 7, 9, 11 } },
            { "Kafi", { 0, 2, 3, 5, 7, 9, 10 } },
            { "Hijaz", { 0, 1, 4, 5, 7, 8, 10 } },
            { "Bayati", { 0, 1, 3, 5, 7, 8, 10 } },
            { "Nahawand", { 0, 2, 3, 5, 7, 8, 11 } },
            { "Chinese Shang", { 0, 2, 3, 5, 7, 9, 10 } },
            { "Chinese Yu", { 0, 3, 5, 7, 10 }, true },
            { "Pelog", { 0, 1, 3, 7, 8 }, true },
            { "Slendro", { 0, 2, 5, 7, 10 }, true },
            { "Byzantine", { 0, 1, 4, 5, 7, 8, 11 } },
            { "Prometheus", { 0, 2, 4, 6, 9, 10 } },
            { "Romanian Minor", { 0, 2, 3, 6, 7, 9, 10 } },
            { "Bebop", { 0, 2, 4, 5, 7, 9, 10, 11 } },
            { "Diminished", { 0, 2, 3, 5, 6, 8, 9, 11 } },
            { "Lydian Dominant", { 0, 2, 4, 6, 7, 9, 10 } },
            { "Altered", { 0, 1, 3, 4, 6, 8, 10 } },
            { "Dorian b2", { 0, 1, 3, 5, 7, 9, 10 } },
            { "Ultralocrian", { 0, 1, 3, 4, 6, 8, 9 } },
            { "Augmented Heptatonic", { 0, 3, 4, 5, 7, 8, 11 } },
            { "Whole Tone", { 0, 2, 4, 6, 8, 10 } },
            { "Locrian Major", { 0, 2, 4, 5, 6, 8, 10 } },
            { "Double Harmonic Lydian", { 0, 1, 4, 6, 7, 8, 11 } },
            { "Enigmatic", { 0, 1, 4, 6, 8, 10, 11 } },
            { "Major Augmented", { 0, 2, 4, 5, 8, 9, 11 } },
            { "Messiaen #4", { 0, 1, 2, 5, 6, 7, 8, 11 } },
            { "Composite Blues", { 0, 2, 3, 4, 5, 6, 7, 9, 10 } },
            { "Lydian Augmented", { 0, 2, 4, 6, 8, 9, 11 } },
        };
        return s;
    }

    inline std::array<std::string, 12> noteNames()
    {
        return { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    }

    inline std::string pcName(int pc)
    {
        auto n = noteNames();
        return n[((pc % 12) + 12) % 12];
    }

    // Build pitch-class set for (root, intervals). Returns bool[12] in-scale.
    inline std::array<bool, 12> pcSet(int root, const std::vector<int>& intervals)
    {
        std::array<bool, 12> s{}; s.fill(false);
        for (int iv : intervals)
            s[((root + iv) % 12 + 12) % 12] = true;
        return s;
    }

    // Snap a MIDI note into the scale. Returns snapped MIDI note.
    // mode: 0 = nearest (ties prefer down), 1 = down, 2 = up.
    inline int snapNote(int midiNote, int root, const std::vector<int>& intervals, int mode)
    {
        if (intervals.empty()) return midiNote;
        auto set = pcSet(root, intervals);
        int pc = ((midiNote % 12) + 12) % 12;
        if (set[(size_t) pc]) return midiNote;

        auto inScale = [&](int m) {
            int p = ((m % 12) + 12) % 12;
            return set[(size_t) p];
        };

        if (mode == 1) // down (falls back upward at the bottom of the range)
        {
            for (int d = 1; d <= 12; ++d)
                if (midiNote - d >= 0 && inScale(midiNote - d)) return midiNote - d;
            for (int d = 1; d <= 12; ++d)
                if (midiNote + d <= 127 && inScale(midiNote + d)) return midiNote + d;
            return midiNote;
        }
        if (mode == 2) // up (falls back downward at the top of the range)
        {
            for (int d = 1; d <= 12; ++d)
                if (midiNote + d <= 127 && inScale(midiNote + d)) return midiNote + d;
            for (int d = 1; d <= 12; ++d)
                if (midiNote - d >= 0 && inScale(midiNote - d)) return midiNote - d;
            return midiNote;
        }
        // nearest, ties prefer down
        for (int d = 1; d <= 12; ++d)
        {
            bool dn = midiNote - d >= 0 && inScale(midiNote - d);
            bool up = midiNote + d <= 127 && inScale(midiNote + d);
            if (dn && up) return midiNote - d; // tie -> down
            if (dn) return midiNote - d;
            if (up) return midiNote + d;
        }
        return midiNote;
    }

    // Scale degree of a (snapped) note: index into intervals, or -1.
    inline int scaleDegree(int midiNote, int root, const std::vector<int>& intervals)
    {
        int pc = ((midiNote % 12) + 12) % 12;
        for (size_t i = 0; i < intervals.size(); ++i)
            if (((root + intervals[i]) % 12 + 12) % 12 == pc) return (int) i;
        return -1;
    }

    // ---- White-keys mode: white keys play consecutive scale degrees ----
    inline bool isWhitePc(int pc)
    {
        pc = ((pc % 12) + 12) % 12;
        return pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
    }

    // position of a white pitch class within its octave: C=0 .. B=6, else -1
    inline int whitePosInOctave(int pc)
    {
        pc = ((pc % 12) + 12) % 12;
        switch (pc)
        {
            case 0: return 0; case 2: return 1; case 4: return 2; case 5: return 3;
            case 7: return 4; case 9: return 5; case 11: return 6;
            default: return -1;
        }
    }

    // nearest white-key MIDI note at or around m (ties prefer down)
    inline int nearestWhiteKey(int m)
    {
        for (int d = 0; d <= 12; ++d)
        {
            if (m - d >= 0 && isWhitePc((m - d) % 12)) return m - d;
            if (m + d <= 127 && d > 0 && isWhitePc((m + d) % 12)) return m + d;
        }
        return m;
    }

    // Map any input note to consecutive scale degrees: the white keys of each
    // octave play degrees 0..6 of (root, intervals), wrapping across octaves.
    // E.g. C D E F G A B -> C D# F F# G A# C for C minor blues.
    inline int degreeMapNote(int midiNote, int root, const std::vector<int>& intervals)
    {
        if (intervals.empty()) return midiNote;
        int w = nearestWhiteKey(midiNote);
        int pos = whitePosInOctave(w % 12);
        if (pos < 0) pos = 0;
        int len = (int) intervals.size();
        // count white keys from middle C, so consecutive white keys are
        // consecutive degrees for ANY scale length: a 5-note scale no longer
        // repeats on A/B, an 8-note one no longer loses its 8th degree.
        // (For 7-note scales this is the same octave-anchored map as before.)
        int d = (w / 12 - 5) * 7 + pos;
        int oct = d >= 0 ? d / len : -((-d + len - 1) / len);
        int idx = d - oct * len;
        int n = 60 + root + intervals[(size_t) idx] + 12 * oct;
        return n < 0 ? 0 : (n > 127 ? 127 : n);
    }

    // Human-readable semitone names for the custom chord designer (0-24).
    inline std::string intervalName(int semitones)
    {
        static const char* names[25] = {
            "Root", "Minor 2nd", "Major 2nd", "Minor 3rd", "Major 3rd",
            "Perfect 4th", "Tritone", "Perfect 5th", "Minor 6th", "Major 6th",
            "Minor 7th", "Major 7th", "Octave", "Minor 9th", "Major 9th",
            "Minor 10th", "Major 10th", "Perfect 11th", "Augmented 11th", "Perfect 12th",
            "Minor 13th", "Major 13th", "Minor 14th", "Major 14th", "2 Octaves"
        };
        if (semitones < 0 || semitones > 24) return "?";
        return names[semitones];
    }

    // ---- Chords ----
    // ChordType ids used in per-degree selectors:
    // -1 = Auto (diatonic, or power for powerChords scales), 0 = Single/Off,
    // 1 Major, 2 Minor, 3 Dim, 4 Aug, 5 Sus2, 6 Sus4, 7 Power(1-5),
    // 8 Dom7(1-3-5-b7), 9 Maj7, 10 Min7, 11 Power+Oct(1-5-8), 12 Add9
    // Allocation-free form for the audio thread: writes up to 4 intervals, returns the count.
    inline int forcedChordIntervals(int type, int* out)
    {
        auto put = [out](std::initializer_list<int> l) { int n = 0; for (int v : l) out[n++] = v; return n; };
        switch (type)
        {
            case 1:  return put({ 0, 4, 7 });
            case 2:  return put({ 0, 3, 7 });
            case 3:  return put({ 0, 3, 6 });
            case 4:  return put({ 0, 4, 8 });
            case 5:  return put({ 0, 2, 7 });
            case 6:  return put({ 0, 5, 7 });
            case 7:  return put({ 0, 7 });
            case 8:  return put({ 0, 4, 7, 10 });
            case 9:  return put({ 0, 4, 7, 11 });
            case 10: return put({ 0, 3, 7, 10 });
            case 11: return put({ 0, 7, 12 });
            case 12: return put({ 0, 4, 7, 14 });
            default: return put({ 0 });
        }
    }

    inline std::vector<int> forcedChordIntervals(int type)
    {
        int b[4];
        int n = forcedChordIntervals(type, b);
        return { b, b + n };
    }

    inline std::string chordTypeName(int type)
    {
        switch (type)
        {
            case -1: return "Auto";
            case 0:  return "Single";
            case 1:  return "Major";
            case 2:  return "Minor";
            case 3:  return "Dim";
            case 4:  return "Aug";
            case 5:  return "Sus2";
            case 6:  return "Sus4";
            case 7:  return "Power";
            case 8:  return "7";
            case 9:  return "Maj7";
            case 10: return "Min7";
            case 11: return "5+Oct";
            case 12: return "Add9";
            default: return "?";
        }
    }

    // Diatonic triad by stacking thirds within the scale.
    // Allocation-free form: writes 1 or 3 intervals, returns the count.
    inline int diatonicTriad(int degree, const std::vector<int>& intervals, int* out)
    {
        if (intervals.empty()) { out[0] = 0; return 1; }
        int n = (int) intervals.size();
        auto height = [&](int degOffset) {
            int idx = (degree + degOffset) % n;
            int oct = (degree + degOffset) / n;
            return intervals[(size_t) idx] + 12 * oct;
        };
        int r = height(0);
        out[0] = 0; out[1] = height(2) - r; out[2] = height(4) - r;
        return 3;
    }

    inline std::vector<int> diatonicTriad(int degree, const std::vector<int>& intervals)
    {
        int b[3];
        int n = diatonicTriad(degree, intervals, b);
        return { b, b + n };
    }
}
