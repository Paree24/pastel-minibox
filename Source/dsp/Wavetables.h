#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

/** The Wavetable device's built-in tables (docs/wavetable.md): 64 tables of 16 frames, each frame stored band-limited
    at nine resolutions (512 harmonics down to 2), so a note reads the most harmonics that stay under Nyquist. Built
    once, on first use (message thread), then read-only: the audio thread only reads. */
namespace mb::dsp::wt
{
constexpr int kTables = 64, kFrames = 16, kLevels = 9, kTopHarm = 512;

struct TableInfo { const char* name; const char* family; };

class Bank
{
public:
    static const Bank& get();
    int count() const { return kTables; }
    static const TableInfo& info (int table);
    static juce::StringArray names();
    static juce::StringArray families();   // per table, in order

    /** The level for a phase increment (cycles per sample): the most harmonics under ~0.45 of the sample rate. */
    static int levelFor (float inc)
    {
        int l = 0;
        while (l < kLevels - 1 && (float) (kTopHarm >> l) * inc > 0.45f) ++l;
        return l;
    }
    static int size (int level) { return std::max (128, 4 * (kTopHarm >> level)); }   // ≥ 4 points a cycle of the top harmonic

    /** One frame at one level: size (level) samples plus three guard samples (the first ones again; one before
        index 0 is the last sample). */
    const float* frame (int table, int level, int frame) const
    {
        return data.data() + (size_t) table * perTable + (size_t) frame * perFrame + levelOffset[(size_t) level];
    }
    /** Cubic (Hermite) read at phase 0…1 (wrapped by the caller): the interpolation's images stay far down. */
    static float read (const float* f, int size, float ph)
    {
        const float x = ph * (float) size;
        const int i = std::min ((int) x, size - 1);
        const float t = x - (float) i;
        const float ym1 = f[i == 0 ? size - 1 : i - 1], y0 = f[i], y1 = f[i + 1], y2 = f[i + 2];
        const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }
    /** The value at a position 0…1 through the table (frames crossfaded) and a phase. */
    float sample (int table, int level, float pos, float ph) const
    {
        const float fp = std::clamp (pos, 0.0f, 1.0f) * (float) (kFrames - 1);
        const int f0 = std::min ((int) fp, kFrames - 2);
        const float t = fp - (float) f0;
        const int n = size (level);
        const float a = read (frame (table, level, f0), n, ph);
        if (t <= 0.0f) return a;
        return a + (read (frame (table, level, f0 + 1), n, ph) - a) * t;
    }

private:
    Bank();
    std::vector<float> data;
    size_t perFrame = 0, perTable = 0;
    std::array<size_t, kLevels> levelOffset {};
};
} // namespace mb::dsp::wt
