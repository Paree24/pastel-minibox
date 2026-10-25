#pragma once
#include "Widgets.h"
#include "model/MidiMap.h"

namespace mb::ui
{
/** The actions a controller button can be mapped to (id, name); ids are `action:<id>` targets. */
const std::vector<std::pair<juce::String, juce::String>>& learnActions();
/** A target's name for the learn drawer ("Selected · Macro 1", "Track 1 · Level", "Play"). */
juce::String learnTargetName (AppContext&, const juce::String& target);
/** A binding onto a continuous target (macros, mixer, parameters, master, tempo); null for actions or when the
    target is not there (an empty slot, a parameter the selected instrument does not have). */
std::unique_ptr<Binding> learnBinding (AppContext&, const juce::String& target);
/** Applies a mapped message to a continuous target (pickup, toggle / momentary buttons, relative encoders). */
void applyLearnControl (Binding&, const MidiControl&, MidiMapping&);
/** A mapping's mode menu (absolute / relative / buttons, pickup, range). */
void showMappingModeMenu (MidiMap&, int index, std::function<void()> after, juce::Component* at = nullptr, juce::Rectangle<float> area = {});
/** Every target a controller can drive, as a menu; item i is ids[i - 1]. */
juce::PopupMenu learnTargetMenu (AppContext&, std::vector<juce::String>& ids);

/** The MIDI learn drawer (desktop.py learn_drawer, mockup 15_midi_learn): the armed control, the last message
    received, the Action menu, whether macros / mixer follow the selected instrument, map files and the list of
    mappings with their mode, range and pickup. */
class LearnDrawer : public juce::Component
{
public:
    LearnDrawer (AppContext&, MidiMap&);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::function<void()> onClose;
    /** Macros, level, pan, sends and parameters are learnt "for the selected instrument". */
    bool followSelected = true;
    /** What a click on a control arms (the selected-instrument form when following). */
    juce::String armTarget (const juce::String& controlTarget) const;
    /** The message mapped to a control (its own target, or the selected-instrument form on the selected track). */
    juce::String labelFor (const juce::String& controlTarget) const;

private:
    void showActions();
    void showMaps();
    void showMode (int index);
    void fileAction (bool save);
    AppContext& ctx;
    MidiMap& map;
    int scroll = 0;
    juce::Rectangle<float> menuArea;
    struct Zone { juce::Rectangle<float> r; juce::String id; int a = 0; };
    std::vector<Zone> zones;
    std::unique_ptr<juce::FileChooser> chooser;
    Feedback fb { *this };
};
} // namespace mb::ui
