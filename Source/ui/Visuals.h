#pragma once
#include "Widgets.h"

namespace mb::ui
{
enum class VisualKind
{
    None, Filter, Hpf, Adsr, Decay, Rate4, MacroEnv, PitchEnv, Wave, Lfo, Algo, Ops, Sample, Slices, Pads, Lane,
    Accent, Ring, Chorus, Delay, Slide, Drive, Voices, Arp, Bits, Reverb
};

/** A visual reads device parameters by id (resolved for the selected part / operator / zone). */
struct VisualSpec
{
    VisualKind kind = VisualKind::None;
    std::vector<juce::String> ids;
    int variant = 0;
};

class VisualView : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    VisualView (AppContext& ctx, DeviceProcessor& dev, VisualSpec spec, juce::Colour colour, int pattern, int track);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    /** Repaint when anything the visual shows has changed. */
    void refresh();

    static void drawRackVisual (juce::Graphics&, juce::Rectangle<float>, DeviceProcessor&, juce::Colour);

private:
    float v (int k) const;            // value of ids[k]
    int iv (int k) const { return juce::roundToInt (v (k)); }
    uint64_t signature() const;
    juce::Rectangle<float> inner() const { return getLocalBounds().toFloat(); }
    void sliceDropped (const juce::File&);
    void loadDroppedSamples (const juce::StringArray&);
    void browseFactory();   // the Browse button: factory samples, kits and loops
    void loadFromDisk();    // the Load… button: a file chooser
    std::unique_ptr<juce::FileChooser> chooser;
    Feedback fb { *this };

    AppContext& ctx;
    DeviceProcessor& dev;
    VisualSpec spec;
    juce::Colour colour;
    int pattern, track;
    std::vector<int> idx;
    uint64_t lastSig = 0;
    int dragMarker = -1, dragPoint = -1, dragStep = -1;
    bool dropHover = false;
    float dragStartValue = 0.0f;
};

namespace draw
{
using R = juce::Rectangle<float>;
void filter (juce::Graphics&, R, juce::Colour, int mode, float cutHz, float res, int poles, bool grid = true);
void adsr (juce::Graphics&, R, juce::Colour, float a, float d, float s, float r);
void wave (juce::Graphics&, R, juce::Colour, std::function<float (float)> fn, float cycles = 2.0f, std::function<float (float)> overlay = {});
void waveform (juce::Graphics&, R, juce::Colour, const std::vector<float>& peaks, float from = 0, float to = 1, float dimBefore = -1, float dimAfter = 2);
void algo (juce::Graphics&, R, juce::Colour, int algorithm, int selected);
void ring (juce::Graphics&, R, juce::Colour, float decay, float cycles);
void delayTaps (juce::Graphics&, R, juce::Colour, float fb);
void chorus (juce::Graphics&, R, juce::Colour, int mode);
void pads (juce::Graphics&, R, juce::Colour, const juce::StringArray& names, int selected, const std::vector<bool>& lit, int cols,
           const std::vector<const std::vector<float>*>& waves = {});
} // namespace draw
} // namespace mb::ui
