#pragma once
#include "Widgets.h"

namespace mb::ui
{
/** The Seq view's piano roll (docs/sequencer.md): a ruler, a keyboard and the note grid of the steps in view,
    a scroll bar, the Select and Draw tools with grid snapping (triplets too), the edit functions behind the
    toolbar's Quantize and Functions menus, zoom in time and pitch, scale highlighting, folding, ghost notes of
    the other tracks, clipboard and undo. It edits either kind of sequence (docs/sequencer.md): a piano-roll clip
    (free notes; a NoteRef's `step` is then the clip note's id) or a step sequence (notes on steps, each with its
    own ±½-step offset). Positions are clip ticks (kPPQ a quarter) either way. */
class PianoRoll : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PianoRoll (AppContext&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    /** Editing and view keys; returns true when used. */
    bool keyCommand (const juce::KeyPress&);

    /** A note: its step and pitch (steps), or its clip note id and pitch (piano roll). */
    struct NoteRef { int step = -1, note = -1; bool operator== (const NoteRef& o) const { return step == o.step && note == o.note; } };
    bool clipMode() const { return track().pianoRoll; }
    const std::vector<NoteRef>& selection() const { return sel; }
    bool hasSelection() const { return ! sel.empty(); }
    void clearSelection() { sel.clear(); repaint(); }
    void validateSelection();
    void appendSignature (juce::String&) const;

    // ---- view: the steps in view (time zoom), the rows (pitch zoom); the bar strip and lanes follow it
    int visibleSteps() const;
    int firstStep() const;
    /** Keeps the view and ctx.viewBar together (a bar picked elsewhere scrolls the roll to it). */
    void syncView();
    void setFirstStep (int step);
    void zoomTime (double factor, float aroundX = -1.0f);
    void zoomToFit();
    void zoomRows (int delta);
    /** Loop playback on / off (a range: Shift-drag in the ruler; the bars in view when none is set). */
    void toggleLoop();
    int rows() const { return std::clamp (ctx.rollSettings.rows, 8, 48); }
    /** The keyboard cursor's step (the clip lanes edit there by keys). */
    int cursorStep() const { return keyCursor.step; }
    void moveCursorStep (int d) { keyCursor.step = std::clamp (keyCursor.step + d, 0, std::max (0, totalSteps() - 1)); if (keyCursor.step < firstStep() || keyCursor.step >= firstStep() + visibleSteps()) setFirstStep (keyCursor.step - visibleSteps() / 2); repaint(); }
    /** Where a clip tick is drawn, and the tick under an x (component coordinates); the grid line at or before it. */
    float xOfTick (double tick) const { return xForTick (tick); }
    int tickOfX (float x) const { return tickAt (x); }
    int gridFloor (int tick) const { return snapFloor (tick, false); }
    /** The centre of a cell in component coordinates (tests). */
    juce::Point<float> pointFor (double step, int note) const;

    // ---- grid
    /** The grid in ticks (the toolbar's Grid, Adaptive following the zoom; triplets ⅔). */
    int gridTicks() const;
    static juce::StringArray gridNames();
    juce::String gridLabel() const;

    // ---- toolbar functions (apply to the selection, or to every note when nothing is selected)
    void selectAll();
    void invertSelection();
    void deleteSelection();
    void copySelection (bool cut);
    void paste();
    void duplicate();
    void transpose (int semis);
    /** The toolbar's transpose: an octave, or a step (a scale step when scale lock is on). */
    void transposeBy (int dir, bool octave);
    void toggleMute();
    /** The selection a grid unit longer / shorter, its velocity ±8, the loop around it. */
    void lengthBy (int dir);
    void velocityBy (int dir);
    void loopSelection();
    /** The keyboard cursor's step column is shown (the clip lanes under the roll edit there by keys). */
    bool showCursorColumn = false;
    /** Locked notes stay as they are: no moving, resizing, deleting or functions until unlocked (Ctrl+L). */
    void toggleLock();
    // chords: notes that start together
    void selectChords();
    void chordInvert (bool up);
    void chordSpread();
    void chordClose();
    void chordAddBass();
    /** Zoom to the notes (or the selection) in time and pitch. */
    void fitNotes (bool selectionOnly);
    void quantize (float amount, bool lengths);
    void humanize();
    void legato();
    void setLengthToNew();
    void stretch (float factor);
    void reverse();
    void invertPitch();
    void fitToScale();
    void chop();
    void glue();
    void strum();
    void arpeggiate();   // the chords with the track's Arp settings
    void velocityRandomize (float amount = 0.16f);   // each velocity moves up to that fraction up or down
    void velocityRamp (bool up);
    void velocitySet (int v);
    void rotate (int dir);
    void insertStep (bool insert);
    /** The new-note length choices of the toolbar (index 0 = last used). */
    static juce::StringArray lengthChoiceNames (double stepQuarters);
    int newNoteLength() const;

    int topNote = 72;
    static constexpr int kRows = 18;
    static constexpr float kKeysW = 64.0f, kGap = 8.0f, kRuler = 20.0f, kScroll = 10.0f;

private:
    enum class Zone { None, Body, Start, End };
    struct Hit { NoteRef ref; Zone zone = Zone::None; };
    enum class Drag { None, Create, Move, ResizeEnd, ResizeStart, Marquee, Keys, Ruler, Scroll, Paint, Erase, LoopSet };
    /** A note taken out of the track: position and length in ticks, and its step's settings (kept if it lands alone). */
    struct NoteData
    {
        int tick, note, vel, len; bool muted;
        uint8_t flags = 0, prob = 100, ratchet = 1, cond = 0;
        bool tie = false;   // a step with slide / tie (drawn a step long)
        bool locked = false;
    };

    Track& track();
    const Track& track() const { return const_cast<PianoRoll*> (this)->track(); }
    int pattern() const { return ctx.focusPattern(); }
    int stepsPerBar() const;
    int totalSteps() const;
    int stepT() const { return ctx.engine.project().stepTicks (pattern()); }
    int totalTicks() const { return totalSteps() * stepT(); }
    juce::Rectangle<float> grid() const { return { kKeysW + kGap, kRuler, (float) getWidth() - kKeysW - kGap, (float) getHeight() - kRuler - kScroll }; }
    juce::Rectangle<float> scrollTrack() const { const auto g = grid(); return { g.getX(), g.getBottom() + 3, g.getWidth(), 6 }; }
    juce::Rectangle<float> scrollThumb() const;
    float cellW() const { return grid().getWidth() / (float) visibleSteps(); }
    float xForTick (double tick) const { return grid().getX() + (float) ((tick / stepT() - firstStep()) * cellW()); }
    int tickAt (float x) const { return (int) std::lround ((firstStep() + (x - grid().getX()) / cellW()) * stepT()); }
    int snapNearest (int tick, bool free) const;
    int snapFloor (int tick, bool free) const;
    // rows: plain (every pitch from topNote down) or folded (only the pitches in use / in the scale)
    std::vector<int> rowNotes;   // descending
    int rowOffset = 0;
    bool wasFolded = false;
    void buildRows();
    int visibleRows() const { return std::max (1, std::min (rows(), (int) rowNotes.size() - rowOffset)); }
    float rowH() const { return grid().getHeight() / (float) (ctx.rollSettings.fold ? visibleRows() : rows()); }
    int rowForNote (int note) const;
    int noteForRow (int row) const;
    juce::Rectangle<float> noteRect (const NoteData&) const;
    /** Every note of a track (`fn (ref, data)`), and those that reach into the view (`fn (ref, data, rect)`). */
    template <class Fn> void forEachNote (const Track&, Fn&& fn) const;
    template <class Fn> void forEachVisible (const Track&, Fn&& fn) const;
    void removeRef (Track&, const NoteRef&);
    int lenToSteps128 (int lenT) const { return std::clamp ((int) std::lround (lenT * 128.0 / stepT()), 1, kMaxNoteLen); }
    Hit noteHit (juce::Point<float>) const;
    int stepAt (float x) const;
    int pitchAt (float y) const;
    bool isSelected (int step, int note) const;
    void preview (int note);
    void stopPreview();
    void rememberFrom (const NoteRef&);
    /** dPitch: semitones, or rows when the rows skip pitches (scale lock, fold): a scale step at a time. */
    void applyMove (int dTicks, int dPitch, bool copy, bool semitones = false);
    bool rowsSkipPitches() const { return ctx.rollSettings.fold || ctx.engine.project().scale.lock; }
    void applyResize (int dTicks, bool end, bool free);
    void nudge (int dTicks, int dPitch);
    void autoScroll (float x);
    void addNoteAt (int tick, int note);
    void paintRuler (juce::Graphics&);
    bool noteData (const Track&, const NoteRef&, NoteData&) const;
    /** Writes a note at its tick: on the nearest step that can hold it (each step holds a pitch once). */
    NoteRef place (Track&, const NoteData&);
    /** The notes a function works on (the selection, or all notes), taken out of the track; `put` writes
        the transformed list back and selects it. Both are one undo step. */
    std::vector<NoteData> takeTargets();
    void put (const std::vector<NoteData>&);
    std::vector<NoteRef> targetRefs() const;

    AppContext& ctx;
    Feedback fb { *this };
    std::vector<NoteRef> sel, selBefore;
    Drag drag = Drag::None;
    Track before;                       // the track when the gesture started (drags are applied to a copy of it)
    juce::Point<float> anchor;
    int anchorTick = 0, anchorPitch = 60, refStart = 0, refEnd = 0, viewAtDown = 0, visAtDown = 16;
    double stepAtDown = 0.0;
    juce::Rectangle<float> marquee;
    NoteRef createdRef, dragRef;
    bool copyDrag = false, moved = false, shiftToggle = false, wasSelected = false;
    NoteRef toggleRef;
    int previewNote = -1;
    int lastViewBar = -1;
    int paintTick = -1, paintPitch = -1, loopAnchor = 0;   // Paint stroke, loop range drag
    bool erased = false;
    juce::Point<float> lastErase;
    void eraseAt (juce::Point<float>);
    void paintAt (juce::Point<float>, bool free);
    void paintNote (int tick, int pitch, bool free);
    juce::Random rnd { 1234 };
    NoteRef keyCursor { 0, 60 };        // the keyboard cursor cell (shown while the roll has keyboard focus)
    bool cursorKey (const juce::KeyPress&);
    static std::vector<NoteData> clipboard;   // ticks relative to the first copied note
};
} // namespace mb::ui
