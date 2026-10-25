#pragma once
#include "devices/DeviceProcessor.h"
#include "devices/DeviceFx.h"
#include "dsp/Oscillators.h"
#include "dsp/Envelopes.h"
#include "Algorithms.h"

namespace mb
{
/** Minibox FM (§8.2): 6 operators, 32 algorithms, pitch EG, LFO, Poly/Mono/Legato with up to 8 voices,
    and a non-destructive macro layer that offsets every modulator or carrier at once. */
class FmDevice : public DeviceProcessor
{
public:
    FmDevice();
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void handleEvent (const NoteEvent&) override;
    void render (float* L, float* R, int n) override;
    bool anyVoiceActive() const override;

    static constexpr int kOps = 6, kVoices = 8;
    /** Parameter index of operator `op` (0-based) field `f`. */
    enum OpField { Ratio, Fixed, Freq, Detune, Level, VelSens, KeyScale, RateScale, R1, R2, R3, R4, L1, L2, L3, L4, Wave, Mute, kOpFields };
    int opParam (int op, OpField f) const { return opBase + op * kOpFields + (int) f; }
    int globalParam (const char* id) const { return layout.indexOf (id); }

    /** Is operator `op` a carrier in the current algorithm (for the macro layer and the UI)? */
    bool isCarrier (int op) const;

    /** Parse a standard 32-voice 6-operator bank dump (4104 bytes). Returns parameter maps by id. */
    struct ImportedVoice { juce::String name; std::vector<std::pair<juce::String, float>> values; };
    static std::vector<ImportedVoice> parseBank (const juce::MemoryBlock&);

    static constexpr float kLevelScale = 0.55f;   // eight voices sum: the soft output stage rounds the rare full-velocity 8-note chord
private:
    struct Voice
    {
        VoiceSlot slot;
        std::array<dsp::SineOp, kOps> op;
        std::array<dsp::RateLevelEnv, kOps> env;
        dsp::RateLevelEnv pitchEnv;
        float pitch = 60.0f, target = 60.0f, vel = 0.8f, fadeIn = 1.0f;
        std::array<float, kOps> last {};
    };
    void noteOn (int note, float vel);
    void noteOff (int note);

    int opBase = 0;
    std::array<Voice, kVoices> voices;
    dsp::Lfo lfo;
    DeviceFx fx;
    DeviceReverb verb;
    HeldStack held;
    uint32_t clock = 0;
    dsp::Rng rng { 6006u };
    std::vector<float> bufL, bufR;
    dsp::Smoother levelSm;
    int pAlgorithm = 0, pFeedback = 0, pVoiceMode = 0, pVoices = 0, pTranspose = 0, pGlide = 0;
    int pLfoShape = 0, pLfoRate = 0, pLfoDelay = 0, pLfoPitch = 0, pLfoAmp = 0;
    int pPeg[6] {};
    int pMacro[6] {};
    int pChorusMode = 0, pChorusRate = 0, pChorusDepth = 0, pChorusMix = 0, pReverb = 0, pLevel = 0, pRevSize = 0;
};
} // namespace mb
