#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>
#include <atomic>

namespace mb
{
/** One MIDI learn mapping (docs/midi-learn.md): a controller message (kind, channel, number, port) and what it
    controls. Targets are strings resolved by the UI: `macro:<track|sel>:<0-7>`, `level:<track|sel>`, `pan:…`,
    `delay:…`, `reverb:…`, `param:<track|sel>:<param id>`, `master:<volume|delay|reverb|glue|limit>`, `tempo`,
    `action:<name>`. */
struct MidiMapping
{
    enum class Kind { CC, Note, PitchBend };
    enum class Mode { Absolute, RelativeTwos, RelativeOffset, RelativeSign, Trigger, Toggle, Momentary };
    Kind kind = Kind::CC;
    int channel = 0;            // 1–16; 0 = any
    int number = 0;             // CC or note number
    juce::String port;          // matched as a part of the input's name (empty = any input)
    juce::String target;
    juce::String label;         // the hardware control's name ("Knob 1"), shown in Settings ▸ MIDI controller
    Mode mode = Mode::Absolute;
    float lo = 0.0f, hi = 1.0f; // range of the target the control covers
    bool pickup = false;        // absolute: wait until the control passes the current value
    // runtime (message thread)
    float lastIn = -1.0f;
    bool picked = false, held = false;

    bool matches (const juce::String& portName, const juce::MidiMessage&) const;
    bool sameSource (const MidiMapping& o) const { return kind == o.kind && channel == o.channel && number == o.number && port == o.port; }
    bool isButtonMode() const { return mode == Mode::Trigger || mode == Mode::Toggle || mode == Mode::Momentary; }
    bool isRelative() const { return mode == Mode::RelativeTwos || mode == Mode::RelativeOffset || mode == Mode::RelativeSign; }
    juce::String messageName() const;
    static juce::String modeName (Mode);
};

/** What a mapped message asks of its target (message thread). */
struct MidiControl
{
    enum class Type { Set, Delta, Press, Release };
    Type type = Type::Set;
    float value = 0.0f;         // Set: target value in lo…hi (0–1 normalised); Delta: change in normalised units
};

/** The controller map and learn mode. MIDI inputs call `filter` (any thread): mapped messages and messages
    while a control is armed are taken and queued, everything else plays the instruments as before. The
    message thread calls `dispatch`, which applies queued messages to their targets or learns them. */
class MidiMap : private juce::AsyncUpdater
{
public:
    MidiMap();
    ~MidiMap() override;

    // ---- MIDI thread
    /** True when the message was used by a mapping or by learning (it must not reach the instruments). */
    bool filter (const juce::String& portName, const juce::MidiMessage&);

    // ---- message thread
    std::vector<MidiMapping> mappings;
    juce::String name { "Custom" };
    /** Bumped when the mappings, learn mode or the armed target change (not for the last message). */
    int revision = 0;
    /** Learn mode and the armed target (empty when nothing is armed). */
    bool learning() const { return learnOn.load(); }
    void setLearning (bool on);
    const juce::String& armedTarget() const { return armed; }
    void arm (const juce::String& target);
    /** The last message received from any input (shown by the learn drawer). */
    juce::String lastReceived;
    /** Applies a mapping's message to its target: `apply (target, control, mapping)`. */
    std::function<void (const juce::String&, const MidiControl&, MidiMapping&)> apply;
    std::function<void()> onChanged;   // mappings, learning or the last message changed
    /** Handles the queued messages now (normally posted to the message thread automatically). */
    void dispatch();
    /** Handles one message as if it came from `port` (tests and the plugin's host input). */
    void handle (const juce::String& port, const juce::MidiMessage&);

    void add (MidiMapping);
    void remove (int index);
    void removeTarget (const juce::String& target);
    void clear();
    juce::String sourceFor (const juce::String& target) const;   // e.g. "CC 74", or empty

    juce::var toVar() const;
    bool fromVar (const juce::var&);
    bool save (const juce::File&) const;
    bool load (const juce::File&);
    /** The map in use, saved automatically: <user app data>/Pastel Minibox/current.mbmidi */
    static juce::File currentFile();
    static juce::File& currentFileOverride();   // tests: keep the user's map untouched
    /** Shipped maps (Factory/midimaps). */
    static juce::Array<juce::File> factoryMaps();
    void changed (bool persist = true);

    // ---- FX keys (docs/master-fx.md): while on, 8 notes from any input, from fxKeysBase up, hold the performance
    // effects instead of playing (for controllers whose pads send the same notes as the keys). Saved with the map.
    std::atomic<bool> fxKeysOn { false };
    std::atomic<int> fxKeysBase { 36 };
    /** The next note-on from any input becomes the first FX key (and turns FX keys on). */
    std::atomic<bool> fxKeysLearning { false };
    /** MIDI thread: an FX key went down / up (index 0–7). Null (the plugin): FX keys are off. */
    std::function<void (int index, bool down)> onFxKey;
    void setFxKeys (bool on);   // message thread (saved)

    /** Converts a CC / note / pitch-bend message for a mapping into what it asks of the target. */
    static bool control (MidiMapping&, const juce::MidiMessage&, MidiControl&);

private:
    void handleAsyncUpdate() override { if (fxKeysChanged.exchange (false)) changed(); dispatch(); }
    std::atomic<bool> fxKeysChanged { false };
    void publish();   // copies the match keys for the MIDI thread
    void learn (const juce::String& port, const juce::MidiMessage&);

    struct Key { MidiMapping::Kind kind; int channel, number; juce::String port; };
    juce::SpinLock keyLock;
    std::vector<Key> keys;
    std::atomic<bool> learnOn { false }, armedFlag { false }, monitor { false };
    juce::String armed;

    struct Queued { juce::MidiMessage msg; char port[96]; };
    static constexpr int kQueue = 512;
    juce::AbstractFifo fifo { kQueue };
    std::vector<Queued> queue;
    // learn: the messages received for the armed target, to tell absolute from relative encoders
    std::vector<int> learnValues;
    juce::String learnPort;
    int learnKey = -1;
};
} // namespace mb
