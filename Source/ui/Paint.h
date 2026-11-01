#pragma once
#include "Theme.h"

/** Drawing primitives ported from mockups/tools/svgkit.py so the app matches the mockups. */
namespace mb::ui::paint
{
using G = juce::Graphics;
using R = juce::Rectangle<float>;

void text (G&, const juce::String&, float x, float baselineY, float size, Weight, juce::Colour, juce::Justification j = juce::Justification::left);
void textCentred (G&, const juce::String&, R area, float size, Weight, juce::Colour);
juce::Path arc (float cx, float cy, float r, float degFrom, float degTo);

void card (G&, R, const juce::String& title = {}, juce::Colour fill = col::surface);
void well (G&, R);
void knob (G&, float cx, float cy, float r, float v, juce::Colour, bool bipolar, bool locked = false);
float pill (G&, float x, float y, const juce::String&, float size, juce::Colour fill, juce::Colour textCol, float h = 28,
            float padx = 12, Weight w = Weight::Bold, bool caret = false, juce::Justification anchor = juce::Justification::left,
            float fixedW = -1.0f);
/** Segmented choice; returns segment rectangles (empty when drawn as a dropdown). */
std::vector<R> segmented (G&, R, const juce::StringArray& opts, int sel, juce::Colour, float size = 11.0f);
bool segmentedFits (float w, const juce::StringArray& opts, float size = 11.0f);
void trackBadge (G&, float cx, float cy, int idx, float r = 13.0f);
/** The Minibox mark (Assets/icon/pastel-minibox.svg, the Android icon too): four rounded pads, lilac, mint, pink and
    amber, the amber one a play button; `pad` square pads `gap` apart from (x, y). */
void brandMark (G&, float x, float y, float pad = 9.0f, float gap = 2.0f);
void deviceDots (G&, float cx, float cy, const std::array<bool, 4>& on, float r = 3.0f, float gap = 9.0f);
void msButtons (G&, float x, float y, bool mute, bool solo, float size = 24.0f);

// icons
void play (G&, float cx, float cy, float size, juce::Colour);
void stop (G&, float cx, float cy, float size, juce::Colour);
void record (G&, float cx, float cy, float size, juce::Colour);
void chevron (G&, float cx, float cy, float size, juce::Colour, char dir = 'd', float sw = 2.0f);
void dots (G&, float cx, float cy, juce::Colour, float gap = 5.0f, float r = 1.8f);
void plus (G&, float cx, float cy, float size, juce::Colour, float sw = 2.0f);
void minus (G&, float cx, float cy, float size, juce::Colour, float sw = 2.0f);
void metronome (G&, float cx, float cy, float size, juce::Colour);
void openIcon (G&, float cx, float cy, float size, juce::Colour);
void saveIcon (G&, float cx, float cy, float size, juce::Colour);
void newIcon (G&, float cx, float cy, float size, juce::Colour);
void folderIcon (G&, float cx, float cy, float size, juce::Colour);
void exportIcon (G&, float cx, float cy, float size, juce::Colour);
void lockIcon (G&, float cx, float cy, float size, juce::Colour);
void keysIcon (G&, float cx, float cy, float size, juce::Colour);
void toStartIcon (G&, float cx, float cy, float size, juce::Colour);
void undoIcon (G&, float cx, float cy, float size, juce::Colour, bool redo = false);
void searchIcon (G&, float cx, float cy, float size, juce::Colour);
/** Piano-roll tools: 0 select, 1 draw, 2 paint, 3 erase. */
void toolIcon (G&, float cx, float cy, int kind, juce::Colour);
} // namespace mb::ui::paint
