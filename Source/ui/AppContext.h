#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "engine/Engine.h"
#include <functional>
#include <unordered_map>

namespace mb { class MasterFx; class MidiMap; }

namespace mb::ui
{
/** Each device's colour, as in its editor mockup (03–10): the plugin shows every device in it. */
inline juce::Colour deviceColour (DeviceType t)
{
    switch (t)
    {
        case DeviceType::Drums: case DeviceType::DrumSampler: return juce::Colour (0xff9EB9F3);
        case DeviceType::Acid: case DeviceType::Mono: case DeviceType::Wavetable: return juce::Colour (0xff8FD0C2);
        case DeviceType::Poly: return juce::Colour (0xffE8B86A);
        default: return juce::Colour (0xffE9A3B8);   // FM, Sampler
    }
}

/** State shared by the views: the engine, what is focused, and the callbacks between views. */
struct AppContext
{
    explicit AppContext (Engine& e, bool isStandalone) : engine (e), standalone (isStandalone) {}

    Engine& engine;
    const bool standalone;
    /** The groovebox's master and performance effects (null in the plugin) and the drawer that edits them. */
    MasterFx* masterFx = nullptr;
    std::function<void (bool pads, int index)> onMasterFx;
    std::function<void (int track)> onTrackFx;   // a track's own effects in the drawer (toggles when already there)
    /** The controller map (null in the plugin): FX keys live there. */
    MidiMap* midiMap = nullptr;
    /** FX keys on / off (releases the effects the keys held). */
    std::function<void (bool)> setFxKeys;   // opens the drawer on a chain slot or a pad (toggles when already there)

    int focusPattern() const { return engine.focusPattern.load(); }
    int focusTrack() const { return engine.focusTrack.load(); }
    DeviceProcessor* focusDevice() const { return engine.device (focusPattern(), focusTrack()); }

    /** Step chosen as the parameter-lock target (shift-click a step); -1 when none. */
    int lockStep = -1;
    /** Bar shown by the step strip and the Seq view. */
    int viewBar = 0;
    /** Drum lane editor view: 0 Vel, 1 Prob, 2 Ratchet. */
    int laneView = 0;
    /** Last note played on each track, used when a step is switched on by clicking. */
    std::array<int, kTracks> lastNote { 36, 36, 60, 60 };
    /** The length (gate) of the note last clicked or resized: new notes on that track get it. */
    std::array<int, kTracks> lastGate { 96, 96, 96, 96 };
    std::array<int, kTracks> lastVel { 100, 100, 100, 100 };

    /** Sub-selection per device: drum part, FM operator, sampler zone or drum-sampler pad. */
    int selection (const DeviceProcessor* d) const { auto it = sel.find (d); return it == sel.end() ? 0 : it->second; }
    void select (const DeviceProcessor* d, int i) { sel[d] = i; if (onSelectionChanged) onSelectionChanged(); }

    /** Reference state for Compare: the state at the last preset load or save. */
    std::unordered_map<const DeviceProcessor*, juce::ValueTree> reference;
    std::unordered_map<const DeviceProcessor*, juce::ValueTree> comparing;   // edited state while comparing
    void setReference (const DeviceProcessor& d)
    {
        juce::ValueTree t ("Device");
        d.save (t);
        reference[&d] = t;
        comparing.erase (&d);
    }

    bool dirty = false;
    void markDirty() { if (! dirty) { dirty = true; if (onDirtyChanged) onDirtyChanged(); } }

    // ---- undo / redo of sequence edits: a snapshot of a pattern's tracks (and length) before each edit
    struct PatternSnap { int pattern = 0; std::array<Track, kTracks> tracks; int lengthBars = 1, stepRes = 1; };
    // an undo step holds the patterns an edit touched (one, or all of them for a scale change)
    using UndoStep = std::vector<PatternSnap>;
    std::vector<UndoStep> undoStack, redoStack;
    PatternSnap capture (int pattern) const
    {
        const auto& pt = engine.project().patterns[(size_t) pattern];
        PatternSnap snap;
        snap.pattern = pattern; snap.lengthBars = pt.lengthBars; snap.stepRes = pt.stepRes;
        for (int t = 0; t < kTracks; ++t) snap.tracks[(size_t) t] = pt.slots[(size_t) t].track;
        return snap;
    }
    void restore (const PatternSnap& snap)
    {
        const Engine::ScopedEdit edit (engine);
        auto& pt = engine.project().patterns[(size_t) snap.pattern];
        pt.lengthBars = snap.lengthBars; pt.stepRes = snap.stepRes;
        for (int t = 0; t < kTracks; ++t) pt.slots[(size_t) t].track = snap.tracks[(size_t) t];
    }
    void pushUndo (UndoStep step)
    {
        undoStack.push_back (std::move (step));
        if (undoStack.size() > 64) undoStack.erase (undoStack.begin());
        redoStack.clear();
        markDirty();
    }
    /** Call before changing a pattern's notes, steps, lanes or length (once per gesture). */
    void snapshot (int pattern) { pushUndo ({ capture (pattern) }); }
    /** Before an edit of every pattern (a scale change moving the notes). */
    void snapshotAll() { UndoStep s; for (int p = 0; p < kPatterns; ++p) s.push_back (capture (p)); pushUndo (std::move (s)); }
    bool undo() { return swapStep (undoStack, redoStack); }
    bool redo() { return swapStep (redoStack, undoStack); }
    bool swapStep (std::vector<UndoStep>& from, std::vector<UndoStep>& to)
    {
        if (from.empty()) return false;
        UndoStep back;
        for (auto& s : from.back()) back.push_back (capture (s.pattern));
        to.push_back (std::move (back));
        for (auto& s : from.back()) restore (s);
        from.pop_back();
        markDirty();
        if (onSequenceRestored) onSequenceRestored();
        return true;
    }
    /** The view follows the playing bar. */
    bool followPlayhead = true;
    /** Piano-roll toolbar settings (docs/sequencer.md). */
    struct RollSettings
    {
        int tool = 0;               // 0 Select, 1 Draw, 2 Paint, 3 Erase
        int lengthMode = 0;         // 0 last used; 1… fixed (PianoRoll::lengthChoices)
        bool scale = true, fold = true, ghosts = false;   // the scale's rows shown, folded to them (as the phone)
        int viewSteps = 0;          // steps in view (time zoom); 0 = a bar
        int start = 0;              // the first step in view
        int rows = 18;              // rows in view (pitch zoom)
        int grid = 3;               // PianoRoll::gridNames(): 0 Adaptive, 1 1/64 … 3 1/16 … 7 1 bar
        bool triplet = false, dotted = false, snap = true;
    } rollSettings;

    std::function<void()> onSelectionChanged, onDirtyChanged, onDevicesChanged, onSequenceRestored;
    std::function<void (int track)> openEditor;
    std::function<void()> closeEditor;
    std::function<void (int track, juce::Component* anchor)> showDevicePicker;
    std::function<void (DeviceProcessor*, juce::Component* anchor)> showPresetBrowser;

private:
    std::unordered_map<const DeviceProcessor*, int> sel;
};
} // namespace mb::ui
