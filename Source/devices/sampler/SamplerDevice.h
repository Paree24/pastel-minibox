#pragma once
#include "devices/DeviceProcessor.h"
#include "devices/SampleSlot.h"
#include "dsp/Filters.h"
#include "dsp/Envelopes.h"
#include "dsp/Effects.h"
#include "devices/DeviceFx.h"

namespace mb
{
/** Minibox Sampler (§8.5): up to 8 key zones played chromatically, start/length/loop/reverse,
    pitch EG, SVF, ADSR, LFO, Clean or Vintage resampling, drive, 1–8 voices. */
class SamplerDevice : public DeviceProcessor
{
public:
    static constexpr int kZones = 8, kVoices = 8;
    SamplerDevice();
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void handleEvent (const NoteEvent&) override;
    void render (float* L, float* R, int n) override;
    bool anyVoiceActive() const override;
    void save (juce::ValueTree&) const override;
    void load (const juce::ValueTree&) override;

    enum P { playMode, start, length, loopStart, loopLen, reverse,
             tune, fine, pegInt, pegAttack, pegDecay,
             character, bits, rate, drive,
             filterType, cutoff, reso, envAmt, key, lfoCut,
             attack, decay, sustain, release, velocity,
             lfoShape, lfoRate, lfoPitch,
             voices, glide, voiceMode, reverb, level, revSize, revTone, revPre, zoneParams };
    int zoneRoot (int z) const { return zoneParams + z * 3; }
    int zoneLo (int z) const { return zoneParams + z * 3 + 1; }
    int zoneHi (int z) const { return zoneParams + z * 3 + 2; }

    SampleSlot zone[kZones];
    int numZones() const;
    /** Message thread: load a sample ref into a zone (async). */
    void loadZone (int z, const juce::String& ref, bool sync = false);
    /** Several files dropped at once become key zones spread across the keyboard. */
    void loadZonesFromFiles (const juce::Array<juce::File>& files);
    juce::String zoneRef (int z) const;

    /** Sample positions (0..1 of the file) for the hero waveform markers. */
    struct Region { double s, e, ls, le; };
    Region region (int length) const;

private:
    struct Voice
    {
        VoiceSlot slot;
        const SampleData* sample = nullptr;
        int zoneIdx = 0;
        double pos = 0.0;
        bool forward = true;
        float rootNote = 60.0f, pitch = 60.0f, target = 60.0f, vel = 0.8f, fadeIn = 1.0f;
        dsp::Adsr env;
        float pegValue = 0.0f;
        int pegStage = 0;
        dsp::SvfTpt filter;
        dsp::Resampler vintage;
    };
    void noteOn (int note, float vel);
    void noteOff (int note);
    int zoneFor (int note) const;

    std::array<Voice, kVoices> voicesArr;
    std::array<juce::String, kZones> refs;
    std::shared_ptr<int> lifetime = std::make_shared<int> (0);   // async loads check it before touching the device
    dsp::Lfo lfo;
    dsp::Drive driveFx;
    HeldStack held;
    uint32_t clock = 0;
    std::vector<float> bufL, bufR;
    DeviceReverb verb;
    dsp::Smoother levelSm;
};
} // namespace mb
