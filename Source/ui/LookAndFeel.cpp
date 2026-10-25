#include "LookAndFeel.h"

namespace mb::ui
{
MiniboxLookAndFeel::MiniboxLookAndFeel()
{
    auto scheme = juce::LookAndFeel_V4::ColourScheme (col::surface, col::bg, col::surface, col::divider, col::text, col::control,
                                                      col::bg, col::pattern, col::text);
    setColourScheme (scheme);
    setColour (juce::PopupMenu::backgroundColourId, col::surface);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::control);
    setColour (juce::PopupMenu::highlightedTextColourId, col::text);
    setColour (juce::AlertWindow::backgroundColourId, col::surface);
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::AlertWindow::outlineColourId, col::divider);
    setColour (juce::TextButton::buttonColourId, col::control);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextEditor::backgroundColourId, col::control);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::outlineColourId, col::divider);
    setColour (juce::TextEditor::focusedOutlineColourId, col::faint);
    setColour (juce::TooltipWindow::backgroundColourId, col::control);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);
    setColour (juce::DocumentWindow::textColourId, col::text);
    setColour (juce::ComboBox::backgroundColourId, col::control);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ListBox::backgroundColourId, col::surface);
    setColour (juce::ScrollBar::thumbColourId, col::faint);
}

juce::Typeface::Ptr MiniboxLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    const bool bold = f.isBold();
    return font (12, bold ? Weight::Black : Weight::Bold).getTypefacePtr();
}

void MiniboxLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillPath (path, juce::AffineTransform::translation (0, 3));
    g.setColour (col::surface);
    g.fillPath (path);
    g.setColour (col::divider);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

void MiniboxLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::surface);
    g.setColour (col::divider);
    g.drawRect (0, 0, w, h);
}

void MiniboxLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1);
    g.setColour (down ? col::faint : (over ? col::divider.brighter (0.2f) : col::control));
    g.fillRoundedRectangle (r, r.getHeight() / 2);
}

void MiniboxLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (col::control);
    g.setColour (col::text);
    g.setFont (font (12, Weight::Bold));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (8, 4), juce::Justification::centredLeft, 3);
}

juce::Rectangle<int> MiniboxLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const int w = juce::jmin (420, (int) textWidth (text, 12) + 20), h = 26;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18, pos.y + 18, w, h).constrainedWithin (parent);
}
} // namespace mb::ui
