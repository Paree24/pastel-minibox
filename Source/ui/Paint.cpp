#include "Paint.h"
#include "MiniboxAssets.h"

namespace mb::ui
{
juce::Font font (float size, Weight w)
{
    static juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (MiniboxAssets::LatoRegular_ttf, MiniboxAssets::LatoRegular_ttfSize);
    static juce::Typeface::Ptr bold = juce::Typeface::createSystemTypefaceFor (MiniboxAssets::LatoBold_ttf, MiniboxAssets::LatoBold_ttfSize);
    static juce::Typeface::Ptr black = juce::Typeface::createSystemTypefaceFor (MiniboxAssets::LatoBlack_ttf, MiniboxAssets::LatoBlack_ttfSize);
    auto tf = w == Weight::Black ? black : (w == Weight::Bold ? bold : regular);
    return juce::Font (juce::FontOptions (tf).withHeight (size * 1.2f));   // CSS px size → JUCE height
}

float textWidth (const juce::String& s, float size, Weight w)
{
    return juce::GlyphArrangement::getStringWidth (font (size, w), s);
}
} // namespace mb::ui

namespace mb::ui::paint
{
namespace
{
float rad (float deg) { return juce::degreesToRadians (deg + 90.0f); }   // SVG angle (east, y down) → JUCE (north, clockwise)
}

void text (G& g, const juce::String& s, float x, float baseline, float size, Weight w, juce::Colour c, juce::Justification j)
{
    g.setColour (c);
    g.setFont (font (size, w));
    g.drawSingleLineText (s, juce::roundToInt (x), juce::roundToInt (baseline), j);
}

void textCentred (G& g, const juce::String& s, R area, float size, Weight w, juce::Colour c)
{
    g.setColour (c);
    g.setFont (font (size, w));
    g.drawText (s, area, juce::Justification::centred, true);
}

juce::Path arc (float cx, float cy, float r, float a0, float a1)
{
    juce::Path p;
    p.addCentredArc (cx, cy, r, r, 0.0f, rad (a0), rad (a1), true);
    return p;
}

void card (G& g, R r, const juce::String& title, juce::Colour fill)
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, 12.0f);
    if (title.isNotEmpty())
    {
        g.setColour (col::quiet);
        g.setFont (font (11.0f, Weight::Black).withExtraKerningFactor (0.11f));
        g.drawSingleLineText (title.toUpperCase(), juce::roundToInt (r.getX() + 16), juce::roundToInt (r.getY() + 26));
    }
}

void well (G& g, R r)
{
    g.setColour (col::bg);
    g.fillRoundedRectangle (r, 8.0f);
}

void knob (G& g, float cx, float cy, float r, float v, juce::Colour c, bool bipolar, bool locked)
{
    const float a0 = 135.0f, sweep = 270.0f, rr = r + 5.0f;
    const juce::PathStrokeType st (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (col::divider);
    g.strokePath (arc (cx, cy, rr, a0, a0 + sweep), st);
    const float av = a0 + sweep * juce::jlimit (0.0f, 1.0f, v);
    g.setColour (c);
    if (bipolar)
    {
        const float mid = a0 + sweep * 0.5f;
        if (std::abs (av - mid) > 0.5f) g.strokePath (arc (cx, cy, rr, std::min (mid, av), std::max (mid, av)), st);
    }
    else if (v > 0.005f) g.strokePath (arc (cx, cy, rr, a0, av), st);
    g.setColour (col::control);
    g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
    g.setColour (col::controlEdge);
    g.drawEllipse (cx - r, cy - r, r * 2, r * 2, 1.0f);
    const float a = juce::degreesToRadians (av);
    g.setColour (col::text);
    g.drawLine (cx + std::cos (a) * r * 0.3f, cy + std::sin (a) * r * 0.3f, cx + std::cos (a) * r * 0.78f, cy + std::sin (a) * r * 0.78f, 2.4f);
    if (locked)
    {
        g.setColour (col::pattern);
        g.fillEllipse (cx + r * 0.85f - 4, cy - r * 0.85f - 4, 8, 8);
    }
}

float pill (G& g, float x, float y, const juce::String& s, float size, juce::Colour fill, juce::Colour tc, float h, float padx,
            Weight w, bool caret, juce::Justification anchor, float fixedW)
{
    const float width = fixedW > 0 ? fixedW : textWidth (s, size, w) + padx * 2 + (caret ? 14.0f : 0.0f);
    if (anchor == juce::Justification::horizontallyCentred) x -= width / 2;
    else if (anchor == juce::Justification::right) x -= width;
    g.setColour (fill);
    g.fillRoundedRectangle (x, y, width, h, h / 2);
    const float tx = x + (width - (caret ? 14.0f : 0.0f)) / 2;
    text (g, s, tx, y + h / 2 + size * 0.36f, size, w, tc, juce::Justification::horizontallyCentred);
    if (caret) chevron (g, x + width - padx - 4, y + h / 2, 7, tc, 'd', 1.6f);
    return width;
}

bool segmentedFits (float w, const juce::StringArray& opts, float size)
{
    float need = 0.0f;
    for (auto& o : opts) need += textWidth (o, size) + 14.0f;
    return need <= w;
}

std::vector<R> segmented (G& g, R r, const juce::StringArray& opts, int sel, juce::Colour c, float size)
{
    std::vector<R> out;
    const float h = r.getHeight();
    g.setColour (col::control);
    g.fillRoundedRectangle (r, h / 2);
    if (! segmentedFits (r.getWidth(), opts, size))
    {
        text (g, opts[std::clamp (sel, 0, opts.size() - 1)], r.getX() + 14, r.getCentreY() + size * 0.36f, size, Weight::Bold, c);
        chevron (g, r.getRight() - 16, r.getCentreY(), 8, col::quiet, 'd', 1.6f);
        return out;
    }
    float need = 0.0f;
    std::vector<float> widths;
    for (auto& o : opts) { widths.push_back (textWidth (o, size) + 14.0f); need += widths.back(); }
    const float extra = (r.getWidth() - need) / (float) opts.size();
    float x = r.getX();
    for (int i = 0; i < opts.size(); ++i)
    {
        const float ww = widths[(size_t) i] + extra;
        R seg (x, r.getY(), ww, h);
        out.push_back (seg);
        if (i == sel)
        {
            g.setColour (c);
            g.fillRoundedRectangle (seg.reduced (2), (h - 4) / 2);
            text (g, opts[i], seg.getCentreX(), r.getCentreY() + size * 0.36f, size, Weight::Black, col::bg, juce::Justification::horizontallyCentred);
        }
        else text (g, opts[i], seg.getCentreX(), r.getCentreY() + size * 0.36f, size, Weight::Bold, col::quiet, juce::Justification::horizontallyCentred);
        x += ww;
    }
    return out;
}

void trackBadge (G& g, float cx, float cy, int idx, float r)
{
    g.setColour (col::track (idx));
    g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
    text (g, juce::String (idx + 1), cx, cy + 4.5f * r / 13.0f, 12.0f * r / 13.0f, Weight::Black, col::bg, juce::Justification::horizontallyCentred);
}

void deviceDots (G& g, float cx, float cy, const std::array<bool, 4>& on, float r, float gap)
{
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (on[(size_t) i] ? col::track (i) : col::faint);
        g.fillEllipse (cx + (i - 1.5f) * gap - r, cy - r, r * 2, r * 2);
    }
}

void msButtons (G& g, float x, float y, bool mute, bool solo, float size)
{
    g.setColour (mute ? col::mute : col::control);
    g.fillRoundedRectangle (x, y, size, size, 7.0f);
    text (g, "M", x + size / 2, y + size / 2 + 4, 11, Weight::Black, mute ? col::bg : col::quiet, juce::Justification::horizontallyCentred);
    g.setColour (solo ? col::text : col::control);
    g.fillRoundedRectangle (x + size + 6, y, size, size, 7.0f);
    text (g, "S", x + size * 1.5f + 6, y + size / 2 + 4, 11, Weight::Black, solo ? col::bg : col::quiet, juce::Justification::horizontallyCentred);
}

void play (G& g, float cx, float cy, float h, juce::Colour c)
{
    juce::Path p;
    p.addTriangle (cx - h * 0.38f, cy - h * 0.5f, cx + h * 0.55f, cy, cx - h * 0.38f, cy + h * 0.5f);
    g.setColour (c);
    g.fillPath (p);
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void stop (G& g, float cx, float cy, float s, juce::Colour c) { g.setColour (c); g.fillRoundedRectangle (cx - s / 2, cy - s / 2, s, s, 2.0f); }
void record (G& g, float cx, float cy, float s, juce::Colour c) { g.setColour (c); g.fillEllipse (cx - s / 2, cy - s / 2, s, s); }

void chevron (G& g, float cx, float cy, float size, juce::Colour c, char dir, float sw)
{
    const float h = size / 2;
    juce::Path p;
    switch (dir)
    {
        case 'u': p.startNewSubPath (cx - h, cy + h / 2); p.lineTo (cx, cy - h / 2); p.lineTo (cx + h, cy + h / 2); break;
        case 'l': p.startNewSubPath (cx + h / 2, cy - h); p.lineTo (cx - h / 2, cy); p.lineTo (cx + h / 2, cy + h); break;
        case 'r': p.startNewSubPath (cx - h / 2, cy - h); p.lineTo (cx + h / 2, cy); p.lineTo (cx - h / 2, cy + h); break;
        default: p.startNewSubPath (cx - h, cy - h / 2); p.lineTo (cx, cy + h / 2); p.lineTo (cx + h, cy - h / 2); break;
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void dots (G& g, float cx, float cy, juce::Colour c, float gap, float r)
{
    g.setColour (c);
    for (int i = -1; i <= 1; ++i) g.fillEllipse (cx + i * gap - r, cy - r, r * 2, r * 2);
}

void plus (G& g, float cx, float cy, float s, juce::Colour c, float sw)
{
    g.setColour (c);
    g.drawLine (cx - s / 2, cy, cx + s / 2, cy, sw);
    g.drawLine (cx, cy - s / 2, cx, cy + s / 2, sw);
}

void minus (G& g, float cx, float cy, float s, juce::Colour c, float sw) { g.setColour (c); g.drawLine (cx - s / 2, cy, cx + s / 2, cy, sw); }

void metronome (G& g, float cx, float cy, float h, juce::Colour c)
{
    juce::Path p;
    p.startNewSubPath (cx - h * 0.32f, cy + h * 0.5f); p.lineTo (cx - h * 0.14f, cy - h * 0.5f);
    p.lineTo (cx + h * 0.14f, cy - h * 0.5f); p.lineTo (cx + h * 0.32f, cy + h * 0.5f); p.closeSubPath();
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine (cx, cy + h * 0.3f, cx + h * 0.3f, cy - h * 0.35f, 1.8f);
}

namespace
{
void stroke (G& g, const juce::Path& p, juce::Colour c, float w = 1.8f)
{
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
}

void openIcon (G& g, float cx, float cy, float size, juce::Colour c)
{
    const float h = size / 2;
    juce::Path p;
    p.startNewSubPath (cx - h * 0.2f, cy - h); p.lineTo (cx - h, cy - h); p.lineTo (cx - h, cy + h); p.lineTo (cx + h, cy + h); p.lineTo (cx + h, cy + h * 0.2f);
    p.startNewSubPath (cx - h * 0.1f, cy + h * 0.1f); p.lineTo (cx + h, cy - h);
    p.startNewSubPath (cx + h * 0.2f, cy - h); p.lineTo (cx + h, cy - h); p.lineTo (cx + h, cy - h * 0.2f);
    stroke (g, p, c);
}

void saveIcon (G& g, float cx, float cy, float size, juce::Colour c)
{
    const float h = size / 2;
    juce::Path p;
    p.startNewSubPath (cx - h, cy - h); p.lineTo (cx + h * 0.5f, cy - h); p.lineTo (cx + h, cy - h * 0.5f); p.lineTo (cx + h, cy + h); p.lineTo (cx - h, cy + h); p.closeSubPath();
    p.addRectangle (cx - h * 0.5f, cy + h * 0.15f, h, h * 0.85f);
    stroke (g, p, c, 1.6f);
}

void newIcon (G& g, float cx, float cy, float size, juce::Colour c)
{
    const float h = size / 2;
    g.setColour (c);
    g.drawRoundedRectangle (cx - h * 0.8f, cy - h, h * 1.6f, h * 2, 2.0f, 1.6f);
    plus (g, cx, cy, h, c, 1.6f);
}

void folderIcon (G& g, float cx, float cy, float size, juce::Colour c)
{
    const float h = size / 2;
    juce::Path p;
    p.startNewSubPath (cx - h, cy - h * 0.7f); p.lineTo (cx - h * 0.2f, cy - h * 0.7f); p.lineTo (cx + h * 0.05f, cy - h * 0.4f);
    p.lineTo (cx + h, cy - h * 0.4f); p.lineTo (cx + h, cy + h * 0.75f); p.lineTo (cx - h, cy + h * 0.75f); p.closeSubPath();
    stroke (g, p, c, 1.6f);
}

void exportIcon (G& g, float cx, float cy, float size, juce::Colour c)
{
    const float h = size / 2;
    juce::Path p;
    p.startNewSubPath (cx - h, cy + h * 0.2f); p.lineTo (cx - h, cy + h); p.lineTo (cx + h, cy + h); p.lineTo (cx + h, cy + h * 0.2f);
    p.startNewSubPath (cx, cy + h * 0.4f); p.lineTo (cx, cy - h);
    stroke (g, p, c, 1.6f);
    chevron (g, cx, cy - h * 0.5f, h, c, 'u', 1.6f);
}

void lockIcon (G& g, float cx, float cy, float size, juce::Colour c)
{
    const float h = size / 2;
    g.setColour (c);
    g.fillRoundedRectangle (cx - h * 0.7f, cy - h * 0.1f, h * 1.4f, h * 1.1f, 1.5f);
    g.strokePath (arc (cx, cy - h * 0.1f, h * 0.45f, 180.0f, 360.0f), juce::PathStrokeType (1.5f));
}

void searchIcon (G& g, float cx, float cy, float size, juce::Colour c)
{
    g.setColour (c);
    g.drawEllipse (cx - size * 0.42f, cy - size * 0.42f, size * 0.64f, size * 0.64f, 1.8f);
    g.drawLine (cx + size * 0.14f, cy + size * 0.14f, cx + size * 0.45f, cy + size * 0.45f, 1.8f);
}
void keysIcon (G& g, float cx, float cy, float size, juce::Colour c)
{
    // a small keyboard: the Play / input symbol (svgkit.icon_keys)
    const float w = size, h = size * 0.72f, x = cx - w / 2, y = cy - h / 2;
    g.setColour (c);
    g.drawRoundedRectangle (x, y, w, h, 2.5f, 1.6f);
    for (int i = 1; i <= 3; ++i) g.drawLine (x + w * (float) i / 4, y + h * 0.55f, x + w * (float) i / 4, y + h, 1.4f);
    for (int i : { 1, 3 }) g.fillRoundedRectangle (x + w * (float) i / 4 - w * 0.07f, y, w * 0.14f, h * 0.55f, 1.0f);
}

void toStartIcon (G& g, float cx, float cy, float size, juce::Colour c)
{
    g.setColour (c);
    g.fillRoundedRectangle (cx - size * 0.5f, cy - size / 2, size * 0.16f, size, 1.0f);
    juce::Path p;
    p.addTriangle (cx + size * 0.5f, cy - size / 2, cx - size * 0.25f, cy, cx + size * 0.5f, cy + size / 2);
    g.fillPath (p);
}

void undoIcon (G& g, float cx, float cy, float size, juce::Colour c, bool redo)
{
    // a curved arrow: the arc runs over the top from 9 o'clock (undo) or 3 o'clock (redo), the head at its start
    const float r = size * 0.45f, k = redo ? -1.0f : 1.0f;
    juce::Path p;
    if (redo) p.addCentredArc (cx, cy, r, r, 0.0f, juce::MathConstants<float>::halfPi, -2.4f, true);
    else p.addCentredArc (cx, cy, r, r, 0.0f, -juce::MathConstants<float>::halfPi, 2.4f, true);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float tx = cx - k * r, ty = cy + 1.0f;
    juce::Path h;
    h.startNewSubPath (tx - 3.5f, ty - 3.5f); h.lineTo (tx, ty + 0.5f); h.lineTo (tx + 3.5f, ty - 3.5f);
    g.strokePath (h, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void toolIcon (G& g, float cx, float cy, int kind, juce::Colour c)
{
    // piano-roll tools (svgkit.icon_tool): 0 select (arrow), 1 draw (pencil), 2 paint (brush), 3 erase (eraser)
    g.setColour (c);
    juce::Path p;
    if (kind == 0)
    {
        p.startNewSubPath (cx - 4, cy - 7); p.lineTo (cx - 4, cy + 5); p.lineTo (cx - 1, cy + 2); p.lineTo (cx + 1.5f, cy + 7);
        p.lineTo (cx + 3.5f, cy + 6); p.lineTo (cx + 1, cy + 1.2f); p.lineTo (cx + 5, cy + 1.2f); p.closeSubPath();
        g.fillPath (p);
    }
    else if (kind == 1)
    {
        g.drawLine (cx - 4, cy + 4, cx + 4, cy - 4, 3.2f);
        p.addTriangle (cx - 5.5f, cy + 5.5f, cx - 4.8f, cy + 2.4f, cx - 2.4f, cy + 4.8f);
        g.fillPath (p);
    }
    else if (kind == 2)
    {
        g.drawLine (cx + 5, cy - 6, cx, cy - 1, 2.2f);
        p.startNewSubPath (cx - 1, cy - 2.5f);
        p.quadraticTo (cx + 2.5f, cy + 1, cx - 1, cy + 3.5f);
        p.quadraticTo (cx - 4, cy + 6.5f, cx - 6, cy + 6);
        p.quadraticTo (cx - 5.5f, cy + 3, cx - 3.5f, cy);
        p.closeSubPath();
        g.fillPath (p);
    }
    else
    {
        p.startNewSubPath (cx - 6, cy + 2); p.lineTo (cx + 1, cy - 5); p.lineTo (cx + 6, cy); p.lineTo (cx + 1, cy + 5); p.lineTo (cx - 3, cy + 5); p.closeSubPath();
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawLine (cx - 2.5f, cy - 1.5f, cx + 3.5f, cy + 4.5f, 1.6f);
        g.drawLine (cx - 3, cy + 7, cx + 6, cy + 7, 1.4f);
    }
}

} // namespace mb::ui::paint
