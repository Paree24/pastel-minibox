#pragma once
#include "Widgets.h"
#include "app/presets/PresetBank.h"

namespace mb::ui
{
/** Preset browser: search, bank buttons (All, the families, User), a numbered list where selecting a
    row loads it (double-click closes), inline name + SAVE (a second click confirms OVERWRITE?), OVER to
    replace the selected factory preset with the current sound, DEL to delete a user preset or restore
    an overwritten factory one. */
class PresetBrowser : public juce::Component, private juce::TextEditor::Listener
{
public:
    PresetBrowser (AppContext&, DeviceProcessor&, juce::Colour);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    std::function<void()> onLoaded;

    static void show (AppContext&, DeviceProcessor&, juce::Component* anchor, std::function<void()> onLoaded = {});

private:
    void textEditorTextChanged (juce::TextEditor& t) override;
    void textEditorReturnKeyPressed (juce::TextEditor& t) override { if (&t == &saveName) clickSave(); }
    void reload();
    void filter();
    void select (int row, bool load);
    void ensureVisible (int row);
    void clickSave();
    void close();
    const PresetInfo* selected() const { return sel >= 0 && sel < (int) shown.size() ? &shown[(size_t) sel] : nullptr; }

    AppContext& ctx;
    DeviceProcessor& dev;
    juce::Colour colour;
    std::vector<PresetInfo> all, shown;
    std::vector<juce::String> banks;   // "All", families..., "User"
    juce::String bank { "All" };
    juce::TextEditor search, saveName;
    float scroll = 0.0f;
    int sel = -1;
    bool confirmOverwrite = false;
    Feedback fb { *this };
    std::vector<std::pair<juce::Rectangle<float>, juce::String>> chipR;
    juce::Rectangle<float> listR, saveR, overR, delR;
};
} // namespace mb::ui
