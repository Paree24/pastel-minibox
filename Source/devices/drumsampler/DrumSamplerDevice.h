#pragma once
#include "devices/DeviceProcessor.h"
#include "devices/SampleSlot.h"
#include "dsp/Effects.h"
#include "devices/DeviceFx.h"

namespace mb
{
/** Minibox Drum Sampler (§8.6): the Sampler voice per pad without key tracking. Per pad: sample,
    start, length, pitch, attack, decay, pitch EG, hi-cut, level, pan, sends, bits, drive, play mode,
    reverse, choke. A dropped loop is sliced across the pads. */
class DrumSamplerDevice : public DeviceProcessor
{
public:
    static constexpr int kPads = 8;
    DrumSamplerDevice();
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void handleEvent (const NoteEvent&) override;
    void render (float* L, float* R, int n) override;
    bool anyVoiceActive() const override;
    int laneCount() const override { return kPads; }
    juce::String laneName (int lane) const override;
    int laneNote (int lane) const override;
    int laneForNote (int note) const override { return padForNote (note); }
    void save (juce::ValueTree&) const override;
    void load (const juce::ValueTree&) override;

    enum Field { Start, Length, Pitch, Attack, Decay, PegInt, PegDecay, HiCut, Level, Pan, SendDelay, SendReverb,
                 Bits, Drive, Mode, Reverse, Choke, kFields };
    int padParam (int pad, Field f) const { return pad * kFields + (int) f; }
    int pSliceMode = 0, pSliceSens = 0, pKitLevel = 0, pRevMix = 0, pRevSize = 0;

    SampleSlot pad[kPads];
    juce::String padRef (int p) const { return refs[(size_t) p]; }
    void loadPad (int p, const juce::String& ref, bool sync = false);
    /** Slice a loop across all 8 pads (transients or equal parts). `done` gets the slice start positions (fractions
        of the loop) on the message thread, so the caller can fill the lanes at the project tempo. The file is
        decoded on the loader thread (`sync`, or no message loop: at once, and the starts are also returned). */
    std::vector<double> sliceLoop (const juce::String& ref, bool sync = false, std::function<void (std::vector<double>)> done = {});
    static std::vector<double> findSlices (const SampleData&, bool transients, float sensitivity, int count);

    std::array<std::atomic<int>, kPads> hits {};

private:
    struct Voice
    {
        bool active = false, held = false, forward = true;
        const SampleData* sample = nullptr;
        double pos = 0.0, end = 0.0;
        int t = 0;
        float vel = 1.0f, amp = 0.0f, rel = 1.0f, choke = 1.0f;
        float fadeOut = 1.0f, fadeStep = 0.0f;   // a retriggered pad's old playback fades out as a tail
        bool choking = false, releasing = false;
        dsp::OnePole lp1L, lp2L, lp1R, lp2R;
        dsp::StereoDrive drive;
        dsp::Resampler crush, crushR;
    };
    void renderVoice (Voice&, int pad, int n);
    void trigger (int p, float vel);
    std::vector<double> applySlices (const juce::String& ref, SamplePtr data);
    int padForNote (int note) const;

    std::array<Voice, kPads> voices;
    std::array<Voice, kPads> tails;   // per pad: the playback a retrigger cut off, fading out (no click)
    std::array<juce::String, kPads> refs;
    std::shared_ptr<int> lifetime = std::make_shared<int> (0);   // async loads check it before touching the device
    std::vector<float> bufL, bufR, sendL, sendR;
    DeviceReverb verb;
    dsp::Smoother levelSm;
};
} // namespace mb
