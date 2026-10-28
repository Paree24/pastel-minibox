#pragma once
#include "dsp/Effects.h"
#include "DeviceProcessor.h"

namespace mb
{
/** Chorus → synced delay used inside Mono and FM (the reverb is the DeviceReverb card). Processes a stereo
    buffer in place. */
struct DeviceFx
{
    dsp::BbdChorus chorus;
    dsp::TapeDelay delay;
    float sr = 48000.0f;

    void prepare (float rate) { sr = rate; chorus.prepare (rate); delay.prepare (rate); delay.setTone (0.55f); }
    bool chorusWasOn = false, delayWasOn = false;   // switched off, the lines stop: they are cleared when back on
    void reset() { chorus.reset(); delay.reset(); }

    void process (float* L, float* R, int n, int chorusMode, float chorusMix, float delayMix, double delayBeats,
                  float feedback, const TransportInfo& t, float chorusRate = 1.0f, float chorusDepth = 1.0f)
    {
        const float secs = (float) (delayBeats * 60.0 / std::max (20.0, t.bpm));
        // off, an effect stops writing its lines; back on, it starts from silence (not from what was frozen there)
        const bool chorusOn = chorusMix > 0.001f && chorusMode > 0, delayOn = delayMix > 0.001f;
        if (chorusOn && ! chorusWasOn) chorus.reset();
        if (delayOn && ! delayWasOn) delay.reset();
        chorusWasOn = chorusOn; delayWasOn = delayOn;
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            if (chorusOn) chorus.process (l, r, chorusMode, chorusMix, chorusRate, chorusDepth);
            float ol = l, orr = r;
            if (delayOn) delay.process (l, r, ol, orr, secs, feedback, delayMix);
            L[i] = ol; R[i] = orr;
        }
    }
};

/** The Reverb card every device carries: its own plate with Mix, Size (decay and room), Tone (bright …
    dark) and Pre-delay. The wet signal is added to the buffers; the mix is smoothed and the plate keeps
    running while its tail rings out after Mix goes to zero. */
struct DeviceReverb
{
    dsp::PlateReverb plate;
    dsp::Smoother mixSm;
    int tail = 0, tailLen = 1;
    void prepare (float rate) { plate.prepare (rate); mixSm.setTime (20.0f, rate); mixSm.reset (0.0f); tailLen = (int) (8.0f * rate); }
    void reset() { plate.reset(); mixSm.reset (0.0f); tail = 0; }
    /** Adds the reverb of (inL, inR) into (L, R); in == out is fine. */
    void process (const float* inL, const float* inR, float* L, float* R, int n, float mix, float size, float tone, float preSec)
    {
        if (mix <= 0.0005f && tail <= 0) { mixSm.reset (0.0f); return; }
        if (mix > 0.0005f && tail <= 0) plate.reset();   // waking up: from silence, not what was frozen in the lines
        tail = mix > 0.0005f ? tailLen : tail - n;
        plate.set (size, 1.0f - tone, size, preSec);
        for (int i = 0; i < n; ++i)
        {
            // Mix is the send into the plate (the return stays at unity), so the tail rings on after Mix goes to 0
            float l = 0.0f, r = 0.0f;
            const float send = mixSm.tick (mix);
            plate.process (inL[i] * send, inR[i] * send, l, r, 1.0f);
            L[i] += l; R[i] += r;
        }
    }
};

/** Adds the Reverb card's Size / Tone / Pre-delay parameters (each device keeps its own Mix id). */
inline void addReverbParams (ParamLayout& L)
{
    L.cont ("revSize", "Size", 0, 1, 0.45f, Unit::Percent);
    L.cont ("revTone", "Tone", 0, 1, 0.55f, Unit::Percent);
    L.cont ("revPre", "Pre-delay", 0, 0.1f, 0.012f, Unit::Seconds);
}

/** Chord memory shapes shared by Mono and Poly (semitones above the played note). */
inline const juce::StringArray& chordShapeNames()
{
    static const juce::StringArray s { "Major", "Minor", "Sus4", "Fifths", "Min7", "Maj7", "Min9", "Stack" };
    return s;
}
inline std::array<int, 4> chordShape (int idx, int& count)
{
    static const int shapes[8][4] = { { 0, 4, 7, 12 }, { 0, 3, 7, 12 }, { 0, 5, 7, 12 }, { 0, 7, 12, 19 },
                                      { 0, 3, 7, 10 }, { 0, 4, 7, 11 }, { 0, 3, 10, 14 }, { 0, 5, 10, 15 } };
    idx = std::clamp (idx, 0, 7);
    count = 4;
    return { shapes[idx][0], shapes[idx][1], shapes[idx][2], shapes[idx][3] };
}
} // namespace mb
