#pragma once
#include "EditorSpecs.h"

namespace mb::ui
{
/** One section card: title, optional title toggle, choice pill, visual and up to 6 knobs (desktop.py draw_section). */
class SectionCard : public juce::Component
{
public:
    SectionCard (AppContext&, DeviceProcessor&, const SectionSpec&, juce::Colour, int pattern, int track,
                 std::function<void (int, float)> onParamChange);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
private:
    AppContext& ctx;
    DeviceProcessor& dev;
    SectionSpec spec;
    juce::Colour colour;
    std::vector<std::unique_ptr<KnobView>> knobs;
    std::unique_ptr<ChoiceView> choice;
    std::unique_ptr<PaintedButton> toggle;
    std::unique_ptr<VisualView> visual;
};

/** Sections laid out in rows with width weights; rebuilt when the layout depends on a changed value. */
class EditorBody : public juce::Component
{
public:
    EditorBody (AppContext&, juce::Colour);
    void setDevice (DeviceProcessor*, int pattern, int track);
    void resized() override;
    void refresh();
    std::function<void (int, float)> onParamChange;   // plugin: host notification
private:
    juce::String layoutKey() const;
    void rebuild();
    AppContext& ctx;
    juce::Colour colour;
    DeviceProcessor* dev = nullptr;
    int pattern = -1, track = -1;
    EditorSpec spec;
    juce::String builtKey;
    std::vector<std::unique_ptr<SectionCard>> cards;
};

/** Preset actions shared by the standalone header and the plugin header. */
namespace presetActions
{
void step (AppContext&, DeviceProcessor&, int direction);
void toggleCompare (AppContext&, DeviceProcessor&);
void showDeviceMenu (AppContext&, DeviceProcessor&, juce::Component* anchor, int pattern, int track);
}

/** Standalone editor header: back, track pill, track dots, engine switch, preset browser, save, Compare, ⋯. */
class EditorHeader : public juce::Component
{
public:
    explicit EditorHeader (AppContext&);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();
private:
    AppContext& ctx;
    juce::String shownKey;
    juce::Rectangle<float> backR, pillR, presetR, prevR, nextR, saveR, compareR, moreR, engineR;
    std::array<juce::Rectangle<float>, 4> dotR;
    Feedback fb { *this };
};

/** Step strip under a standalone editor: the focused track's current bar. */
class StepStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StepStrip (AppContext&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void refresh();
private:
    struct Hit { int bar = -1, step = -1, lane = -1; bool badge = false; };
    Hit locate (juce::Point<float>) const;
    AppContext& ctx;
    juce::String sig;
    int dragStep = -1;
    Feedback fb { *this };
};

/** Standalone editor page. */
class EditorView : public juce::Component
{
public:
    explicit EditorView (AppContext&);
    void showTrack (int track);
    void resized() override;
    void refresh();
    void paint (juce::Graphics&) override;
private:
    AppContext& ctx;
    EditorHeader header;
    std::unique_ptr<EditorBody> body;
    StepStrip strip;
    DeviceProcessor* shown = nullptr;
};
} // namespace mb::ui
