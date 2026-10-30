#pragma once
#include "engine/Engine.h"
#include "dsp/Effects.h"

namespace mb
{
/** The groovebox's master effects (8 slots in series) and its 8 hold-to-play performance effects, on the master
    bus's insert point: pads first, then the slots, then the bus's own glue, volume and limiter (docs/master-fx.md).
    The effects are Pastel EFX Grid's (Source/efx/README.md). Standalone only: the plugin installs none.

    Message thread: `sync()` after a slot's effect changes (new instances are built outside the engine's lock);
    `setHeld` from the pads, the keys and MIDI. The audio thread reads the project's FxSlot values directly. */
class MasterFx : public MasterInsert
{
public:
    explicit MasterFx (Engine&);
    ~MasterFx() override;
    void prepare (double sampleRate, int maxBlock) override;
    void process (float* L, float* R, int n, double bpm, double ppq, bool playing) override;
    bool trackActive (int pattern, int track) const override;
    void processTrack (int pattern, int track, float* L, float* R, int n, double bpm, double ppq, bool playing) override;

    /** A track's effect slot (0–2): the effect at its defaults ("" empties it), built here, swapped in under the lock. */
    void setTrackEffect (int pattern, int track, int slot, const juce::String& effect);

    /** Builds or drops instances to match the project's slots; gives empty pads their effects. */
    void sync();
    /** Edits that change a slot's effect: its settings and its running instance change together, under the lock.
        `effect` "" empties a chain slot; for a pad it restores the pad's own settings. */
    void setSlotEffect (bool pad, int index, const juce::String& effect);
    void removeSlot (int index);            // the later slots shift left (no gaps)
    void moveSlot (int from, int to);
    /** A pad held (its effect fades in and starts fresh) or released (it fades out). */
    void setHeld (int pad, bool held);
    bool isHeld (int pad) const { return pad >= 0 && pad < kPerformFx && held[(size_t) pad].load(); }
    /** The running instance of a slot or pad (message thread: meters, the drawer). */
    const efx::GridEffect* instance (bool perform, int index) const;

    static const char* padName (int pad);                      // "Stutter" …
    /** The pad's effect with its performance settings (the effect's macro at full, held repeats, full wet). */
    static void defaultPad (FxSlot&, int pad);
    /** A slot's effect at its defaults ("" empties it). */
    static void setEffect (FxSlot&, const juce::String& effectName);
    /** The effects a master slot can take (analysis-only and graphically edited ones are left out). */
    static const std::vector<int>& choosableEffects();

private:
    struct Inst { std::unique_ptr<efx::GridEffect> fx; juce::String name; };
    void syncOne (Inst&, const juce::String& want);
    std::unique_ptr<efx::GridEffect> build (const juce::String& effect) const;
    void run (Inst&, const FxSlot&, float* L, float* R, int n, double bpm, double ppq, bool playing, bool bypass);

    Engine& engine;
    std::array<Inst, kMasterFxSlots> slots;
    std::array<Inst, kPerformFx> pads;
    std::array<std::array<std::array<Inst, kTrackFx>, kTracks>, kPatterns> tracks;   // mostly empty
    std::array<std::atomic<bool>, kPerformFx> held {};
    std::array<bool, kPerformFx> wasHeld {};
    int syncedGen = -1;   // a newly loaded project waits (dry) until sync() built its effects
    double sampleRate = 48000.0;
    int maxBlock = 512;
};
} // namespace mb
