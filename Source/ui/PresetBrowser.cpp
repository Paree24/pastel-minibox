#include "PresetBrowser.h"

namespace mb::ui
{
namespace
{
constexpr float kRow = 30.0f;
void styleEditor (juce::TextEditor& t, const juce::String& hint)
{
    t.setFont (font (13, Weight::Bold));
    t.setColour (juce::TextEditor::backgroundColourId, col::control);
    t.setColour (juce::TextEditor::textColourId, col::text);
    t.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    t.setColour (juce::TextEditor::focusedOutlineColourId, col::faint);
    t.setColour (juce::CaretComponent::caretColourId, col::text);
    t.setTextToShowWhenEmpty (hint, col::faint);
    t.setIndents (12, 7);
}
} // namespace

PresetBrowser::PresetBrowser (AppContext& c, DeviceProcessor& d, juce::Colour colr) : ctx (c), dev (d), colour (colr)
{
    styleEditor (search, "Search");
    styleEditor (saveName, "Preset name");
    search.addListener (this);
    saveName.addListener (this);
    saveName.setText (dev.presetName == "Init" ? juce::String() : dev.presetName, false);
    addAndMakeVisible (search);
    addAndMakeVisible (saveName);
    setWantsKeyboardFocus (true);
    presets::rescan();
    reload();
    setSize (480, 600);
    for (int i = 0; i < (int) shown.size(); ++i)
        if (shown[(size_t) i].name == dev.presetName) { sel = i; break; }
    ensureVisible (sel);
}

void PresetBrowser::show (AppContext& ctx, DeviceProcessor& d, juce::Component* anchor, std::function<void()> onLoaded)
{
    int track = 0;
    for (int t = 0; t < kTracks; ++t) if (ctx.engine.device (ctx.focusPattern(), t) == &d) track = t;
    auto b = std::make_unique<PresetBrowser> (ctx, d, ctx.standalone ? col::track (track) : deviceColour (d.type()));
    b->onLoaded = std::move (onLoaded);
    juce::CallOutBox::launchAsynchronously (std::move (b), anchor->getScreenBounds().withSizeKeepingCentre (60, 40).withY (anchor->getScreenY()), nullptr);
}

void PresetBrowser::reload()
{
    all = presets::list (dev.type());
    banks = { "All" };
    for (auto& f : presets::families (dev.type())) banks.push_back (f);
    banks.push_back ("User");
    filter();
}

void PresetBrowser::filter()
{
    const auto q = search.getText().trim().toLowerCase();
    const auto keep = selected() != nullptr ? selected()->file : juce::File();
    shown.clear();
    for (auto& p : all)
    {
        if (bank == "User" ? p.factory : (bank != "All" && p.family != bank)) continue;
        if (q.isNotEmpty() && ! p.name.toLowerCase().contains (q) && ! p.family.toLowerCase().contains (q)) continue;
        shown.push_back (p);
    }
    sel = -1;
    for (int i = 0; i < (int) shown.size(); ++i)
        if (shown[(size_t) i].file == keep) sel = i;
    scroll = 0.0f;
    ensureVisible (sel);
}

void PresetBrowser::textEditorTextChanged (juce::TextEditor& t)
{
    confirmOverwrite = false;
    if (&t == &search) filter();
    repaint();
}

void PresetBrowser::select (int row, bool load)
{
    if (row < 0 || row >= (int) shown.size()) return;
    sel = row;
    ensureVisible (row);
    if (load && presets::apply (dev, shown[(size_t) row]))
    {
        ctx.setReference (dev);
        ctx.markDirty();
        saveName.setText (dev.presetName, false);
        confirmOverwrite = false;
        if (onLoaded) onLoaded();
    }
    repaint();
}

void PresetBrowser::ensureVisible (int row)
{
    if (row < 0 || listR.isEmpty()) return;
    const float top = (float) row * kRow, h = listR.getHeight();
    if (top < scroll) scroll = top;
    else if (top + kRow > scroll + h) scroll = top + kRow - h;
}

void PresetBrowser::clickSave()
{
    const auto raw = saveName.getText().trim();
    if (raw.isEmpty()) { saveName.grabKeyboardFocus(); return; }
    const auto name = presets::sanitiseName (raw);
    if (presets::userPresetExists (dev.type(), name) && ! confirmOverwrite) { confirmOverwrite = true; repaint(); return; }
    confirmOverwrite = false;
    if (! presets::save (dev, name)) return;
    dev.presetName = name;
    ctx.setReference (dev);
    bank = "User";
    search.setText ({}, false);
    saveName.setText (name, false);
    reload();
    for (int i = 0; i < (int) shown.size(); ++i)
        if (shown[(size_t) i].name == name) select (i, false);
    if (onLoaded) onLoaded();
    repaint();
}

void PresetBrowser::close()
{
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
}

void PresetBrowser::resized()
{
    search.setBounds (16, 16, getWidth() - 32, 32);
    saveName.setBounds (16, getHeight() - 48, getWidth() - 32 - 238, 32);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (col::surface);
    fb.begin();
    paint::searchIcon (g, (float) getWidth() - 34, 32, 14, col::faint);
    // bank buttons
    chipR.clear();
    float x = 16, y = 58;
    for (auto& b : banks)
    {
        const float w = textWidth (b, 11) + 22;
        if (x + w > (float) getWidth() - 16) { x = 16; y += 30; }
        const bool on = bank == b;
        paint::pill (g, x, y, b, 11, on ? colour : col::control, on ? col::bg : col::quiet, 24, 11, on ? Weight::Black : Weight::Bold);
        chipR.push_back ({ { x, y, w, 24 }, b });
        x += w + 6;
    }
    const bool layoutChanged = listR.isEmpty();
    listR = { 8, y + 34, (float) getWidth() - 16, (float) getHeight() - y - 34 - 64 };
    if (layoutChanged) ensureVisible (sel);
    g.saveState();
    g.reduceClipRegion (listR.toNearestInt());
    for (size_t i = 0; i < shown.size(); ++i)
    {
        const float ry = listR.getY() + (float) i * kRow - scroll;
        if (ry + kRow < listR.getY() || ry > listR.getBottom()) continue;
        const auto& p = shown[i];
        const bool cur = p.name == dev.presetName, isSel = (int) i == sel;
        if (isSel || cur) { g.setColour (isSel ? col::control : col::control.withAlpha (0.5f)); g.fillRoundedRectangle (listR.getX(), ry + 1, listR.getWidth(), kRow - 2, 8); }
        paint::text (g, p.factory ? juce::String (p.index).paddedLeft ('0', 3) : juce::String ("USR"), listR.getX() + 12, ry + 20, 11, Weight::Bold,
                     cur ? colour : col::faint);
        paint::text (g, p.name + (p.shadowed ? " *" : ""), listR.getX() + 50, ry + 20, 13, cur ? Weight::Black : Weight::Bold, cur ? col::text : col::quiet);
        paint::text (g, p.family, listR.getRight() - 12, ry + 20, 11, Weight::Bold, p.factory ? col::faint : colour, juce::Justification::right);
        fb.hot (juce::Rectangle<float> (listR.getX(), ry + 1, listR.getWidth(), kRow - 2).getIntersection (listR), 8);
    }
    if (shown.empty())
        paint::text (g, bank == "User" ? "No user presets yet: name the sound below and press Save" : "Nothing matches", listR.getCentreX(),
                     listR.getY() + 40, 12, Weight::Bold, col::faint, juce::Justification::centred);
    g.restoreState();
    // footer: name + Save, Over (replace the selected factory preset), Del
    const float fy = (float) getHeight() - 46;
    const auto* s = selected();
    float fx = (float) getWidth() - 16 - 230;
    const float sw = paint::pill (g, fx, fy, confirmOverwrite ? "Overwrite?" : "Save", 12, confirmOverwrite ? col::mute : colour, col::bg, 28, 12,
                                  Weight::Black, false, juce::Justification::left, 92);
    saveR = { fx, fy, sw, 28 };
    fx += sw + 6;
    const bool canOver = s != nullptr && s->factory, canDel = s != nullptr && (! s->factory || s->shadowed);
    const float ow = paint::pill (g, fx, fy, "Over", 12, col::control, canOver ? col::text : col::faint, 28, 12, Weight::Bold, false,
                                  juce::Justification::left, 62);
    overR = canOver ? juce::Rectangle<float> (fx, fy, ow, 28) : juce::Rectangle<float>();
    fx += ow + 6;
    const float dw = paint::pill (g, fx, fy, "Del", 12, col::control, canDel ? col::text : col::faint, 28, 12, Weight::Bold, false,
                                  juce::Justification::left, 58);
    delR = canDel ? juce::Rectangle<float> (fx, fy, dw, 28) : juce::Rectangle<float>();
    for (auto& [r, id] : chipR) fb.hot (r, 12);
    fb.hot (saveR, 14);
    if (! overR.isEmpty()) fb.hot (overR, 14);
    if (! delR.isEmpty()) fb.hot (delR, 14);
    fb.paint (g);
}

void PresetBrowser::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.position;
    for (auto& [r, id] : chipR) if (r.contains (p)) { bank = id; filter(); repaint(); return; }
    if (listR.contains (p))
    {
        select ((int) ((p.y - listR.getY() + scroll) / kRow), true);
        return;
    }
    if (saveR.contains (p)) { clickSave(); return; }
    if (overR.contains (p))
    {
        if (auto* s = selected(); s != nullptr && presets::overwriteFactory (dev, *s))
        {
            dev.presetName = s->name;
            ctx.setReference (dev);
            reload();
            if (onLoaded) onLoaded();
            repaint();
        }
        return;
    }
    if (delR.contains (p))
    {
        if (auto* s = selected(); s != nullptr && presets::remove (*s))
        {
            reload();
            repaint();
        }
    }
}

void PresetBrowser::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (listR.contains (e.position)) close();
}

bool PresetBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::downKey) { select (std::min (sel + 1, (int) shown.size() - 1), true); return true; }
    if (k == juce::KeyPress::upKey) { select (std::max (sel - 1, 0), true); return true; }
    if (k == juce::KeyPress::returnKey) { close(); return true; }
    return false;
}

void PresetBrowser::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float maxScroll = std::max (0.0f, (float) shown.size() * kRow - listR.getHeight());
    scroll = juce::jlimit (0.0f, maxScroll, scroll - w.deltaY * 240.0f);
    repaint();
}
} // namespace mb::ui
