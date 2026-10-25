#pragma once
#include "Theme.h"

namespace mb::ui
{
/** One look-and-feel for every stock JUCE part (menus, dialogs, call-outs, scrollbars, text fields). */
class MiniboxLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MiniboxLookAndFeel();
    juce::Font getPopupMenuFont() override { return font (13, Weight::Bold); }
    juce::Font getAlertWindowTitleFont() override { return font (15, Weight::Black); }
    juce::Font getAlertWindowMessageFont() override { return font (13, Weight::Bold); }
    juce::Font getAlertWindowFont() override { return font (13, Weight::Bold); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (12, Weight::Bold); }
    juce::Font getLabelFont (juce::Label&) override { return font (13, Weight::Bold); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13, Weight::Bold); }
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    int getCallOutBoxBorderSize (const juce::CallOutBox&) override { return 12; }
};
} // namespace mb::ui
