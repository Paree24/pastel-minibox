#pragma once
#include "Widgets.h"
#include "PianoRoll.h"

namespace mb::ui
{
/** Seq view (desktop.py seq_screen): bar strip, the 4 tracks of the current bar, and the detail editor
    of the selected track (piano roll and lanes, or the drum lane grid). */
class SeqView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SeqView (AppContext&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void resized() override;
    void refresh();
    /** Piano-roll editing keys (Delete, clipboard, nudge…); true when used. */
    bool keyCommand (const juce::KeyPress&);
    bool hasNoteSelection() const { return roll.isVisible() && roll.hasSelection(); }
    /** A step sequence shown as Steps (true) or as a piano roll. */
    void setStepsView (bool steps) { stepsView = steps; refresh(); repaint(); }
    /** The clip lane under the notes: −1 velocity, else a controller lane (tests, screenshots). */
    void showLane (int lane) { clipLane = lane; repaint(); }

private:
    struct Target { enum Kind { None, BarLess, BarMore, Res, Bar, RowSelect, RowStep, RowMute, RowSolo, ViewSwitch, Convert, Quantize, Overdub, LockParam,
                                Follow, Tool, LaneVel, LaneA, LaneB, LaneCond, StepNote, StepLane, ClipVel, ClipLaneMenu, DrumCell, DrumLaneView, DrumLane } kind = None;
                    int a = -1, b = -1; };
    Target locate (juce::Point<float>) const;
    void applyDrag (const Target&, juce::Point<float>, bool rightButton);
    juce::Rectangle<float> rollArea() const;
    juce::Rectangle<float> laneArea (int lane) const;
    // the Steps view of a melodic step sequence (mockup 02_seq): note cells and one lane per step setting
    juce::Rectangle<float> notesArea() const;
    juce::Rectangle<float> stepLaneArea (int lane) const;
    static constexpr int kStepLanes = 6;   // velocity, gate, accent / probability, slide / ratchet, condition, lock
    void paintStepsView (juce::Graphics&, Track&, DeviceProcessor&, juce::Colour, int first, int play, int cursor);
    void paintClipLane (juce::Graphics&, Track&, juce::Colour);
    juce::Rectangle<float> clipLaneArea() const;
    /** Is the selected melodic track shown as a piano roll (a clip, or steps viewed as a roll)? */
    bool rollView() const;
    void convertToPianoRoll();
    void clipVelocityAt (juce::Point<float>);
    /** The clip's lane: −1 velocity, else a controller (CCPoint lanes). */
    int clipLane = -1;
    int laneLastTick = -1;
    void clipLaneDraw (juce::Point<float>, bool erase, bool start);
    void showLaneMenu();
    juce::String laneName (int lane) const;
    juce::Rectangle<float> stepCell (juce::Rectangle<float> area, int i, int n, float gap) const;
    int spb() const;
    Track& track (int t);

    AppContext& ctx;
    bool stepsView = true;         // a step sequence: Steps (true) or viewed as a piano roll
    juce::Rectangle<float> convertR, lockPickR;
    Step noteStepAtDown;           // pitch drag on a note cell
    float noteDownY = 0;
    bool noteWasOn = false, noteMoved = false;
    int lastKind = -1;
    int lockParam = -1;
    int drumLaneView = 0;          // Vel, Prob, Ratchet, Micro
    int previewNote = -1;
    int scrolledFor = -1;
    juce::String sig;
    Target dragTarget;
    Feedback fb { *this };
    PianoRoll roll;
    enum ToolId { TSelect, TDraw, TPaint, TErase, TGrid, TLength, TQuantize, TFunctions, TScale, TFold, TGhosts, TZoomOut, TZoomIn, TFit, TLoop, TUndo, TRedo, TTransDownOct, TTransDown, TTransUp, TTransUpOct };
    std::vector<std::pair<juce::Rectangle<float>, int>> toolZones;
    juce::Rectangle<float> followR;
    void paintToolbar (juce::Graphics&, juce::Colour);
    void toolAction (int id);
    void showConditionMenu (int track, int lane, int step);
    void showBarMenu (int bar);
    struct BarClip { int track = 0; std::vector<Step> steps; std::array<std::vector<DrumStep>, kLanes> lanes; };
    std::vector<BarClip> barClip;
    bool paintOn = true;          // a press on a step decides on / off for the cells a drag paints
    void paintStep (int track, int step);
    void paintDrum (int lane, int step);
};
} // namespace mb::ui
