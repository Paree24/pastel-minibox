#pragma once
#include "devices/DeviceProcessor.h"
#include "devices/DeviceFx.h"
#include "dsp/Oscillators.h"
#include "dsp/Filters.h"
#include "dsp/Envelopes.h"
#include "dsp/Analog.h"

namespace mb
{
/** Minibox Mono (§8.7): one oscillator core with pulse, saw, sub and noise through a source mixer,
    a 24 dB ladder, one ADSR, an LFO, glide and Mono/Unison/Poly/Chord voice modes. */
class MonoDevice : public DeviceProcessor
{
public:
    MonoDevice();
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void handleEvent (const NoteEvent&) override;
    void render (float* L, float* R, int n) override;
    bool anyVoiceActive() const override;

    enum P { range, tune, pw, pwm, pwmSrc, mixPulse, mixSaw, mixSub, subType, mixNoise,
             cutoff, reso, envAmt, lfoCut, key, drive,
             attack, decay, sustain, release, ampMode,
             lfoShape, lfoRate, lfoFade, lfoPitch, lfoPwm,
             glideMode, glideTime, voiceMode, detune, chord,
             fxChorus, fxDelay, fxTime, fxFeedback, fxReverb, level, revSize, revTone, revPre, lfoSync, lfoDiv, numParams };

private:
    struct Voice
    {
        VoiceSlot slot;
        dsp::PolyBlepOsc osc;
        dsp::SubOsc sub;
        dsp::Noise noise;
        dsp::analog::OtaCascade filter;
        dsp::analog::Oversampler2x os;
        dsp::analog::Tolerance tol;
        dsp::Adsr env;
        float pitch = 60.0f, targetPitch = 60.0f, vel = 0.8f, detuneSemis = 0.0f, pan = 0.0f;
        float fadeIn = 1.0f, velGain = 0.0f;   // velGain glides: a retrigger at a new velocity never steps
    };
    void startVoice (Voice&, int note, float vel, bool legato, bool glide);
    void noteOn (int note, float vel);
    void noteOff (int note);
    int numActiveVoices() const;

    std::array<Voice, 4> voices;
    dsp::Lfo lfo;
    DeviceFx fx;
    DeviceReverb verb;
    HeldStack held;
    uint32_t clock = 0;
    dsp::Rng rng { 4242u };
    std::vector<float> bufL, bufR, lfoBuf;
    dsp::Smoother cutSm, levelSm;
};
} // namespace mb
