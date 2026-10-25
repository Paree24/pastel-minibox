#pragma once
#include "Widgets.h"

namespace mb::ui
{
/** Master effects (desktop.py master_fx_drawer, mockup 19_master_fx): the chain's 8 slots in series (EFX Grid's
    effects), or the 8 performance pads' effects; the selected one's mode, controls and mix below. Chain: add,
    replace, remove, move, bypass. Pads: tune each pad's effect, reset it to the pad's settings. */
class MasterFxDrawer : public juce::Component
{
public:
    explicit MasterFxDrawer (AppContext&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();
    std::function<void()> onClose;
    /** Shows the chain (false) or the pads (true), with slot / pad `index` selected. */
    void show (bool pads, int index);
    bool showingPads() const { return padsView; }
    int selected() const { return sel; }

private:
    FxSlot& slot (int i) const;
    void rebuildKnobs();
    void addEffect (int index, bool replace);
    void remove();
    void move (int dir);
    void changed();

    AppContext& ctx;
    bool padsView = false;
    int sel = 0;
    juce::String builtFor;                       // what the knobs show (rebuilt when it changes)
    std::vector<std::unique_ptr<KnobView>> knobs;
    std::unique_ptr<KnobView> mixKnob;
    struct Zone { juce::Rectangle<float> r; juce::String id; int a = 0; };
    std::vector<Zone> zones;
    juce::Rectangle<float> menuArea;
    juce::String sig;
    Feedback fb { *this };
};
} // namespace mb::ui
