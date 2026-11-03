#include "Widgets.h"

namespace mb::ui
{
// ------------------------------------------------------------------------------------------- bindings
static LockLane* lockLaneFor (AppContext* ctx, int pat, int trk, int param, bool create)
{
    if (ctx == nullptr || pat < 0 || trk < 0) return nullptr;
    return ctx->engine.project().patterns[(size_t) pat].slots[(size_t) trk].track.lockFor (param, create);
}

float ParamBinding::getNorm() const
{
    if (ctx != nullptr && ctx->lockStep >= 0)
        if (auto* lane = lockLaneFor (ctx, pat, trk, idx, false))
        {
            const float v = lane->values[(size_t) ctx->lockStep];
            if (! std::isnan (v)) return dev->params()[idx].toNorm (v);
        }
    return dev->shownNorm (idx);   // automation (a playing step lock) moves the knob
}

void ParamBinding::setNorm (float n)
{
    const float value = dev->params()[idx].fromNorm (n);
    if (ctx != nullptr && ctx->lockStep >= 0 && pat >= 0 && trk >= 0)
    {
        // a selected step takes a parameter lock instead of changing the sound
        const Engine::ScopedEdit edit (ctx->engine);
        if (auto* lane = lockLaneFor (ctx, pat, trk, idx, true)) lane->values[(size_t) ctx->lockStep] = value;
        ctx->markDirty();
        return;
    }
    dev->set (idx, value);
    if (ctx != nullptr)
    {
        if (pat >= 0 && trk >= 0) ctx->engine.touchParam (pat, trk, idx, value);   // motion recording (while recording)
        ctx->markDirty();
    }
    if (onChange) onChange (idx, value);
}

juce::String ParamBinding::text() const { return dev->params()[idx].format (dev->params()[idx].fromNorm (getNorm())); }

int ParamBinding::steps() const
{
    const auto& p = dev->params()[idx];
    return p.stepped() ? (int) (p.max - p.min) + 1 : 0;
}

bool ParamBinding::locked() const
{
    if (ctx == nullptr || pat < 0 || trk < 0) return false;
    auto* lane = lockLaneFor (ctx, pat, trk, idx, false);
    return lane != nullptr && lane->any();
}

juce::String MacroBinding::text() const
{
    const auto& mm = dev->macros()[(size_t) macro];
    if (mm.count == 0) return {};
    const auto& p = dev->params()[mm.targets[0].param];
    return p.format (dev->pv (mm.targets[0].param));
}

// ------------------------------------------------------------------------------------------- knob
bool adjustWithKeys (Binding& b, const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    const bool fine = k.getModifiers().isShiftDown();
    const int s = b.steps();
    float step = s > 1 ? 1.0f / (float) (s - 1) : (fine ? 0.002f : 0.02f);
    if (code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey) step = s > 1 ? step * 4 : 0.1f;
    float dir = 0.0f;
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::pageUpKey) dir = 1.0f;
    else if (code == juce::KeyPress::downKey || code == juce::KeyPress::pageDownKey) dir = -1.0f;
    else if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { b.gestureBegin(); b.setNorm (b.defaultNorm()); b.gestureEnd(); return true; }
    else return false;
    b.gestureBegin();
    b.setNorm (juce::jlimit (0.0f, 1.0f, b.getNorm() + dir * step));
    b.gestureEnd();
    return true;
}

// ------------------------------------------------------------------------------------------- MIDI learn
LearnHooks& learnHooks() { static LearnHooks h; return h; }

void paintLearn (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& target, float radius)
{
    auto& h = learnHooks();
    if (! h.active || target.isEmpty()) return;
    const auto mapped = h.label ? h.label (target) : juce::String();
    g.setColour (col::pattern.withAlpha (mapped.isEmpty() ? 0.18f : 0.32f));
    g.fillRoundedRectangle (r, radius);
    if (h.isArmed && h.isArmed (target)) { g.setColour (col::pattern); g.drawRoundedRectangle (r, radius, 2.5f); }
    if (mapped.isNotEmpty())
    {
        const float w = textWidth (mapped, 9, Weight::Black) + 12;
        paint::pill (g, r.getCentreX() - w / 2, std::max (0.0f, r.getY() - 8), mapped, 9, col::pattern, col::bg, 16, 6, Weight::Black);
    }
}

KnobView::KnobView (std::unique_ptr<Binding> b, juce::String l, juce::Colour c, float r, bool sv)
    : radius (r), colour (c), label (std::move (l)), showValue (sv), bind (std::move (b))
{
    setRepaintsOnMouseActivity (false);
    fb.shading = false;   // the knob draws its own hover state
    fb.keyHandler = [this] (int, const juce::KeyPress& k) { return adjustWithKeys (*bind, k); };
}

void KnobView::paint (juce::Graphics& g)
{
    const float cx = getWidth() * 0.5f, cy = radius + 6.0f;
    const float v = bind->getNorm();
    lastPainted = v;
    paint::knob (g, cx, cy, radius, v, colour, bind->bipolar(), bind->locked());
    fb.begin();
    fb.hot ({ cx - radius - 4, cy - radius - 4, radius * 2 + 8, radius * 2 + 8 }, radius + 4);
    const bool live = dragging || hover || Feedback::navFocus().fb == &fb;   // a focused knob shows its value too
    const auto lbl = live && ! showValue ? bind->text() : label;
    paint::text (g, lbl, cx, cy + radius + 19.0f, labelSize, Weight::Bold, live && ! showValue ? col::text : col::quiet,
                 juce::Justification::horizontallyCentred);
    if (showValue)
        paint::text (g, bind->text(), cx, cy + radius + 33.0f, labelSize - 1.0f, Weight::Regular, live ? col::text : col::faint,
                     juce::Justification::horizontallyCentred);
    paintLearn (g, { cx - radius - 8, cy - radius - 8, radius * 2 + 16, radius * 2 + 16 }, bind->learnId, radius + 8);
    fb.paint (g);
}

void KnobView::refresh()
{
    if (std::abs (bind->getNorm() - lastPainted) > 1.0e-4f) repaint();
}

void KnobView::mouseDown (const juce::MouseEvent& e)
{
    learnPress = learnHooks().press (bind->learnId, e);
    if (learnPress) return;
    if (e.mods.isPopupMenu() && bind->hasAutomation())
    {
        // right-click: the recorded automation of this control goes (its step locks, its clip lane)
        juce::PopupMenu m;
        m.addItem (1, "Clear automation");
        learnPress = true;   // no drag from this press
        juce::Component::SafePointer<KnobView> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [self] (int r)
        {
            if (r == 1 && self != nullptr) { self->bind->clearAutomation(); self->repaint(); }
        });
        return;
    }
    startNorm = bind->getNorm();
    dragging = true;
    bind->gestureBegin();
    repaint();
}

void KnobView::mouseDrag (const juce::MouseEvent& e)
{
    if (learnPress) return;
    const float sens = e.mods.isShiftDown() ? 2000.0f : 220.0f;
    float n = juce::jlimit (0.0f, 1.0f, startNorm - (float) e.getDistanceFromDragStartY() / sens);
    if (const int s = bind->steps(); s > 1) n = std::round (n * (s - 1)) / (float) (s - 1);
    if (std::abs (n - bind->getNorm()) > 1.0e-6f) { bind->setNorm (n); repaint(); }
}

void KnobView::mouseUp (const juce::MouseEvent&)
{
    if (std::exchange (learnPress, false)) return;
    dragging = false;
    bind->gestureEnd();
    repaint();
}

void KnobView::mouseDoubleClick (const juce::MouseEvent&)
{
    if (learnHooks().active) return;
    bind->gestureBegin();
    bind->setNorm (bind->defaultNorm());
    bind->gestureEnd();
    repaint();
}

void KnobView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int s = bind->steps();
    const float step = s > 1 ? 1.0f / (float) (s - 1) : (e.mods.isShiftDown() ? 0.002f : 0.02f);
    const float dir = (w.deltaY + w.deltaX) > 0 ? 1.0f : -1.0f;
    bind->setNorm (juce::jlimit (0.0f, 1.0f, bind->getNorm() + dir * step));
    repaint();
}

// ------------------------------------------------------------------------------------------- choice
ChoiceView::ChoiceView (juce::StringArray opts, std::function<int()> get, std::function<void (int)> set, juce::Colour c)
    : options (std::move (opts)), colour (c), getter (std::move (get)), setter (std::move (set)) {}

void ChoiceView::paint (juce::Graphics& g)
{
    shown = getter();
    fb.begin();
    const auto segs = paint::segmented (g, getLocalBounds().toFloat(), options, shown, colour, textSize);
    if (segs.empty()) fb.hot (getLocalBounds().toFloat());
    for (auto& r : segs) fb.hot (r.reduced (2));
    fb.paint (g);
}

void ChoiceView::mouseUp (const juce::MouseEvent& e)
{
    const auto r = getLocalBounds().toFloat();
    if (! paint::segmentedFits (r.getWidth(), options, textSize))
    {
        juce::PopupMenu m;
        for (int i = 0; i < options.size(); ++i) m.addItem (i + 1, options[i], true, i == getter());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int id)
        {
            if (id > 0) { setter (id - 1); repaint(); }
        });
        return;
    }
    // hit-test the segments the same way they were painted
    float need = 0.0f;
    std::vector<float> widths;
    for (auto& o : options) { widths.push_back (textWidth (o, textSize) + 14.0f); need += widths.back(); }
    const float extra = (r.getWidth() - need) / (float) options.size();
    float x = 0.0f;
    for (int i = 0; i < options.size(); ++i)
    {
        const float w = widths[(size_t) i] + extra;
        if (e.position.x >= x && e.position.x < x + w) { setter (i); repaint(); return; }
        x += w;
    }
}

// ------------------------------------------------------------------------------------------- fader
void FaderView::paint (juce::Graphics& g)
{
    fb.shading = false;
    if (! fb.keyHandler) fb.keyHandler = [this] (int, const juce::KeyPress& k) { return adjustWithKeys (*bind, k); };
    fb.begin();
    fb.hot (getLocalBounds().toFloat().reduced (2), 8);
    const float x = 11.0f, w = 6.0f, top = 6.0f, h = (float) getHeight() - 12.0f;
    const float v = bind->getNorm();
    g.setColour (col::control);
    g.fillRoundedRectangle (x - w / 2, top, w, h, w / 2);
    g.setColour (colour.withAlpha (0.9f));
    g.fillRoundedRectangle (x - w / 2, top + h * (1 - v), w, h * v, w / 2);
    if (meter)
    {
        const auto [l, r] = meter();
        const float lv[2] = { juce::jlimit (0.0f, 1.0f, l), juce::jlimit (0.0f, 1.0f, r) };
        for (int i = 0; i < 2; ++i)
        {
            const float mx = x + 10.0f + (float) i * 5.0f;
            g.setColour (col::control);
            g.fillRoundedRectangle (mx, top, 3, h, 1.5f);
            g.setColour (colour.withAlpha (0.6f));
            g.fillRoundedRectangle (mx, top + h * (1 - lv[i]), 3, h * lv[i], 1.5f);
        }
    }
    g.setColour (col::text);
    g.fillRoundedRectangle (x - 11, top + h * (1 - v) - 5, 22, 10, 5);
    paintLearn (g, getLocalBounds().toFloat().reduced (1, 8), bind->learnId, 8);
    fb.paint (g);
}

void FaderView::mouseDown (const juce::MouseEvent& e)
{
    learnPress = learnHooks().press (bind->learnId, e);
    if (! learnPress) { startNorm = bind->getNorm(); bind->gestureBegin(); }
}

void FaderView::mouseDrag (const juce::MouseEvent& e)
{
    if (learnPress) return;
    const float h = (float) getHeight() - 12.0f;
    bind->setNorm (juce::jlimit (0.0f, 1.0f, startNorm - (float) e.getDistanceFromDragStartY() / h * (e.mods.isShiftDown() ? 0.1f : 1.0f)));
    repaint();
}

// ------------------------------------------------------------------------------------------- dialogs
void askForText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("t", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r)
    {
        if (r == 1) done (w->getTextEditorContents ("t").trim());
    }), true);
}
// ------------------------------------------------------------------------------------------- keyboard navigation
bool Feedback::handleKey (const juce::KeyPress& k, juce::Component& top)
{
    struct Target { Feedback* fb; int index; juce::Rectangle<float> local, inTop; };
    std::vector<Target> targets;
    for (auto* fb : registry())
    {
        // visible inside the window content (every parent up to `top` visible)
        bool visible = &top == &fb->view || top.isParentOf (&fb->view);
        for (auto* c = &fb->view; visible && c != nullptr && c != &top; c = c->getParentComponent()) visible = c->isVisible();
        if (! visible) continue;
        for (size_t i = 0; i < fb->hots.size(); ++i)
        {
            const auto& h = fb->hots[i];
            if (! (h.flags & kNav) || h.r.isEmpty()) continue;
            const auto inTop = top.getLocalArea (&fb->view, h.r);
            if (! top.getLocalBounds().toFloat().intersects (inTop)) continue;
            targets.push_back ({ fb, (int) i, h.r, inTop });
        }
    }
    // reading order: rows (within 14 px), then left to right
    std::stable_sort (targets.begin(), targets.end(), [] (const Target& a, const Target& b)
    {
        const int ra = (int) (a.inTop.getCentreY() / 14.0f), rb = (int) (b.inTop.getCentreY() / 14.0f);
        return ra != rb ? ra < rb : a.inTop.getX() < b.inTop.getX();
    });
    int cur = -1;
    if (auto* f = navFocus().fb)
    {
        const int i = f->nearestHot (navFocus().r);
        for (size_t t = 0; t < targets.size(); ++t) if (targets[t].fb == f && targets[t].index == i) cur = (int) t;
        if (cur < 0) navFocus() = {};
    }
    auto focusOn = [&] (int t)
    {
        if (auto* old = navFocus().fb) old->view.repaint();
        navFocus() = { targets[(size_t) t].fb, targets[(size_t) t].local };
        targets[(size_t) t].fb->view.repaint();
    };
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::tabKey)
    {
        if (targets.empty()) return true;
        const bool back = k.getModifiers().isShiftDown();
        focusOn (cur < 0 ? (back ? (int) targets.size() - 1 : 0) : (cur + (back ? -1 : 1) + (int) targets.size()) % (int) targets.size());
        return true;
    }
    if (cur < 0) return false;
    auto& t = targets[(size_t) cur];
    if (t.fb->keyHandler && t.fb->keyHandler (t.index, k)) { t.fb->view.repaint(); return true; }
    if (code == juce::KeyPress::escapeKey) { clearFocus(); return true; }
    // the mouse by keys (docs/keyboard.md): Enter clicks (Shift / Alt / Ctrl+Enter: that modified click), Shift+F10 or
    // Delete right-clicks (clear, context menus), F2 double-clicks
    const auto mods = k.getModifiers();
    const bool rightClick = (code == juce::KeyPress::F10Key && mods.isShiftDown()) || code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey;
    if (code == juce::KeyPress::returnKey || rightClick || code == juce::KeyPress::F2Key)
    {
        const auto c = t.local.getCentre();
        const auto now = juce::Time::getCurrentTime();
        const auto clickMods = rightClick ? juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)
                                          : juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | (mods.getRawFlags() & (juce::ModifierKeys::shiftModifier | juce::ModifierKeys::altModifier | juce::ModifierKeys::commandModifier)));
        const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), c, clickMods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  &t.fb->view, &t.fb->view, now, c, now, code == juce::KeyPress::F2Key ? 2 : 1, false);
        juce::Component::SafePointer<juce::Component> v (&t.fb->view);
        t.fb->view.mouseDown (e);
        if (v != nullptr && code == juce::KeyPress::F2Key) v->mouseDoubleClick (e);
        if (v != nullptr) v->mouseUp (e);
        return true;
    }
    const int dx = code == juce::KeyPress::leftKey ? -1 : (code == juce::KeyPress::rightKey ? 1 : 0);
    const int dy = code == juce::KeyPress::upKey ? -1 : (code == juce::KeyPress::downKey ? 1 : 0);
    if (dx == 0 && dy == 0) return false;
    // spatial: the nearest control in that direction (straighter beats closer)
    const auto from = t.inTop.getCentre();
    int best = -1; float bestScore = 1.0e9f;
    for (size_t i = 0; i < targets.size(); ++i)
    {
        if ((int) i == cur) continue;
        const auto d = targets[i].inTop.getCentre() - from;
        const float along = dx != 0 ? d.x * dx : d.y * dy, across = std::abs (dx != 0 ? d.y : d.x);
        if (along <= 1.0f) continue;
        const float score = along + across * 2.5f;
        if (score < bestScore) { bestScore = score; best = (int) i; }
    }
    if (best >= 0) focusOn (best);
    return true;
}

} // namespace mb::ui
