#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <memory>
#include "dsp/DspCommon.h"

namespace mb
{
enum class DeviceType { Mono = 0, Poly, Acid, FM, Sampler, Drums, DrumSampler, Count };

juce::String deviceTypeName (DeviceType);          // "Drum Sampler"
juce::String deviceTypeId (DeviceType);            // "drumsampler"
DeviceType deviceTypeFromId (const juce::String&); // Count when unknown
bool isDrumDevice (DeviceType);

enum class Unit { Plain, Percent, Hz, Seconds, Semis, Cents, Db, Ratio, Int, Choice, Division, Bits, KHz, Pan, Footage };

struct ParamInfo
{
    juce::String id, name;
    float min = 0.0f, max = 1.0f, def = 0.0f;
    bool log = false, bipolar = false;
    Unit unit = Unit::Plain;
    juce::StringArray choices;

    bool stepped() const { return unit == Unit::Choice || unit == Unit::Int || unit == Unit::Division
                                  || unit == Unit::Footage || unit == Unit::Bits; }
    float toNorm (float v) const;
    float fromNorm (float n) const;
    float snap (float v) const;
    juce::String format (float v) const;
};

class ParamLayout
{
public:
    int add (ParamInfo p);
    int cont (const juce::String& id, const juce::String& name, float mn, float mx, float def, Unit u = Unit::Plain,
              bool log = false, bool bipolar = false);
    int choice (const juce::String& id, const juce::String& name, const juce::StringArray& opts, int def = 0);
    int integer (const juce::String& id, const juce::String& name, int mn, int mx, int def, Unit u = Unit::Int);

    int size() const { return (int) infos.size(); }
    const ParamInfo& operator[] (int i) const { return infos[(size_t) i]; }
    int indexOf (const juce::String& id) const;   // -1 when missing

private:
    std::vector<ParamInfo> infos;
    std::unordered_map<std::string, int> byId;
};

struct MacroTarget { int param = -1; float lo = 0.0f, hi = 1.0f; };   // lo/hi are normalised
struct MacroMap
{
    juce::String name;
    std::array<MacroTarget, 8> targets {};
    int count = 0;
};

struct NoteEvent
{
    enum Type { NoteOn, NoteOff, AllNotesOff, Panic, Control } type = NoteOn;   // Control: `note` is the lane (ExprLane, 100+ a macro), `velocity` its value
    int note = 60;
    float velocity = 0.8f;   // 0..1
    int flags = 0;           // see StepFlags
    int sampleOffset = 0;
};
enum StepFlags { kAccent = 1, kSlide = 2, kTie = 4 };

/** General MIDI drum notes 35–59, as pad controllers send them, for a part order of
    kick, snare, clap, tom, closed hat, open hat, cymbal, perc (−1 when the note isn't one of them). */
inline int gmDrumRole (int note)
{
    switch (note)
    {
        case 35: case 36: return 0;                                   // kicks
        case 38: case 40: case 37: return note == 37 ? 7 : 1;          // snares (37 side stick → perc)
        case 39: return 2;                                            // clap
        case 41: case 43: case 45: case 47: case 48: case 50: return 3;   // toms
        case 42: case 44: return 4;                                   // closed and pedal hat
        case 46: return 5;                                            // open hat
        case 49: case 51: case 52: case 53: case 55: case 57: case 59: return 6;   // crashes, rides, china, splash
        case 54: case 56: case 58: return 7;                          // tambourine, cowbell, vibraslap
        // (from 60 up the C–G keys of each octave play the parts, so the computer keyboard keeps its layout)
        default: return -1;
    }
}

struct TransportInfo
{
    double bpm = 120.0;
    double beatPos = 0.0;   // quarter notes since start
    bool playing = false;
};


/** The processing tier, for low-end phones. Full is the instrument as designed. Eco halves the polyphony (VA 6 → 3,
    FM and Sampler 8 → 4, Bassline 4 → 2), sends each device's Reverb card into the master reverb instead of running
    its own plate, and lets a switched-away pattern ring for 1.5 s instead of 4 s. Set from the UI; read anywhere. */
enum class Quality { Full = 0, Eco = 1 };
std::atomic<int>& qualitySetting();
inline bool eco() { return qualitySetting().load (std::memory_order_relaxed) == (int) Quality::Eco; }
/** The VA and Bassline voices run their oscillators and filters at 2× (the desktop) or 1× (the phone: less CPU, a
    little more aliasing at the top). Set before the devices are prepared. */
std::atomic<bool>& oversampleVoices();
inline int voiceOs() { return oversampleVoices().load (std::memory_order_relaxed) ? 2 : 1; }
/** A device's polyphony under the current tier. */
inline int voiceCap (int full) { return eco() ? std::max (1, (full + 1) / 2) : full; }

class DeviceProcessor
{
public:
    explicit DeviceProcessor (DeviceType t) : devType (t) {}
    virtual ~DeviceProcessor() = default;

    DeviceType type() const { return devType; }
    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;
    virtual void handleEvent (const NoteEvent&) = 0;
    /** Adds stereo output into L/R (sub-block). */
    virtual void render (float* L, float* R, int n) = 0;
    virtual bool isSilent() const { return silentBlocks > 8 && ! anyVoiceActive(); }
    virtual bool anyVoiceActive() const = 0;
    virtual int laneCount() const { return 0; }        // drum devices: 8
    virtual juce::String laneName (int) const { return {}; }
    virtual int laneNote (int lane) const { return 36 + lane; }
    /** Which lane a played note triggers (−1: none). Drum devices override it; the recorder uses the same
        mapping, so whatever a key plays is what gets recorded. */
    virtual int laneForNote (int note) const { for (int l = 0; l < laneCount(); ++l) if (laneNote (l) == note) return l; return -1; }

    // ---- parameters
    ParamLayout& params() { return layout; }
    const ParamLayout& params() const { return layout; }
    float get (int i) const { return values[(size_t) i].load (std::memory_order_relaxed); }
    float getNorm (int i) const { return layout[i].toNorm (get (i)); }
    void set (int i, float v) { if (i >= 0 && i < layout.size()) values[(size_t) i].store (layout[i].snap (v), std::memory_order_relaxed); }
    void setNorm (int i, float n) { set (i, layout[i].fromNorm (n)); }
    float get (const juce::String& id) const { const int i = layout.indexOf (id); return i >= 0 ? get (i) : 0.0f; }
    void set (const juce::String& id, float v) { set (layout.indexOf (id), v); }
    void resetToDefaults();

    /** Per-step parameter lock (sequencer, audio thread). Cleared with clearLocks() on the next step. */
    /** The groovebox's own arp is on for this track (Play drawer): a device arp steps aside (no arp of an arp). */
    bool outerArp = false;
    void setLock (int param, float value) { if (param >= 0 && param < layout.size()) locks[(size_t) param] = layout[param].snap (value); }   // in range, on the steps
    void clearLocks() { std::fill (locks.begin(), locks.end(), std::numeric_limits<float>::quiet_NaN()); }
    /** Value the DSP uses: a lock overrides the stored value for one step, unless the control is held (being turned
        while recording: the hand wins over what the take wrote, so the sound follows it). */
    float pv (int i) const
    {
        const float l = locks[(size_t) i];
        return std::isnan (l) || held[(size_t) i].load (std::memory_order_relaxed) ? get (i) : l;
    }
    void setHeld (int i, bool h) { if (i >= 0 && i < (int) held.size()) held[(size_t) i].store (h, std::memory_order_relaxed); }
    /** A macro being turned while recording: its clip lane's playback leaves it alone. */
    std::array<std::atomic<bool>, 8> macroHeld {};
    /** What a control shows: the value playing now (a step's lock while it plays), else the stored one. */
    float shownNorm (int i) const { return layout[i].toNorm (pv (i)); }
    float shownMacro (int m) const;

    // ---- macros
    std::array<MacroMap, 8>& macros() { return macroMaps; }
    float getMacro (int m) const;
    void setMacro (int m, float v01);
    /** "Name:paramId:lo:hi,paramId:lo:hi|Name:..." for presets and device state. */
    juce::String macroString() const;
    void setMacroString (const juce::String&);

    // ---- state
    juce::String presetName { "Init" };
    virtual void save (juce::ValueTree& tree) const;
    virtual void load (const juce::ValueTree& tree);
    static constexpr int kVersion = 1;

    virtual void setTransport (const TransportInfo& t) { transport = t; }
    /** Send buses to the master Delay (bus 0) and Reverb (bus 1); set by the slot before render. */
    void setAuxBuffers (float* aL, float* aR, float* bL, float* bR) { aux[0] = aL; aux[1] = aR; aux[2] = bL; aux[3] = bR; }
    virtual void allNotesOff() { NoteEvent e; e.type = NoteEvent::AllNotesOff; handleEvent (e); }

    // ---- performance expression (MIDI input and a clip's lanes): every melodic instrument follows it
    enum ExprLane { kBend = 0, kModWheel = 1, kExpression = 11, kPressure = 128 };
    /** bend −1…1 (± bendRange semitones), mod wheel 0…1 (vibrato up to ±½ semitone), expression 0…1 (level),
        channel pressure 0…1 (deepens the vibrato like the mod wheel). */
    void setExpression (int lane, float value);
    void resetExpression() { bend = 0.0f; modWheel = 0.0f; expressionGain = 1.0f; pressure = 0.0f; }
    float bend = 0.0f, modWheel = 0.0f, expressionGain = 1.0f, pressure = 0.0f;
    float bendRange = 2.0f;

    /** Scales the Reverb card's wet level: the plugin's host "Reverb Return" (1 at its default 0.8). */
    float reverbScale = 1.0f;

    /** Notes currently sounding, for the plugin's MIDI activity row (bit per MIDI note). */
    std::array<std::atomic<uint32_t>, 4> heldNotes {};
    void markNote (int note, bool on);

protected:
    void finishLayout();   // allocates values and locks after the constructor filled the layout
    void macro (int m, const juce::String& name, std::initializer_list<std::tuple<const char*, float, float>> t);
    void trackSilence (const float* L, const float* R, int n);

    DeviceType devType;
    ParamLayout layout;
    std::vector<std::atomic<float>> values;
    std::vector<float> locks;
    std::vector<std::atomic<bool>> held;
    std::array<MacroMap, 8> macroMaps, defaultMacroMaps;
    TransportInfo transport;
    double sampleRate = 48000.0;
    int silentBlocks = 0;
    float* aux[4] {};
    /** Semitones every voice adds now (pitch bend and vibrato); advances the vibrato by n samples. Call once per render. */
    float expressionSemis (int n);
    double vibPhase = 0.0;
    void sendTo (int bus, int i, float l, float r) { if (aux[bus * 2] != nullptr) { aux[bus * 2][i] += l; aux[bus * 2 + 1][i] += r; } }
    /** The Reverb card: the device's own plate, or in Eco a send into the master reverb (bus 1) at the same amount. */
    template <typename Verb>
    void reverbCard (Verb& v, const float* inL, const float* inR, float* L, float* R, int n, float mix, float size, float tone, float pre)
    {
        if (eco() && aux[2] != nullptr)
        {
            v.reset();   // its tail stops; the master's plays the space
            for (int i = 0; i < n; ++i) { aux[2][i] += inL[i] * mix; aux[3][i] += inR[i] * mix; }
            return;
        }
        v.process (inL, inR, L, R, n, mix, size, tone, pre);
    }
};

std::unique_ptr<DeviceProcessor> createDevice (DeviceType);

// ---------------------------------------------------------------------------------------------
/** Voice allocation shared by the poly devices: reuse the same note, then a free voice, then the
    oldest released voice, then the quietest. A stolen voice fades out over 3 ms before restarting. */
struct VoiceSlot
{
    int note = -1;
    bool held = false, active = false;
    uint32_t age = 0;
    float level = 0.0f;
    // steal crossfade
    int stealCountdown = 0, stealLen = 1;
    int pendingNote = -1;
    float pendingVel = 0.0f;
    int pendingFlags = 0;
};

template <size_t N>
int allocateVoice (std::array<VoiceSlot, N>& v, int note, int maxVoices, uint32_t& clock)
{
    const int n = std::min ((int) N, maxVoices);
    for (int i = 0; i < n; ++i) if (v[(size_t) i].active && v[(size_t) i].note == note && v[(size_t) i].stealCountdown == 0) return i;
    for (int i = 0; i < n; ++i) if (! v[(size_t) i].active) return i;
    int best = -1; uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < n; ++i) if (! v[(size_t) i].held && v[(size_t) i].age < oldest) { oldest = v[(size_t) i].age; best = i; }
    if (best >= 0) return best;
    float q = 1.0e9f;
    for (int i = 0; i < n; ++i) if (v[(size_t) i].level < q) { q = v[(size_t) i].level; best = i; }
    (void) clock;
    return best < 0 ? 0 : best;
}

/** Mono note priority: last held key wins, released keys fall back to the previous held one. */
struct HeldStack
{
    std::array<int, 16> notes {};
    std::array<float, 16> vels {};
    int count = 0;
    void push (int n, float v)
    {
        remove (n);
        if (count == 16) { std::move (notes.begin() + 1, notes.end(), notes.begin()); std::move (vels.begin() + 1, vels.end(), vels.begin()); --count; }
        notes[(size_t) count] = n; vels[(size_t) count] = v; ++count;
    }
    void remove (int n)
    {
        for (int i = 0; i < count; ++i)
            if (notes[(size_t) i] == n)
            {
                for (int j = i; j < count - 1; ++j) { notes[(size_t) j] = notes[(size_t) j + 1]; vels[(size_t) j] = vels[(size_t) j + 1]; }
                --count; return;
            }
    }
    int top() const { return count > 0 ? notes[(size_t) count - 1] : -1; }
    float topVel() const { return count > 0 ? vels[(size_t) count - 1] : 0.0f; }
    void clear() { count = 0; }
};
} // namespace mb
