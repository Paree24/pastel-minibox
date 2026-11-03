#pragma once
#include "Paint.h"
#include "AppContext.h"

namespace mb::ui
{
/** Hover and press feedback for custom-painted controls. Attach it to a view (it listens to the view's mouse
    events itself); the view registers each clickable rectangle while it paints (`hot`) and calls `paint` last.
    The control under the mouse brightens and shows a pointing hand; the pressed one dims and sinks a pixel. */
class Feedback : private juce::MouseListener
{
public:
    explicit Feedback (juce::Component& v) : view (v) { view.addMouseListener (this, false); registry().push_back (this); }
    ~Feedback() override
    {
        view.removeMouseListener (this);
        auto& r = registry();
        r.erase (std::remove (r.begin(), r.end(), this), r.end());
        if (navFocus().fb == this) navFocus() = {};
    }
    void begin() { hots.clear(); }
    /** Optional: a cursor for a point (e.g. a resize arrow on a note's end); the default is a pointing hand over controls. */
    std::function<juce::MouseCursor (juce::Point<float>)> cursorFor;
    /** Keyboard: keys for the focused control of this view first (knobs adjust, the piano roll moves its cursor). */
    std::function<bool (int hotIndex, const juce::KeyPress&)> keyHandler;
    /** Hover / press shading (off for controls that draw their own, like knobs); the focus ring always shows. */
    bool shading = true;
    enum Flags { kHover = 1, kNav = 2, kBoth = 3 };
    void hot (juce::Rectangle<float> r, float radius = -1.0f, int flags = kBoth)
    {
        hots.push_back ({ r, radius < 0.0f ? std::min (8.0f, std::min (r.getWidth(), r.getHeight()) / 2) : radius, flags });
    }
    void paint (juce::Graphics& g)
    {
        if (shading)
        {
            if (! pressed.r.isEmpty()) { g.setColour (juce::Colours::black.withAlpha (0.28f)); g.fillRoundedRectangle (pressed.r.translated (0, 1), pressed.radius); }
            else if (! hover.r.isEmpty()) { g.setColour (juce::Colours::white.withAlpha (0.07f)); g.fillRoundedRectangle (hover.r, hover.radius); }
        }
        // keyboard focus ring (the stored rectangle follows the control if the layout moved)
        if (navFocus().fb == this)
        {
            const int i = nearestHot (navFocus().r);
            if (i >= 0)
            {
                navFocus().r = hots[(size_t) i].r;
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawRoundedRectangle (hots[(size_t) i].r.expanded (2.5f), hots[(size_t) i].radius + 2.5f, 2.0f);
            }
        }
    }

    // ---- keyboard navigation (docs/keyboard.md): every visible Feedback's navigable rectangles are focus targets
    struct Hot { juce::Rectangle<float> r; float radius = 0.0f; int flags = kBoth; };
    struct Focus { Feedback* fb = nullptr; juce::Rectangle<float> r; };
    static std::vector<Feedback*>& registry() { static std::vector<Feedback*> r; return r; }
    static Focus& navFocus() { static Focus f; return f; }
    /** Handles Tab, arrows, Enter and Esc for keyboard focus; `top` is the window content. True when used. */
    static bool handleKey (const juce::KeyPress&, juce::Component& top);
    static void clearFocus() { if (auto* f = navFocus().fb) f->view.repaint(); navFocus() = {}; }
    static bool hasFocus() { return navFocus().fb != nullptr; }
    int nearestHot (juce::Rectangle<float> r) const
    {
        int best = -1; float bd = 1.0e9f;
        for (size_t i = 0; i < hots.size(); ++i)
        {
            if (! (hots[i].flags & kNav)) continue;
            const float d = hots[i].r.getCentre().getDistanceFrom (r.getCentre());
            if (d < bd) { bd = d; best = (int) i; }
        }
        return bd < 40.0f ? best : -1;
    }
    juce::Component& getView() { return view; }
    const std::vector<Hot>& getHots() const { return hots; }
    /** The control under a point (the last registered wins), or a small area there. */
    juce::Rectangle<float> hotAt (juce::Point<float> p) const
    {
        for (auto it = hots.rbegin(); it != hots.rend(); ++it) if (it->r.contains (p)) return it->r;
        return { p.x - 1, p.y - 1, 2, 2 };
    }

private:
    Hot find (juce::Point<float> p) const { for (auto it = hots.rbegin(); it != hots.rend(); ++it) if ((it->flags & kHover) && it->r.contains (p)) return *it; return {}; }
    void setHover (Hot h)
    {
        if (h.r == hover.r) return;
        hover = h;
        view.setMouseCursor (h.r.isEmpty() ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
        view.repaint();
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != &view) return;
        setHover (find (e.position));
        if (cursorFor) view.setMouseCursor (cursorFor (e.position));
    }
    void mouseExit (const juce::MouseEvent& e) override { if (e.eventComponent == &view) { pressed = {}; setHover ({}); view.repaint(); } }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != &view) return;
        pressed = find (e.position);
        if (hasFocus()) clearFocus();   // the mouse takes over from the keyboard
        view.repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override { if (e.eventComponent == &view && ! pressed.r.isEmpty()) { pressed = {}; view.repaint(); } }
    juce::Component& view;
    std::vector<Hot> hots;
    Hot hover, pressed;
};

/** MIDI learn (docs/midi-learn.md): set by the standalone while learn mode is on. Learnable controls are
    tinted, show the message mapped to them, arm on a click and drop their mapping on a right-click. */
struct LearnHooks
{
    bool active = false;
    std::function<bool (const juce::String& target)> isArmed;
    std::function<void (const juce::String& target)> arm, remove;
    std::function<juce::String (const juce::String& target)> label;   // the mapped message ("CC 74"), or empty
    /** In learn mode a press on a learnable control arms it (right-click removes) instead of using it. */
    bool press (const juce::String& target, const juce::MouseEvent& e)
    {
        if (! active || target.isEmpty()) return false;
        if (e.mods.isPopupMenu()) { if (remove) remove (target); }
        else if (arm) arm (target);
        return true;
    }
};
LearnHooks& learnHooks();
/** The learn tint, armed ring and mapped message over a learnable control's area. */
void paintLearn (juce::Graphics&, juce::Rectangle<float> area, const juce::String& target, float radius);

/** A popup menu that opens at a control (an area in `c`), not at the whole view's corner or the mouse. */
inline juce::PopupMenu::Options menuAt (juce::Component& c, juce::Rectangle<float> local)
{
    return juce::PopupMenu::Options().withTargetComponent (&c).withTargetScreenArea (c.localAreaToGlobal (local).getSmallestIntegerContainer());
}

/** What a knob controls: a device parameter, a macro, a mixer or master value. */
struct Binding
{
    virtual ~Binding() = default;
    /** The MIDI learn target this control stands for (empty: not learnable). */
    juce::String learnId;
    virtual float getNorm() const = 0;
    virtual void setNorm (float) = 0;
    virtual float defaultNorm() const = 0;
    virtual juce::String text() const = 0;
    virtual bool bipolar() const { return false; }
    virtual int steps() const { return 0; }          // discrete positions, 0 = continuous
    virtual bool locked() const { return false; }
    virtual void gestureBegin() {}
    virtual void gestureEnd() {}
    /** Recorded automation on this control (step locks, a clip lane): the knob's right-click clears it. */
    virtual bool hasAutomation() const { return false; }
    virtual void clearAutomation() {}
};

/** A device parameter. Writes a parameter lock instead when a lock step is selected, and records
    locks while the transport records. */
struct ParamBinding : Binding
{
    ParamBinding (AppContext* c, DeviceProcessor* d, int index, int pattern = -1, int track = -1)
        : ctx (c), dev (d), idx (index), pat (pattern), trk (track) {}
    float getNorm() const override;
    void setNorm (float n) override;
    float defaultNorm() const override { return dev->params()[idx].toNorm (dev->params()[idx].def); }
    juce::String text() const override;
    bool bipolar() const override { return dev->params()[idx].bipolar; }
    int steps() const override;
    bool locked() const override;
    bool hasAutomation() const override { return ctx != nullptr && pat >= 0 && trk >= 0 && ctx->engine.hasAutomation (pat, trk, idx); }
    void clearAutomation() override { if (hasAutomation()) { ctx->engine.clearAutomation (pat, trk, idx); ctx->markDirty(); } }
    std::function<void (int, float)> onChange;   // plugin: host notification
    AppContext* ctx; DeviceProcessor* dev; int idx, pat, trk;
};

struct MacroBinding : Binding
{
    MacroBinding (AppContext* c, DeviceProcessor* d, int m) : ctx (c), dev (d), macro (m) {}
    float getNorm() const override { return dev->shownMacro (macro); }   // follows automation
    void setNorm (float n) override { dev->setMacro (macro, n); if (ctx) { ctx->engine.recordMacro (dev, macro, n); ctx->markDirty(); } }
    float defaultNorm() const override { return 0.5f; }
    juce::String text() const override;
    bool hasAutomation() const override { return ctx != nullptr && ctx->engine.hasMacroAutomation (dev, macro); }
    void clearAutomation() override { if (ctx != nullptr) { ctx->engine.clearMacroAutomation (dev, macro); ctx->markDirty(); } }
    AppContext* ctx; DeviceProcessor* dev; int macro;
};

/** A plain float (mixer, master): linear range with a formatter. */
struct FloatBinding : Binding
{
    FloatBinding (float* v, float mn, float mx, float def, std::function<juce::String (float)> fmt, bool bi = false, AppContext* c = nullptr)
        : value (v), lo (mn), hi (mx), dflt (def), format (std::move (fmt)), bip (bi), ctx (c) {}
    float getNorm() const override { return (*value - lo) / (hi - lo); }
    void setNorm (float n) override { *value = lo + (hi - lo) * juce::jlimit (0.0f, 1.0f, n); if (ctx) ctx->markDirty(); if (onChange) onChange (*value); }
    float defaultNorm() const override { return (dflt - lo) / (hi - lo); }
    juce::String text() const override { return format (*value); }
    bool bipolar() const override { return bip; }
    float* value; float lo, hi, dflt; std::function<juce::String (float)> format; bool bip; AppContext* ctx;
    std::function<void (float)> onChange;
};

/** Custom-painted knob: vertical drag (Shift = fine), wheel, double-click resets. */
class KnobView : public juce::Component, public juce::SettableTooltipClient
{
public:
    KnobView (std::unique_ptr<Binding> b, juce::String label, juce::Colour c, float radius, bool showValue = false);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    /** Repaints if the bound value changed elsewhere (presets, macros, automation). */
    void refresh();
    Binding& binding() { return *bind; }
    float radius;
    juce::Colour colour;
    juce::String label;
    bool showValue;
    float labelSize = 11.0f;

private:
    std::unique_ptr<Binding> bind;
    float startNorm = 0.0f, lastPainted = -1.0f;
    bool dragging = false, hover = false, learnPress = false;
    Feedback fb { *this };   // keyboard focus (docs/keyboard.md): ↑/↓ adjust, Page Up/Down large steps, Delete resets
};

/** Keys for a focused knob or fader: ↑/↓ a step (Shift fine), Page Up/Down large steps, Delete / Backspace resets. */
bool adjustWithKeys (Binding&, const juce::KeyPress&);

/** Choice pill bound to a choice / stepped parameter, or to a UI value through get/set functions. */
class ChoiceView : public juce::Component
{
public:
    ChoiceView (juce::StringArray opts, std::function<int()> get, std::function<void (int)> set, juce::Colour c);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh() { if (getter() != shown) repaint(); }
    juce::StringArray options;
    juce::Colour colour;
    float textSize = 11.0f;
private:
    std::function<int()> getter;
    std::function<void (int)> setter;
    int shown = -1;
    Feedback fb { *this };
};

/** Generic clickable component painted by a lambda (with hover / press feedback). */
class PaintedButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (juce::Graphics&, juce::Rectangle<float>, bool over, bool down)> painter;
    std::function<void (const juce::MouseEvent&)> onClick;
    void paint (juce::Graphics& g) override
    {
        if (painter) painter (g, getLocalBounds().toFloat(), isMouseOver(), isMouseButtonDown());
        const auto r = getLocalBounds().toFloat();
        const float rad = std::min (r.getWidth(), r.getHeight()) / 2;
        if (isMouseButtonDown()) { g.setColour (juce::Colours::black.withAlpha (0.28f)); g.fillRoundedRectangle (r.translated (0, 1), rad); }
        else if (isMouseOver()) { g.setColour (juce::Colours::white.withAlpha (0.07f)); g.fillRoundedRectangle (r, rad); }
    }
    void mouseUp (const juce::MouseEvent& e) override { if (onClick && contains (e.getPosition())) onClick (e); repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override { repaint(); }
};

/** Vertical fader with an optional stereo meter. */
class FaderView : public juce::Component
{
public:
    FaderView (std::unique_ptr<Binding> b, juce::Colour c) : bind (std::move (b)), colour (c) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { if (! learnHooks().active) { bind->setNorm (bind->defaultNorm()); repaint(); } }
    std::function<std::pair<float, float>()> meter;
    Binding& binding() { return *bind; }
    void refresh() { repaint(); }
private:
    std::unique_ptr<Binding> bind;
    juce::Colour colour;
    float startNorm = 0.0f;
    bool learnPress = false;
    Feedback fb { *this };
};

/** Text-entry modal used for renames and preset names. */
void askForText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done);
} // namespace mb::ui
