#pragma once
#include "devices/DeviceProcessor.h"
#include "devices/DeviceFx.h"
#include "dsp/Oscillators.h"
#include "dsp/Filters.h"
#include "dsp/Analog.h"
#include "dsp/Envelopes.h"
#include "dsp/Effects.h"
#include "Arp.h"

namespace mb
{
/** Minibox Poly (§8.1): 6 voices with a DCO engine (DCO, stepped HPF, 24 dB ladder) and a VA engine
    (saw/pulse/tri, 12 dB OTA with shared or per-voice filter). Voice modes, envelopes, LFO,
    arpeggiator and FX are shared by both engines. */
class PolyDevice : public DeviceProcessor
{
public:
    PolyDevice();
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void handleEvent (const NoteEvent&) override;
    void render (float* L, float* R, int n) override;
    bool anyVoiceActive() const override;

    enum P { engine,
             dcoWave, dcoPw, dcoPwm,
             vaWave, vaOctave, vaPw, vaPwm, vaFine,
             sub, noise, drift,
             hpf, cutoff, reso, envAmt, key, filterVel, filterMode, filterDrive,
             fAttack, fDecay, fSustain, fRelease,
             aAttack, aDecay, aSustain, aRelease, ampSrc, level, velocity,
             lfoShape, lfoSync, lfoRate, lfoDiv, lfoDelay, lfoPitch, lfoPwm, lfoCut,
             voiceMode, detune, glide, chord,
             arpMode, arpRange, arpRate, arpGate,
             driveType, driveAmt, driveTone,
             chorusMode, chorusMix,
             delaySync, delayDiv, delayFree, delayFb, delayTone, delayMix, reverb,
             revSize, revTone, revPre,
             numParams };
    static constexpr int kVoices = 6;

    static constexpr float kLevelScale = 0.7f;   // six voices sum: the soft output stage rounds the rare full-velocity 8-note chord
private:
    float exprNow = 0.0f;   // pitch bend and vibrato for this render (semitones)
    struct Voice
    {
        VoiceSlot slot;
        std::array<dsp::analog::Dco, 3> dco;          // DCO engine
        std::array<dsp::PolyBlepOsc, 3> osc;          // VA engine (VCOs)
        dsp::SubOsc subOsc;                           // VA engine sub
        dsp::Noise noiseGen;
        dsp::OnePole noiseLp;
        dsp::DcBlock ringDc;                          // Ring: the product's DC (two saws multiplied)
        dsp::analog::Hpf4 hp;
        dsp::analog::OtaCascade cascade;
        dsp::analog::Peak12 peak;
        dsp::analog::Oversampler2x os;
        dsp::analog::Tolerance tol;
        dsp::Adsr fenv, aenv;
        float pitch = 60.0f, target = 60.0f, vel = 0.8f, stack = 0.0f, pan = 0.0f;
        float fadeIn = 1.0f, velGain = 0.0f;   // glides: a stolen voice at a new velocity never steps
        float gateAmp = 0.0f;                  // Amp: Gate: opens and closes with the key in 1.5 ms (no click)
        dsp::Rng rng;
    };
    void voiceOn (int note, float vel, int sourceKey);
    void voiceOff (int sourceKey);
    void keyOn (int note, float vel);
    void keyOff (int note);
    void renderChunk (float* L, float* R, int start, int len);

    std::array<Voice, kVoices> voices;
    std::array<int, kVoices> voiceKey {};   // key that owns each voice (chord / unison spread)
    dsp::Lfo lfo;
    dsp::analog::Peak12 sharedFilter;
    dsp::analog::Oversampler2x sharedOs;
    dsp::Adsr sharedEnv;
    dsp::StereoDrive drive;
    int arpModeNow() const { return outerArp ? 0 : (int) get (arpMode); }   // Off while the groovebox's arp plays this track
    bool delayWasOn = false;   // the delay stops while off; it is cleared when it comes back
    dsp::analog::BbdChorus60 chorus;
    dsp::TapeDelay delay;
    DeviceReverb verb;
    Arpeggiator arp;
    HeldStack held;
    uint32_t clock = 0;
    float lastPitch = 60.0f;
    dsp::Rng rng { 9191u };
    std::vector<float> bufL, bufR, lfoBuf;
    dsp::Smoother levelSm;
};
} // namespace mb
