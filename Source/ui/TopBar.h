#pragma once
#include "Widgets.h"

namespace mb::ui
{
/** Standalone top bar (desktop.py topbar): brand, Rack/Seq, record/play/stop, BPM −/+/Tap, 4/4 ▾,
    metronome, A–D switcher and the project controls (name, New, Open, Save, Export, ⋯). */
class TopBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TopBar (AppContext&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void refresh();
    /** Does what clicking a zone does (also MIDI-mapped actions). */
    void trigger (const juce::String& zoneId, juce::ModifierKeys mods = {});
    /** The MIDI learn target of a zone (`action:<id>`, `tempo`), or empty. */
    static juce::String learnTarget (const juce::String& zoneId);
    /** A zone's area (menus open there). */
    juce::Rectangle<float> zoneRect (const juce::String& id) const { for (auto& z : zones) if (z.id == id) return z.r; return {}; }
    /** Live (0) / Step (1) record buttons: arm in that mode, or turn recording off when it is the armed one. */
    void toggleRecord (int mode);
    /** The recording settings (the ▾ by the record buttons, right-click on them, Alt+R; mockup 18_recording). */
    void showRecording();
    /** The popover's content (screenshots, tests). */
    std::unique_ptr<juce::Component> createRecordingPanel();

    enum class View { Rack, Seq, Editor };
    View view = View::Rack;
    juce::String projectName;
    std::function<void (View)> onView;
    std::function<void()> onNew, onOpen, onSave, onExport, onMenu, onRename, onPlayDrawer;
    bool playOpen = false;

private:
    void tap();
    void showTimeSignature();
    AppContext& ctx;
    juce::String sig;
    bool draggingBpm = false, learnPress = false;
    double bpmAtDown = 120.0;
    std::vector<double> taps;
    struct Zone { juce::Rectangle<float> r; juce::String id; juce::String tip; };
    std::vector<Zone> zones;
    juce::String zoneAt (juce::Point<float>) const;
    Feedback fb { *this };
};
} // namespace mb::ui
