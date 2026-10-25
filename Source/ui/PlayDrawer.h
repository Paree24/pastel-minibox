#pragma once
#include "Widgets.h"

namespace mb::ui
{
/** The Play drawer (desktop.py play_drawer, mockup 14_play): how live input plays and records.
    Scale (the project's key: lock, root, scale, snap, white keys), Chord and Arp (for the input track).
    Recording's settings have their own popover by the record buttons (TopBar::showRecording, mockup 18_recording). */
class PlayDrawer : public juce::Component
{
public:
    explicit PlayDrawer (AppContext&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();
    std::function<void()> onClose;

private:
    PlaySettings& play();
    juce::Rectangle<float> box (int i) const;
    void chooseScale();
    /** The project's scale changed from `before`: every melodic note moves with it (one undo step). */
    void moveNotes (const ScaleSettings& before);
    void chooseChord (int degree);
    void chooseRate();
    juce::String chordName (int degree, int& count) const;

    AppContext& ctx;
    std::vector<std::unique_ptr<KnobView>> knobs;   // strum, octaves, gate, swing
    struct Zone { juce::Rectangle<float> r; juce::String id; int a = 0; };
    std::vector<Zone> zones;
    juce::String sig;
    juce::Rectangle<float> menuArea;
    Feedback fb { *this };
};
} // namespace mb::ui
