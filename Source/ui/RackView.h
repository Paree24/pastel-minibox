#pragma once
#include "Visuals.h"

namespace mb::ui
{
/** One slot of the current pattern: identity, preset, M/S, 8 macros, visual, level, pan, sends, open-editor. */
class SlotStrip : public juce::Component
{
public:
    SlotStrip (AppContext&, int track);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();
private:
    void rebuild();
    AppContext& ctx;
    const int track;
    DeviceProcessor* built = nullptr;
    int builtPattern = -1;
    std::vector<std::unique_ptr<KnobView>> macros, mixKnobs;
    std::unique_ptr<FaderView> fader;
    juce::String sig;
    juce::Rectangle<float> nameR, presetR, prevR, nextR, muteR, soloR, openR, visR, plusR, fxR;
    Feedback fb { *this };
    /** Any press on the strip or its knobs selects this instrument (what the keyboard and MIDI play). */
    struct Selector : juce::MouseListener
    {
        explicit Selector (SlotStrip& s) : strip (s) { strip.addMouseListener (this, true); }
        ~Selector() override { strip.removeMouseListener (this); }
        void mouseDown (const juce::MouseEvent&) override { strip.select(); }
        SlotStrip& strip;
    } selector { *this };
    void select();
};

class MasterRow : public juce::Component
{
public:
    explicit MasterRow (AppContext&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();
    /** A performance pad's area (tests). */
    juce::Rectangle<float> padArea (int pad) const { return padR[(size_t) pad]; }
private:
    AppContext& ctx;
    std::vector<std::unique_ptr<KnobView>> knobs;
    std::unique_ptr<FaderView> volume;
    juce::Rectangle<float> delayR, reverbR, effectsR, fxKeysR, repeatR;
    std::array<juce::Rectangle<float>, 8> padR;
    int pressedPad = -1;
    float extra = 0.0f;   // the width beyond the 16:10 layout
    juce::String shown;
    float divProxy = 9.0f;
    Feedback fb { *this };
};

class RackView : public juce::Component
{
public:
    explicit RackView (AppContext&);
    void resized() override;
    juce::Component* strip (int track) { return slots[(size_t) track].get(); }
    void refresh();
private:
    AppContext& ctx;
    std::vector<std::unique_ptr<SlotStrip>> slots;
    juce::Viewport viewport;       // the eight strips scroll, four in view; the master row stays
    juce::Component strips;
    int shownFocus = -1;
    MasterRow master;
};

/** Small floating panel of knobs (master delay / reverb details). */
void showKnobPopover (juce::Component* anchor, juce::Rectangle<int> area, std::vector<std::unique_ptr<KnobView>> knobs);
} // namespace mb::ui
