#pragma once
#include "devices/DeviceProcessor.h"
#include "dsp/Oscillators.h"
#include "dsp/Filters.h"
#include "dsp/Effects.h"
#include "dsp/DrumModels.h"
#include "devices/DeviceFx.h"

namespace mb
{
namespace drums
{
/** Engines name the machine a part imitates (see docs/research/drums.md): Classic = the 1980 analog
    machine, Punch = the 1983 analog/PCM machine, Box = the compact analog/PCM beat box, Ring = the
    resonating-filter kick synth, Layer = the 2-layer DSP percussion synth, Bell = the 1980 cowbell. */
enum Engine { Classic, Punch, Box, Ring, Layer, Bell, kNumEngines };
const char* engineName (Engine);
/** Engines offered by each part (Kick, Snare, Clap, Tom, C Hat, O Hat, Cymbal, Perc). */
const std::vector<Engine>& enginesForPart (int part);
}

/** Minibox Drums (§8.4): 8 parts with a choice of synthesis engine each, per-part mix and sends,
    choke groups, a shared Tube/String kit resonator and kit compression, drive and width. */
class DrumsDevice : public DeviceProcessor
{
public:
    static constexpr int kParts = 8;
    DrumsDevice();
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void handleEvent (const NoteEvent&) override;
    void render (float* L, float* R, int n) override;
    bool anyVoiceActive() const override;
    int laneCount() const override { return kParts; }
    juce::String laneName (int lane) const override;
    int laneNote (int lane) const override;

    enum Field { Eng, Pitch, Bend, BendTime, Decay, Tone, Click, Accent, Drive, DriveType, Level, Pan, SendDelay,
                 SendReverb, VelSens, Choke, ResSend, LayA, LayB, LayMod, LayEnv, LayMix, LayRatio, kFields };
    int partParam (int part, Field f) const { return part * kFields + (int) f; }
    drums::Engine partEngine (int part) const;
    int partForNote (int note) const;
    int laneForNote (int note) const override { return partForNote (note); }

    /** Trigger counters per part for the pad flash in the UI. */
    std::array<std::atomic<int>, kParts> hits {};

private:
    /** One sounding hit of a part: the model chosen by the part's engine plus the part's drive. */
    struct Voice
    {
        bool active = false, choking = false;
        float vel = 1.0f, gain = 1.0f, fadeStep = 0.0f;   // fadeStep > 0: fading out (retrigger or choke)
        drums::Engine eng = drums::Classic;
        int model = 0;
        dsp::drum::Kick808 k808;
        dsp::drum::Kick909 k909;
        dsp::drum::KickResonator kres;
        dsp::drum::Snare808 s808;
        dsp::drum::Snare909 s909;
        dsp::drum::Clap clap;
        dsp::drum::Tom909 t909;
        dsp::drum::Ping808 ping;
        dsp::drum::Cowbell bell;
        dsp::drum::Metal808 metal;
        dsp::drum::Layered lay;
        dsp::drum::BoxKick bkick;
        dsp::drum::BoxSnare bsnare;
        dsp::drum::BoxTom btom;
        dsp::drum::BoxHat bhat;
        dsp::drum::PcmVoice pcm;
        dsp::Drive drive;
        dsp::OnePole hpOut;
    };
    float processPart (int part, Voice& v, const dsp::drum::Ctl& c);
    dsp::drum::Ctl ctlFor (int part, const Voice& v) const;
    void trigger (int part, float vel, bool accent);

    std::array<Voice, kParts> voices, tails;
    // stored recordings for the PCM voices (built in prepare)
    std::vector<float> pcmClap, pcmCrash, pcmAgogo, pcmHat, pcmRide;   // tails: the previous hit fading out after a retrigger
    dsp::Rng rng { 99u };
    int kitBase = 0;
    int pResType = 0, pResTune = 0, pResDecay = 0, pResDamp = 0, pResMix = 0;
    int pComp = 0, pKitDrive = 0, pWidth = 0, pKitLevel = 0, pRevMix = 0, pRevSize = 0;
    dsp::Waveguide resonator;
    dsp::GlueComp comp;
    dsp::StereoDrive kitDrive;
    bool resWasOn = false;     // the resonator stops while off; it is cleared when it comes back
    std::vector<float> bufL, bufR, sendL, sendR;
    DeviceReverb verb;
    dsp::Smoother levelSm;
    struct Snap { float p[kFields]; } snap[kParts];
};
} // namespace mb
