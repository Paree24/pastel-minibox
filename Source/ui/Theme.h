#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace mb::ui
{
/** Theme tokens (ARCHITECTURE.md §9.2, mockups/SPEC.md). */
namespace col
{
inline const juce::Colour bg { 0xff141518 }, surface { 0xff20232A }, control { 0xff262930 }, divider { 0xff2A2D34 };
inline const juce::Colour text { 0xffECE8E3 }, quiet { 0xffA8A49E }, faint { 0xff55585E };
inline const juce::Colour pattern { 0xffB9A7F0 }, record { 0xffE88A8A }, mute { 0xffF2B48F };
inline const juce::Colour controlEdge { 0xff2E323A }, altCell { 0xff23262D };
inline juce::Colour track (int i)
{
    static const juce::Colour t[8] = { juce::Colour (0xff9EB9F3), juce::Colour (0xff8FD0C2), juce::Colour (0xffE8B86A), juce::Colour (0xffE9A3B8),
                                       juce::Colour (0xffA8D58A), juce::Colour (0xffF0A07C), juce::Colour (0xff7FC6E8), juce::Colour (0xffD7A6E8) };
    return t[std::clamp (i, 0, 7)];
}
} // namespace col

/** Base layout (desktop.py): everything is laid out at 1600 × 1000 and scaled. */
namespace geo
{
constexpr int W = 1600, H = 1000, M = 24, GAP = 12, TOP = 64;
}

enum class Weight { Regular, Bold, Black };
juce::Font font (float size, Weight w = Weight::Bold);
/** Approximate text width (matches the mockup generator's metrics). */
float textWidth (const juce::String&, float size, Weight w = Weight::Bold);
} // namespace mb::ui
