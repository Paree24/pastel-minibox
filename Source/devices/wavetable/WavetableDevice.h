#pragma once
#include "devices/DeviceProcessor.h"
#include "dsp/Oscillators.h"
#include "dsp/Envelopes.h"
#include "dsp/Analog.h"
#include "dsp/Wavetables.h"
#include "dsp/Effects.h"

namespace mb
{
/** Wavetable (docs/wavetable.md): a synth for bass music. Two wavetable oscillators (B synced, frequency-modulated or
    ring-modulated by A), a sine or triangle sub, noise, a multimode filter with drive, amp and filter envelopes, two
    LFOs, an eight-slot modulation matrix, unison with a mono low end, and a distortion stage (drive, fold, crush).
    Macros: Wobble, Movement, Dirt, Bite, Width (and Sub, Noise, Glide). */
class WavetableDevice : public DeviceProcessor
{
public:
    WavetableDevice();
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void handleEvent (const NoteEvent&) override;
    void render (float* L, float* R, int n) override;
    bool anyVoiceActive() const override;

    enum P { oscATable, oscAPos, oscAOct, oscASemi, oscAFine, oscAPhase, oscALevel,
             oscBTable, oscBPos, oscBOct, oscBSemi, oscBFine, oscBPhase, oscBLevel, oscBMode, oscBMod,
             subShape, subLevel, subOct, noiseType, noiseLevel, noiseTone, noiseDecay,
             filterMode, cutoff, reso, filterDrive, filterEnv, filterKey,
             ampA, ampD, ampS, ampR, fltA, fltD, fltS, fltR,
             lfo1Shape, lfo1Sync, lfo1Rate, lfo1Div, lfo1Depth, lfo1Fade, lfo1Dest,
             lfo2Shape, lfo2Sync, lfo2Rate, lfo2Div, lfo2Depth, lfo2Fade, lfo2Dest,
             distType, distDrive, distFold, distCrush, distTone, distMix,
             mod1Src, mod1Dst, mod1Amt,   // … 8 slots of three
             voiceMode = mod1Src + 24, uniVoices, uniDetune, uniWidth, monoBass, uniPhase, glide, level, velocity,
             squash, chorus,   // 2026-11-13: after the distortion, three-band compression (density, top end) and a chorus
             numParams };
    static constexpr int kSlots = 8, kVoices = 6, kStacks = 16, kStackBudget = 32;
    static int slotParam (int slot, int field) { return mod1Src + slot * 3 + field; }   // field 0 Src, 1 Dst, 2 Amt

    enum Src { SrcOff, SrcLfo1, SrcLfo2, SrcAmpEnv, SrcFltEnv, SrcVel, SrcNote, SrcWheel, SrcPressure, SrcRandom, kNumSrc };
    enum Dst { DstOff, DstPitch, DstPosA, DstPosB, DstLevelA, DstLevelB, DstBMod, DstPitchB, DstSub, DstNoise, DstCutoff, DstReso,
               DstFilterDrive, DstDist, DstFold, DstCrush, DstDetune, DstWidth, DstPan, DstAmp, DstLfo1Rate, DstLfo2Rate, DstSquash, DstChorus, DstVolume, kNumDst };
    static const juce::StringArray& sourceNames();
    static const juce::StringArray& destNames();

    /** New settings at random, musical ranges kept (the sub stays solid, the level stays sane). scope: 0 everything,
        1 the oscillators, 2 filter and envelopes, 3 modulation, 4 distortion and unison. */
    void randomize (int scope, uint32_t seed);
    static juce::StringArray randomizeScopes() { return { "All", "Oscillators", "Filter & Env", "Modulation", "Dirt & Width" }; }

private:
    struct Svf   // a zero-delay state-variable filter (two-pole)
    {
        float ic1 = 0.0f, ic2 = 0.0f, g = 0.0f, k = 1.4f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        void set (float fc, float q, float sr)
        {
            g = std::tan (dsp::kPi * std::min (fc, sr * 0.45f) / sr); k = q;
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void tick (float x, float& lp, float& bp, float& hp)
        {
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
            lp = v2; bp = v1; hp = x - k * v1 - v2;
        }
        void reset() { ic1 = ic2 = 0.0f; }
    };
    struct Stack { float phA = 0.0f, phB = 0.0f, spread = 0.0f; };
    struct Voice
    {
        VoiceSlot slot;
        std::array<Stack, kStacks> st;
        int stacks = 1;
        float pitch = 60.0f, target = 60.0f, vel = 0.8f, rnd = 0.0f;
        float subPh = 0.0f, noiseEnv = 0.0f, noiseLp = 0.0f, fadeIn = 1.0f;
        dsp::Noise noise;
        dsp::Adsr amp, flt;
        std::array<Svf, 2> fL, fR;   // two stages per side (24 dB)
        float posA = 0.0f, posB = 0.0f;   // ramps across a control block
    };
    struct Lfo
    {
        float phase = 0.0f, held = 0.0f, prevHeld = 0.0f, fade = 1.0f, out = 0.0f;
        bool synced = false, wrapped = false;   // following the bar; took its new value on its own wrap
        dsp::Rng rng;
        float tick (int shape, float inc, float fadeInc);
        void retrigger() { phase = 0.0f; fade = 0.0f; }
    };
    void voiceOn (Voice&, int note, float vel, bool retrigger, bool glide);
    void noteOn (int note, float vel);
    void noteOff (int note);
    int stacksNow() const;
    int maxVoices() const;
    void renderBlock (float* L, float* R, int start, int len);
    float dist (float x, int type, float drive, float fold) const;

    std::array<Voice, kVoices> voices;
    std::array<Lfo, 2> lfos;
    HeldStack held;
    uint32_t clock = 0;
    int lastVoice = 0;
    float lastNote = 48.0f, exprSemis = 0.0f;
    dsp::Rng rng { 7171u };
    const dsp::wt::Bank* bank = nullptr;
    std::vector<float> bufL, bufR;
    dsp::Smoother levelSm;
    std::array<dsp::analog::Oversampler2x, 4> distOs;   // two 2x stages a side: the distortion runs at 4x
    float crushHoldL = 0.0f, crushHoldR = 0.0f, crushCount = 0.0f, toneL = 0.0f, toneR = 0.0f;
    std::array<Svf, 2> monoHp;   // Mono Bass: the side's high-pass
    /** Squash: three bands (crossovers at 150 Hz and 2.5 kHz, 24 dB), each pulled toward one level from below and
        above (upward and downward compression); the low band only lightly, so the sub stays steady. */
    struct Squash
    {
        std::array<std::array<Svf, 8>, 2> f;   // per side: low LP ×2, low HP ×2, mid LP ×2, high HP ×2
        std::array<float, 3> env {}, gain { 1.0f, 1.0f, 1.0f }, target { 1.0f, 1.0f, 1.0f };
        float rms = 0.0f;   // the whole sound's level (its peaks are rounded off against it)
        void set (float sr) { for (auto& side : f) { for (int k = 0; k < 4; ++k) side[(size_t) k].set (150.0f, 1.414f, sr);
                                                     for (int k = 4; k < 8; ++k) side[(size_t) k].set (2500.0f, 1.414f, sr); } }
        void reset() { for (auto& side : f) for (auto& x : side) x.reset(); env.fill (0.0f); gain.fill (1.0f); target.fill (1.0f); rms = 0.0f; }
        void split (int ch, float x, float& lo, float& mid, float& hi)
        {
            auto& s = f[(size_t) ch];
            float l, b, h;
            s[0].tick (x, l, b, h); s[1].tick (l, lo, b, h);
            s[2].tick (x, l, b, h); float rest; s[3].tick (h, l, b, rest);
            s[4].tick (rest, l, b, h); s[5].tick (l, mid, b, h);
            s[6].tick (rest, l, b, h); s[7].tick (h, l, b, hi);
        }
    } squasher;
    dsp::BbdChorus chorusFx;
    bool chorusWasOn = false;
};
} // namespace mb
