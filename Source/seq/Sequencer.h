#pragma once
#include "model/Project.h"
#include "devices/DeviceProcessor.h"
#include <atomic>

namespace mb
{
/** An event for one block: a note for a (pattern, track) device, a lock change or a metronome click. */
struct SeqEvent
{
    enum Kind { Note, ClearLocks, Lock, Click } kind = Note;
    int offset = 0;       // sample offset inside the block
    int pattern = 0, track = 0;
    NoteEvent note;
    int param = -1;
    float value = 0.0f;
};

/** Device lookup the sequencer needs (lane notes for drum devices). */
struct DeviceLookup
{
    virtual ~DeviceLookup() = default;
    virtual DeviceProcessor* deviceAt (int pattern, int track) = 0;
    /** At the pattern's end: true when it has just been made a bar longer (live recording still playing), so
        playback runs on into the new bar instead of looping. Audio thread. */
    virtual bool growAtEnd (int /*pattern*/) { return false; }
};

/** Pattern playback with swing, micro-timing, probability, ratchets, ties, parameter locks,
    queued pattern switching, count-in and metronome. Runs on the audio thread. */
class Sequencer
{
public:
    void prepare (double sr) { sampleRate = sr; pending.reserve (8192); }

    /** Called at the start of each block, before rendering. Appends events with offsets in [0, n). */
    void generate (Project& p, DeviceLookup& devs, int n, std::vector<SeqEvent>& out);

    // ---- control (audio thread, via Engine)
    void start (Project& p, bool fromTop);
    void stop();
    void resetPosition() { step = 0; stepPhase = 0.0; seekTarget = 0; }
    /** Where playback is, in steps (before a block is generated: the step it starts from). */
    double position() const { return needBoundary ? (double) seekTarget : step + stepPhase; }
    /** Jump to a step (playing or not): it plays from there on the next boundary. */
    void seek (int s) { step = s; stepPhase = 0.0; seekTarget = s; needBoundary = true; pending.clear(); }
    /** The step after `si` in the active pattern (its loop range, the pattern's end). */
    int nextStep (const Project&, int si) const;
    bool isPlaying() const { return playing; }
    int currentStep() const { return step; }
    double currentStepPhase() const { return stepPhase; }
    double beatPosition() const { return beatPos; }

    int activePattern = 0;
    int queuedPattern = -1;
    int countInSamplesLeft = 0;
    bool recordArmed = false;
    int pass = 0;              // completed passes of the active pattern since play (trig conditions)
    int pendingBeats = 0, pendingUnit = 0;   // time-signature change applied at the next bar

    /** Pattern that was switched away from (its devices tail out), -1 when none. */
    int lastSwitchedFrom = -1;
    bool switchedThisBlock = false;

private:
    struct Pending { int64_t time; SeqEvent e; };
    void boundary (Project& p, DeviceLookup& devs, int offset, std::vector<SeqEvent>& out);
    void scheduleStep (Project& p, DeviceLookup& devs, int stepIndex, int offset, double stepLen);
    void push (int64_t time, const SeqEvent& e);

    double sampleRate = 48000.0;
    bool playing = false;
    int step = 0;              // the step that is currently playing
    double stepPhase = 0.0;    // fraction of the current step elapsed
    bool needBoundary = true;  // the next boundary is the very first step after start
    int64_t now = 0;
    double beatPos = 0.0;
    double clickPhase = 0.0;
    int clickBeat = 0;
    std::vector<Pending> pending;
    juce::Random rng;
    uint32_t chanceSeed = 1;
    bool entered = false;
    int seekTarget = 0;
    /** 0–99, fixed for a step on a pass (probability), so a step's early and on-time notes agree. */
    int chance (int pat, int track, int step, int pass) const
    {
        uint32_t h = chanceSeed ^ ((uint32_t) pat * 0x9E3779B1u) ^ ((uint32_t) track * 0x85EBCA77u) ^ ((uint32_t) step * 0xC2B2AE3Du) ^ ((uint32_t) pass * 0x27D4EB2Fu);
        h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
        return (int) (h % 100u);
    }
};
} // namespace mb
