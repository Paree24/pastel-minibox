#include "PluginEditor.h"
#include "ui/PresetBrowser.h"

namespace mb
{
using namespace ui;
using namespace ui::geo;

namespace
{
const DeviceType kOrder[] = { DeviceType::Mono, DeviceType::Poly, DeviceType::Acid, DeviceType::FM, DeviceType::Sampler,
                              DeviceType::Drums, DeviceType::DrumSampler };
}

// ------------------------------------------------------------------------------------------- top bar
PluginHeader::PluginHeader (MiniboxProcessor& p) : proc (p) {}

void PluginHeader::refresh()
{
    const int t = proc.device() != nullptr ? (int) proc.device()->type() : -1;
    if (t != shownType) { shownType = t; repaint(); }
}

void PluginHeader::paint (juce::Graphics& g)
{
    auto* d = proc.device();
    g.setColour (col::divider); g.drawHorizontalLine (TOP - 1, 0, W);
    paint::brandMark (g, M, 22);
    paint::text (g, "Minibox", M + 32, 39, 20, Weight::Black, col::text);
    tabs.clear();
    float x = 176;
    for (auto t : kOrder)
    {
        const bool sel = d != nullptr && d->type() == t;
        const float w = paint::pill (g, x, 18, deviceTypeName (t), 12, sel ? col::text : col::control, sel ? col::bg : col::quiet, 28, 12,
                                     sel ? Weight::Black : Weight::Bold);
        tabs.push_back ({ { x, 18, w, 28 }, t });
        x += w + 6;
    }
    fb.begin();
    for (auto& [r, t] : tabs) fb.hot (r, 14);
    fb.paint (g);
}

void PluginHeader::mouseUp (const juce::MouseEvent& e)
{
    for (auto& [r, t] : tabs)
        if (r.contains (e.position))
        {
            if (proc.device() == nullptr || proc.device()->type() != t)
            {
                proc.switchInstrument (t);
                if (onSwitched) onSwitched();
            }
            repaint();
            return;
        }
}

// ------------------------------------------------------------------------------------------- MIDI row
void MidiRow::refresh()
{
    juce::String s;
    if (auto* d = proc.device()) { s << (int) d->type(); for (auto& w : d->heldNotes) s << (int) w.load(); }
    s << juce::roundToInt (proc.engine().project().bpm * 10) << (int) (proc.engine().hostBeatPosition() * 4);
    if (s != sig) { sig = s; repaint(); }
}

void MidiRow::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    paint::card (g, r);
    auto* d = proc.device();
    const auto colour = d != nullptr ? deviceColour (d->type()) : col::track (0);
    const float h = r.getHeight();
    g.setColour (colour); g.fillEllipse (24, h / 2 - 6, 12, 12);
    paint::text (g, "MIDI", 46, h / 2 + 5, 12, Weight::Black, col::quiet);
    paint::text (g, "Omni", 96, h / 2 + 5, 12, Weight::Bold, col::faint);
    auto lit = [d] (int n) { return d != nullptr && ((d->heldNotes[(size_t) (n >> 5)].load() >> (n & 31)) & 1u); };
    const juce::Rectangle<float> kb (170, 16, r.getWidth() - 430, h - 32);
    std::vector<int> whites;
    for (int n = 36; n <= 108 && (int) whites.size() < 36; ++n) if (! juce::MidiMessage::isMidiNoteBlack (n)) whites.push_back (n);
    const float kw = kb.getWidth() / (float) whites.size();
    for (size_t i = 0; i < whites.size(); ++i)
    {
        g.setColour (lit (whites[i]) ? colour : col::control);
        g.fillRoundedRectangle (kb.getX() + i * kw + 1, kb.getY(), kw - 2, kb.getHeight(), 6);
    }
    for (size_t i = 0; i + 1 < whites.size(); ++i)
        if (whites[i + 1] - whites[i] == 2)
        {
            g.setColour (lit (whites[i] + 1) ? colour.darker (0.25f) : col::bg);
            g.fillRoundedRectangle (kb.getX() + (i + 1) * kw - kw * 0.3f, kb.getY(), kw * 0.6f, kb.getHeight() * 0.58f, 4);
        }
    paint::text (g, "HOST", r.getWidth() - 230, h / 2 - 4, 10, Weight::Bold, col::faint);
    paint::text (g, juce::String (proc.engine().project().bpm, 1) + " BPM", r.getWidth() - 230, h / 2 + 16, 15, Weight::Black, col::text);
    // host position as bar.beat.sixteenth (4/4)
    const double beats = std::max (0.0, proc.engine().hostBeatPosition());
    const int bar = (int) (beats / 4.0) + 1, beat = (int) std::fmod (beats, 4.0) + 1, six = (int) (std::fmod (beats, 1.0) * 4.0) + 1;
    paint::text (g, "BAR", r.getWidth() - 110, h / 2 - 4, 10, Weight::Bold, col::faint);
    paint::text (g, juce::String (bar) + "." + juce::String (beat) + "." + juce::String (six), r.getWidth() - 110, h / 2 + 16, 15, Weight::Black, col::text);
}

// ------------------------------------------------------------------------------------------- editor
MiniboxEditor::MiniboxEditor (MiniboxProcessor& p)
    : AudioProcessorEditor (p), proc (p), ctx (p.engine(), false), topBar (p), header (ctx), midiRow (p), midiBtn (p.midiLearn())
{
    setLookAndFeel (&laf);
    setWantsKeyboardFocus (true);
    addAndMakeVisible (content);
    content.addAndMakeVisible (topBar);
    content.addAndMakeVisible (header);
    content.addAndMakeVisible (midiRow);
    content.setSize (W, H);
    // same coordinates as the standalone device editor (desktop.py editor_screen)
    const int stripH = 120;
    topBar.setBounds (0, 0, W, TOP);
    header.setBounds (0, TOP + 8, W, 52);
    midiRow.setBounds (M, H - M - stripH, W - 2 * M, stripH);
    // MIDI learn at the right end of the top bar, the height of the instrument tabs
    content.addAndMakeVisible (midiBtn);
    midiBtn.setBounds (W - M - 84, 18, 84, 28);
    midiBtn.setColour (juce::TextButton::buttonOnColourId, col::pattern);
    midiBtn.setColour (juce::TextButton::textColourOnId, col::pattern);
    proc.presetStepInEditor = [this] (int delta)
    {
        if (auto* d = proc.device()) { presetActions::step (ctx, *d, delta); header.repaint(); }
    };
    ctx.showDevicePicker = [this] (int, juce::Component* anchor) { showInstrumentMenu (anchor); };
    ctx.showPresetBrowser = [this] (DeviceProcessor* d, juce::Component* anchor) { PresetBrowser::show (ctx, *d, anchor, [this] { header.repaint(); }); };
    topBar.onSwitched = [this] { attachDevice(); };
    attachDevice();
    setResizable (true, true);
    setResizeLimits (960, 600, 2400, 1500);
    getConstrainer()->setFixedAspectRatio (1.6);
    setSize (1280, 800);
    startTimerHz (30);
}

MiniboxEditor::~MiniboxEditor()
{
    stopTimer();
    proc.presetStepInEditor = nullptr;
    setLookAndFeel (nullptr);
}

void MiniboxEditor::showInstrumentMenu (juce::Component* anchor)
{
    juce::PopupMenu m;
    auto* cur = proc.device();
    for (auto t : kOrder) m.addItem (1 + (int) t, deviceTypeName (t), true, cur != nullptr && cur->type() == t);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor), [this] (int id)
    {
        if (id <= 0) return;
        if (proc.device() == nullptr || (int) proc.device()->type() != id - 1) { proc.switchInstrument ((DeviceType) (id - 1)); attachDevice(); }
    });
}

void MiniboxEditor::attachDevice()
{
    shown = proc.device();
    body = std::make_unique<EditorBody> (ctx, shown ? deviceColour (shown->type()) : col::track (0));
    content.addAndMakeVisible (*body);
    body->setBounds (M, TOP + 72, W - 2 * M, H - M - 120 - GAP - (TOP + 72));
    body->setDevice (shown, -1, -1);
    if (shown != nullptr)
    {
        ctx.setReference (*shown);
        for (int m = 0; m < 8; ++m) uiMacro[(size_t) m] = shown->getMacro (m);
    }
    header.repaint();
    midiRow.repaint();
    topBar.repaint();
}

void MiniboxEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

bool MiniboxEditor::keyPressed (const juce::KeyPress& k) { return ui::Feedback::handleKey (k, content); }

void MiniboxEditor::resized()
{
    const float s = std::min (getWidth() / (float) W, getHeight() / (float) H);
    content.setTransform (juce::AffineTransform::scale (s).translated ((getWidth() - W * s) / 2.0f, (getHeight() - H * s) / 2.0f));
}

void MiniboxEditor::timerCallback()
{
    if (proc.device() != shown) attachDevice();
    topBar.refresh();
    header.refresh();
    if (body) body->refresh();
    midiRow.refresh();
    // knob edits that move a macro are reported to the host
    if (shown != nullptr)
        for (int m = 0; m < 8; ++m)
        {
            const float v = shown->getMacro (m);
            if (std::abs (v - uiMacro[(size_t) m]) > 1.0e-4f) { uiMacro[(size_t) m] = v; proc.macroChangedByUi (m, v); }
        }
}
} // namespace mb
