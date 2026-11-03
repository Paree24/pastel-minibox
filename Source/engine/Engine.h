#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "model/Project.h"
#include "devices/DeviceProcessor.h"
#include "seq/Sequencer.h"
#include "seq/NoteInput.h"
#include "MasterBus.h"
#include <atomic>
#include <memory>
#if JUCE_LINUX || JUCE_ANDROID || JUCE_MAC
 #include <pthread.h>
#endif

namespace mb
{
/** The audio lock. Where the platform has one (Linux, Android, macOS) it is a priority-inheriting mutex: a UI thread
    holding it for an edit is lifted to the audio thread's priority until it lets go, so the audio thread never waits
    behind a pre-empted UI thread (with the 2–4 ms buffers of a phone that wait would be a dropout). */
class AudioLock
{
public:
   #if JUCE_LINUX || JUCE_ANDROID || JUCE_MAC
    AudioLock()
    {
        pthread_mutexattr_t a;
        pthread_mutexattr_init (&a);
        pthread_mutexattr_settype (&a, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutexattr_setprotocol (&a, PTHREAD_PRIO_INHERIT);
        pthread_mutex_init (&m, &a);
        pthread_mutexattr_destroy (&a);
    }
    ~AudioLock() { pthread_mutex_destroy (&m); }
    void enter() const noexcept { pthread_mutex_lock (&m); }
    void exit() const noexcept { pthread_mutex_unlock (&m); }
   #else
    void enter() const noexcept { cs.enter(); }
    void exit() const noexcept { cs.exit(); }
   #endif
    using ScopedLockType = juce::GenericScopedLock<AudioLock>;

private:
   #if JUCE_LINUX || JUCE_ANDROID || JUCE_MAC
    mutable pthread_mutex_t m;
   #else
    juce::CriticalSection cs;
   #endif
    JUCE_DECLARE_NON_COPYABLE (AudioLock)
};

/** Owns the rack(s), the device pool, the master bus and the sequencer, and renders audio.

    Threading: parameters are atomics written directly by the UI. Structural changes (device swaps,
    project loads, transport and pattern switches, step/lock edits) take the audio lock with
    ScopedEdit. New devices are built outside the lock, swapped under it and freed after it. */
class Engine : private DeviceLookup
{
public:
    enum class Mode { Groovebox, Plugin };
    explicit Engine (Mode m = Mode::Groovebox);
    ~Engine() override;

    Mode mode() const { return engineMode; }
    void prepare (double sampleRate, int maxBlock);
    /** Message thread: master limiter look-ahead on/off. Only the limiter is rebuilt (outside the lock). */
    void setLookAhead (bool on);
    double getSampleRate() const { return sampleRate; }
    int getMaxBlock() const { return maxBlock; }

    /** Audio callback (any block length: longer than prepared is rendered in pieces). `midi` may be null.
        Output is overwritten. */
    void process (float* L, float* R, int n, const juce::MidiBuffer* midi);

    struct ScopedEdit
    {
        explicit ScopedEdit (Engine& e) : lock (e.audioLock) {}
        AudioLock::ScopedLockType lock;
    };

    // ---- model and devices
    Project& project() { return proj; }
    DeviceProcessor* device (int pattern, int track) const { return devices[(size_t) pattern][(size_t) track].get(); }
    /** Replace a slot's device (message thread). Returns the previous device so it is freed outside the lock. */
    std::unique_ptr<DeviceProcessor> setDevice (int pattern, int track, std::unique_ptr<DeviceProcessor> d);
    /** The user swapping a track's device (a picker): as setDevice, and a drum pattern follows its parts by note, so
        switching between the drum synth and the Drum Sampler keeps a hat a hat (their lanes are in different orders). */
    std::unique_ptr<DeviceProcessor> replaceDevice (int pattern, int track, std::unique_ptr<DeviceProcessor> d);
    /** Load a whole new project (devices are prepared outside the lock). */
    void replaceProject (std::unique_ptr<Project> p, std::array<std::array<std::unique_ptr<DeviceProcessor>, kTracks>, kPatterns> devs);
    void newProject();

    /** Plugin mode: the host's tempo and position drive synced LFOs, delays and the arpeggiator. */
    void setHostTransport (double bpm, double beatPos, bool playing) { hostBpm = bpm; hostBeat = beatPos; hostPlaying = playing; }
    double hostBeatPosition() const { return hostBeat; }

    // ---- plugin mode: one instrument
    DeviceProcessor* pluginDevice() const { return devices[0][0].get(); }
    Mixer& pluginMixer() { return proj.patterns[0].slots[0].mixer; }

    // ---- transport (message thread)
    void play();
    void stop();                       // once: stop with tails; twice: reset to bar 1 and kill tails
    void setRecord (bool on);
    bool isRecording() const { return recording.load(); }
    bool isPlaying() const { return playingFlag.load(); }
    void queuePattern (int p);         // click on A–D
    void copyPattern (int from, int to);
    void clearPattern (int p);
    int activePattern() const { return activePat.load(); }
    int queuedPattern() const { return queuedPat.load(); }
    int currentStep() const { return curStep.load(); }
    float patternProgress() const { return progress.load(); }
    bool inCountIn() const { return countingIn.load(); }
    void setTimeSignature (int beats, int unit);

    // ---- focus: where keyboard / other-channel MIDI and preview notes go
    std::atomic<int> focusPattern { 0 }, focusTrack { 0 };
    /** Notes from the UI (computer keyboard, pads). Lock-free; drained by the audio thread. */
    /** Message thread: writes what live and step recording queued into piano-roll clips (a Steps track is
        converted on its first recorded note). Called by the UI's timer; true when anything changed. */
    bool applyRecorded();
    /** A note from the UI (message thread). `audition`: an editor previewing a note (the piano roll), which plays
        as it is (no scale, chord or arp) and is never recorded. */
    void injectNote (int track, int note, float velocity, bool on, int pattern = -1, bool audition = false);
    /** Record a knob move as a parameter lock when recording and playing (message thread). */
    void recordKnob (int pattern, int track, int param, float value);
    /** Motion recording (message thread): a parameter turned while recording is written as a lock on the step
        playing, and on every step it passes while it is still being turned (recordTouched, from the UI timer). */
    void touchParam (int pattern, int track, int param, float value);
    /** A macro turned while recording: a point on the clip's macro lane (piano roll), or locks for the
        parameters it moves (steps). */
    /** Recorded automation of a parameter on a pattern's track (its step locks), and clearing it. */
    bool hasAutomation (int pattern, int track, int param) const;
    void clearAutomation (int pattern, int track, int param);
    /** A macro's automation wherever its device plays: its clip lane, or its targets' step locks. */
    bool hasMacroAutomation (const DeviceProcessor* dev, int macro);
    void clearMacroAutomation (const DeviceProcessor* dev, int macro);
    void recordMacro (const DeviceProcessor*, int macro, float value);
    void recordTouched();
    /** The step under the playhead, for step-lock editing. */
    int stepForRecording() const;
    /** Step recording: where the next note or chord is written (per pattern). */
    std::atomic<int> stepRecordCursor { 0 };
    /** The notes a track's last key played (its chord in Chords mode, the in-key note otherwise; the audio thread
        writes, the UI reads): a tap on an empty step writes them (the phone's step editor). */
    std::array<std::array<std::atomic<int>, 8>, kTracks> lastKeyNotes {};
    std::array<std::atomic<int>, kTracks> lastKeyCount {};
    /** Step recording controls (message thread; applied by the audio thread). Rest leaves a gap of one
        step length, Back steps the cursor back and clears that step. */
    void stepRest() { cmdStep = 1; }
    void stepBack() { cmdStep = 2; }
    /** Step recording: move the cursor without writing or erasing (←/→). */
    void stepMove (int d) { cmdStepMove += d; }
    /** Back to the start of the pattern: the playhead (if playing) and the step cursor. */
    void toStart() { cmdToStart = 1; }
    /** Move the playhead to a step (the ruler; playing or stopped). */
    void seek (int step) { cmdSeek = step; }
    /** Note repeat (a drum roll) while a pad is held: the note plays at once, then repeats every `beats` (0.25 = 1/16)
        on the beat grid: the sequencer's while playing, a free-running one when stopped. The hits go through the
        track's input like played notes (so they are recorded). Message thread; up to kRolls at once. */
    void setRoll (int track, int note, float velocity, double beats, bool on);
    static constexpr int kRolls = 8;
    /** Note repeat for everything played (the desktop's N): while on, every held key or pad rolls at `repeatBeats`
        (MIDI, the computer keyboard, the mouse). Drums hit; synths play each repeat for half the rate. */
    std::atomic<bool> noteRepeat { false };
    std::atomic<double> repeatBeats { 0.25 };
    /** The note repeat rates (keys 1–6 while repeat is on), in beats, and their names. */
    static constexpr int kRepeatRates = 6;
    static double repeatRate (int i) { constexpr double r[kRepeatRates] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 }; return r[std::clamp (i, 0, kRepeatRates - 1)]; }
    static const char* repeatRateName (int i) { const char* n[kRepeatRates] = { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" }; return n[std::clamp (i, 0, kRepeatRates - 1)]; }
    int repeatRateIndex() const { for (int i = 0; i < kRepeatRates; ++i) if (std::abs (repeatRate (i) - repeatBeats.load()) < 1.0e-6) return i; return 2; }
    /** The track that live input is playing right now (for the UI). */
    int inputTrack() const { return focusTrack.load(); }

    // ---- metering
    std::atomic<float> slotPeak[kPatterns][kTracks] {};
    MasterBus& master() { return masterBus; }
    /** The standalone's master / performance effects (message thread; null removes them). */
    void setMasterInsert (MasterInsert*);
    bool masterInsertIs (const MasterInsert* m) const { return masterInsert == m; }
    /** Bumped (under the lock) whenever a whole project replaces the current one. */
    int projectGeneration() const { return projectGen; }
    std::atomic<float> cpuLoad { 0.0f };

    AudioLock audioLock;

private:
    DeviceProcessor* deviceAt (int pattern, int track) override { return devices[(size_t) pattern][(size_t) track].get(); }
    void renderSub (float* L, float* R, int start, int len);
    void applyEvent (const SeqEvent& e);
    void routeMidi (const juce::MidiMessage& m, int offset, std::vector<SeqEvent>& out);
    /** A key from MIDI or the UI: through the track's note input (scale, chord, arp) to the device and the recorder. */
    void inputKey (int pattern, int track, int note, float vel, bool on, int offset, std::vector<SeqEvent>& out, bool fromRoll = false);
    void flushInput (int track, std::vector<SeqEvent>& out);
    /** Tracks whose Arp is on for playback: their sequenced notes (steps or clip) are held keys for the track's
        arpeggiator, with its settings as they are now; what it plays replaces them in `out`. `beat0`: the block's
        start in beats. */
    void runPlaybackArps (int n, double beat0, std::vector<SeqEvent>& out);
    void recordNote (int track, int note, float vel, bool on, int offset);
    /** Live recording of a controller (pitch bend, mod wheel, expression, pressure) into the clip's lane. */
    void recordControl (int track, int lane, float value, int offset);
    std::array<std::array<float, 256>, kTracks> lastRecordedCC {};
    void stepRecord (int track, const int* notes, int count, float vel, bool on);
    void stopAllDevices (bool kill);

    Mode engineMode;
    Project proj;
    std::array<std::array<std::unique_ptr<DeviceProcessor>, kTracks>, kPatterns> devices;
    Sequencer seq;
    MasterBus masterBus;
    MasterInsert* masterInsert = nullptr;
    void processBlock (float* L, float* R, int n, const juce::MidiBuffer* midi);   // n <= maxBlock
    juce::MidiBuffer chunkMidi;   // a longer block's MIDI, one piece at a time
    int projectGen = 0;
    double beatClock = 0.0;      // beats since Play (free-running while stopped): the master effects' tempo grid
    bool clockWasPlaying = false;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    std::vector<float> slotL, slotR, auxAL, auxAR, auxBL, auxBR;
    // sidechain (master.duck): samples since the last kick on a drum track, and the other tracks' gain this sub-block
    double duckAge = 1.0e12;
    std::vector<float> duckGain;
    // the metronome's click: mixed after the master bus (its effects, the performance pads and the limiter never touch
    // it), delayed by the bus's look-ahead so it stays on the beat with the music
    std::vector<float> clickBuf, clickRing;
    int clickRingPos = 0;
    std::vector<SeqEvent> events;
    // patterns switched away from, ringing out (until silent or 4 s): any number of them, each its own clock
    std::array<int64_t, kPatterns> tailSamples {};
    // an outgoing pattern rings at most this long (faded over the last 50 ms); shorter in Eco
    static double tailSecs() { return eco() ? 1.5 : 4.0; }
    std::array<bool, kPatterns> tailing {};
    std::array<std::array<int, kTracks>, kPatterns> trackFxTail {};   // samples a sleeping device's effects still ring
    void clearTails() { tailing.fill (false); }
    bool stopPressedOnce = false;

    // transport commands from the message thread
    std::atomic<int> cmdPlay { 0 }, cmdStop { 0 }, cmdPanic { 0 }, cmdStep { 0 }, cmdToStart { 0 }, cmdStepMove { 0 }, cmdSeek { -1 };
    // live input per track: scale lock, chord mode, arp (the pattern it plays into is remembered for its note-offs)
    std::array<NoteInput, kTracks> inputs;
    std::array<int, kTracks> inputPattern {};
    // the playback arpeggiators (Track::arp): one per track, and the pattern each is playing (−1 idle)
    std::array<NoteInput, kTracks> playArps;
    std::array<int, kTracks> playArpPattern { -1, -1, -1, -1, -1, -1, -1, -1 };
    std::vector<SeqEvent> arpIn;
    void resetPlaybackArps() { for (auto& a : playArps) a.reset(); playArpPattern.fill (-1); }
    int lastFocusTrack = 0, lastFocusPattern = 0;
    std::array<int, kTracks> stepHeld {};      // keys down in the current step-recorded chord
    std::atomic<bool> recording { false }, playingFlag { false }, countingIn { false };
    std::atomic<int> activePat { 0 }, queuedPat { -1 }, curStep { 0 };
    std::atomic<float> stepPhaseNow { 0.0f };   // the playhead within the current step (message thread reads)
    std::atomic<float> progress { 0.0f };
    std::atomic<int> pendingQueue { -2 };
    int pendingBeats = 0, pendingUnit = 0;

    // note repeat: the message thread fills a slot and sets `active`; the audio thread owns the timing
    struct Roll
    {
        std::atomic<bool> active { false };
        std::atomic<int> pattern { 0 }, track { 0 }, note { 0 };
        std::atomic<float> vel { 1.0f };
        std::atomic<double> beats { 0.25 };
        bool started = false;      // audio thread
        double nextBeat = 0.0;     // audio thread
        // a synth repeat's note-off, due half a repeat after its hit (audio thread)
        bool offPending = false;
        double offBeat = 0.0;
        int offPattern = 0, offTrack = 0, offNote = 0;
        int startOffset = 0;
    };
    std::array<Roll, kRolls> rolls;
    std::array<Roll, kRolls> liveRolls;   // note repeat on played keys: the audio thread's own (inputKey)
    void runRolls (int n, std::vector<SeqEvent>& out);
    void runRoll (Roll&, double b0, double b1, double perSample, int n, std::vector<SeqEvent>& out, bool synced);

    // UI note FIFO
    struct UiNote { int pattern, track, note; float vel; bool on, audition; };
    juce::AbstractFifo uiFifo { 256 };
    std::array<UiNote, 256> uiNotes {};
    // live recording: open notes waiting for their note-off (gate length)
    struct OpenNote { int track, note, step; double startPos; bool used = false; bool clip = false; double startAbs = 0.0; };
    std::array<OpenNote, 32> openNotes {};
    void endOpenNote (OpenNote&, int pattern, double pos, double abs);   // a held key's recording ends: its length
    void closeOpenNotes();
    // live recording: notes struck together (a chord) land on one step; Replace clears a step once per pass
    struct ChordGroup { double pos = -1.0e9; int step = -1, micro = 0, pattern = -1; };
    std::array<ChordGroup, kTracks> chordGroup {};
    std::array<std::array<int, kMaxSteps>, kTracks> replacedPass {};
    int blockLen = 0;
    struct Touched { int pattern, track, param; float value; double ms; };
    std::vector<Touched> touched;   // message thread
    double blockStartPos = 0.0;   // the playhead (steps) and the count-in left (samples) when this block began
    int blockCountIn = 0;
    double blockAbs = 0.0, absSteps = 0.0;   // steps played since Play, never wrapping (a held note's length)
    double absAt (int pat, int offset) const;
    /** A live-recorded event's position in steps, from the block's start (no wrapping guesses). False while the
        count-in still has more than a step to go (struck that early, it is not part of the take). */
    bool recordPosition (int pat, int offset, double& pos) const;
    // recording into piano-roll clips: the audio thread queues, the message thread writes (clips allocate)
    struct RecEvent { enum Kind { On, Off, Erase, CC } kind = On; int pattern = 0, track = 0, note = 0, vel = 100, tick = 0, len = 0, end = 0; float value = 0.0f; };
    static constexpr int kRecQueue = 1024;
    juce::AbstractFifo recFifo { kRecQueue };
    std::array<RecEvent, kRecQueue> recEvents {};
    void pushRec (const RecEvent&);
    std::array<std::array<uint32_t, 128>, kTracks> recOpen {};   // message thread: the clip note a held key is writing
    std::vector<uint32_t> takeIds;                                // notes written in this take (Replace keeps them)
    int takeIdsFor = -1;
    // live recording in Replace: the pattern grows instead of looping; when the take stops it is trimmed to the
    // bars its notes reach (never below the length it had)
    bool growAtEnd (int pattern) override;
    std::atomic<int> takeLastStep { -1 };            // audio thread: the last step the take's notes reach
    std::array<int, kPatterns> takeStartBars {};     // message thread: the lengths when the take started
    // the recorded pattern's tracks as they were before the take (a clip form too): doubling copies the
    // recorded track's earlier content into the new bars, never the take itself
    std::array<Track, kTracks> takeBefore, takeBeforeClip;
    int takePattern = -1;
    // the take's automation in record order (message thread): a lock (key = parameter, pos = step) or a lane point
    // (key = lane, pos = tick). Automation never lengthens the pattern: what lands past the kept bars folds back
    struct TakeAuto { int track = 0, key = 0, pos = 0; float value = 0.0f; bool lane = false; };
    std::vector<TakeAuto> takeAuto;
    // the recorded pattern's parameters and macros as they were when the take began: a lane a take starts writing
    // holds that value where the control was not moved (it played the control's new, end value there before)
    std::array<std::vector<float>, kTracks> takeParamBefore;
    std::array<std::array<float, 8>, kTracks> takeMacroBefore {};
    struct Filled { int track, key; std::vector<char> steps; };   // the steps a lane's fill wrote (they were empty)
    std::vector<Filled> takeFilled;                 // (track, parameter or 100 + macro) already filled this take
    bool fillAutomation (int track, int key);      // true the first time in this take
    std::atomic<bool> takeOpen { false };
    void beginTake();   // message thread: record armed and playing (Play with record on, or record on while playing)
    void endTake();     // message thread: Stop or record off; trims what the take grew
    std::atomic<int> recordTake { 0 };   // counts record-on presses (a new take replaces again)
    double clickPhase = 0.0;
    double hostBpm = 120.0, hostBeat = 0.0;
    bool hostPlaying = false;
    // sustain pedal: note-offs held while the pedal is down, per track
    // the sustain pedal is one pedal for every track: the keys it holds remember their track and pattern, so
    // changing the selected instrument with the pedal down neither strands nor swallows them
    bool sustainDown = false;
    std::array<std::array<bool, 128>, kTracks> sustained {};
    std::array<std::array<int8_t, 128>, kTracks> sustainedPattern {};
    float clickEnv = 0.0f, clickFreq = 1000.0f;
};
} // namespace mb
