#pragma once
#include "model/Project.h"
#include "Scales.h"
#include <random>

namespace mb
{
/** Live note input for one track: scale lock (snap or white-keys degree mapping), chord mode (diatonic or
    per-degree chords, inversion, bass, strum) and the arpeggiator (synced to the sequencer clock, free-running
    on the project tempo when stopped, latch). Ported from the user's Midi Note Modifier
    (~/ai_experiments/midi-note-modifier, PluginProcessor.cpp) and reshaped for the engine: events in,
    events out, sustain is handled before this stage. Audio thread only; no allocation after prepare(). */
class NoteInput
{
public:
    struct Out { int offset; int note; float vel; bool on; };
    static constexpr int kMaxEmit = 8;

    void prepare (double sampleRate);
    /** Forget every note without emitting offs (the engine silences the device itself). */
    void reset();
    /** Emit note-offs for everything this input is sounding and forget it (focus change, settings change). */
    void releaseAll (int offset);

    /** A key went down or up. Returns the notes the key plays (after scale and chord) for a note-on, so the
        step recorder can write them; 0 for a note-off. */
    int key (int note, float vel, bool on, int offset, const ScaleSettings&, const PlaySettings&, int* chordOut);
    /** Advance one block: arp steps, strummed notes and gate ends. ppq = the sequencer's beat at the block
        start when it is playing (synced), else the arp free-runs on bpm. */
    /** `swing`: while the pattern plays, the project whose swing the arp's steps follow (as the steps do). */
    void tick (int n, const PlaySettings&, bool synced, double ppq, double bpm, const Project* swing = nullptr);

    std::vector<Out> out;        // emitted this block, cleared by the engine after use
    int heldKeys() const { return numVoices; }

private:
    struct Voice { bool active = false; int numEmitted = 0; int emitted[kMaxEmit] {}; };
    struct PendingOn { int64_t time = 0; int note = 0; float vel = 0.0f; };

    int buildChord (int snapped, const ScaleSettings&, const PlaySettings&, int* chord);
    static void applyInversionBass (int* chord, int& count, int inversion, bool lower, bool bassBelow);
    void emit (int offset, int note, float vel, bool on) { if (out.size() < out.capacity()) out.push_back ({ offset, note, vel, on }); }
    void voiceOn (int offset, int note, float vel);
    void voiceOff (int offset, int note);
    void releaseVoiceNote (int offset, int note);
    void addToPool (const int* notes, int count);
    void removeFromPool (const int* notes, int count);
    void clearPool();
    int pickArpNote (const PlaySettings&);
    void stopArpNote (int offset);

    double sr = 48000.0;
    int64_t clock = 0;
    Voice voices[128];
    int numVoices = 0;
    int outRefCount[128] {};
    PendingOn pendingOns[64];
    int numPendingOns = 0;

    int pool[128] {}, poolSize = 0, played[128] {}, playedSize = 0, poolRef[128] {}, expanded[128 * 4] {};
    int arpNote = -1, arpStep = 0, gateOffIn = -1;
    double nextPpq = -1.0, samplesToNextTick = 0.0;
    float arpVel = 0.9f;
    bool arpImmediate = false, arpWasSynced = false, prevArpOn = false, prevLatch = false;
    int arpImmediatePos = 0;
    std::minstd_rand rng { 1234u };
};
} // namespace mb
