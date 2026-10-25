#pragma once
#include "devices/DeviceProcessor.h"
#include "dsp/Oscillators.h"
#include "dsp/Envelopes.h"
#include "dsp/Effects.h"
#include "dsp/Bassline.h"
#include "dsp/Analog.h"
#include "dsp/Pedals.h"
#include "devices/DeviceFx.h"

namespace mb
{
/** Minibox Acid (§8.3): the bass-line voice modelled from its circuit (docs/research/synths.md): saw /
    square oscillator, the diode-ladder filter (Bassline.h) at 4× oversampling with its coupling
    high-passes, the main envelope (normal and accented decay), the accent paths to the VCA and,
    through the sweep circuit, to the filter, the volume envelope, slide. Extras: Stack, sub, noise,
    key tracking, LFO, a pedal drive (overdrive / distortion / fuzz circuits), a ducking delay and reverb. */
class AcidDevice : public DeviceProcessor
{
public:
    AcidDevice();
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void handleEvent (const NoteEvent&) override;
    void render (float* L, float* R, int n) override;
    bool anyVoiceActive() const override { return noteOn || amp > 1.0e-4 || meg > 1.0e-4; }

    enum P { oscWave, pw, sub, noise, tune, fine,
             stack, stackMode, stackDetune, stackSpread, stackMix,
             filterType, cutoff, reso, envMod, decay, key, bassComp,
             accAmt, sweep, accDecay,
             slideMode, slideTime,
             ampMode, level, velocity, ampDecay,
             lfoShape, lfoSync, lfoRate, lfoDiv, lfoCut, lfoPitch, lfoFade,
             driveType, driveAmt, driveTone, driveMix,
             delaySync, delayDiv, delayFree, delayFb, delayDuck, delayTone, delayMix, reverb,
             artic, revSize, revTone, revPre,
             numParams };
    /** Keyswitches (momentary, never sounded): while C♯0 / D0 / D♯0 is held, notes play accented / sliding /
        both; on release the Articulation chip's own setting returns. */
    static constexpr int kKeyswitchLow = 13, kKeyswitchHigh = 15;
    static constexpr int kOversampling = 4;

    static constexpr float kLevelScale = 0.6f;    // the accent amp boost (up to 4.45×) is rounded by the soft output stage
private:
    void trigger (int note, bool accent, float vel);
    void slideTo (int note, bool accent);

    // oscillators (main + two stack voices) at the oversampled rate
    std::array<dsp::PolyBlepOsc, 3> osc;
    dsp::SubOsc subOsc;
    dsp::Noise noiseGen;
    dsp::bass::DiodeLadder filter;
    dsp::bass::AccentSweep sweepCircuit;
    dsp::OnePole couplingHp, outHp;   // the circuit's coupling high-passes
    dsp::analog::Oversampler2x dec1, dec2;   // 4× → 2× → 1×
    dsp::Lfo lfo;
    dsp::pedal::Board drive;   // Overdrive / Distortion / Fuzz pedal circuits (docs/research/pedals.md)
    dsp::TapeDelay delay;
    DeviceReverb verb;
    HeldStack held;

    double baseRate = 48000.0;
    double oscFreq = 110.0, slewedFreq = 110.0, slewCoef = 0.0;
    double pitchSemis = 45.0, targetSemis = 45.0;     // legato slide works in semitones
    int currentNote = -1;
    double megTarget = 0.0, megAttack = 0.05;
    double meg = 0.0, megCoef = 0.999, accentGain = 0.0, accVca = 0.0, accVcaCoef = 0.0;
    double amp = 0.0, ampAttack = 0.0, ampTarget = 0.0, ampDecayCoef = 0.0, ampReleaseCoef = 0.0;
    bool noteOn = false, accentNow = false, heldSlides = false;
    int keysHeld = 0, articBase = 0;   // keyswitches down (bit per key) and the chip setting they override
    void keyswitch (int note, bool down);
    float vel = 0.8f;
    double velGain = 1.0;
    std::vector<float> bufL, bufR;
    dsp::Smoother levelSm;
    dsp::Rng rng { 31337u };
};
} // namespace mb
