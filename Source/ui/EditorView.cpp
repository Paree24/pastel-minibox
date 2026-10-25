#include "EditorView.h"
#include "app/presets/PresetBank.h"
#include "devices/sampler/SamplerDevice.h"
#include "devices/drumsampler/DrumSamplerDevice.h"
#include "devices/fm/FmDevice.h"

namespace mb::ui
{
using namespace geo;

namespace
{
juce::String resolveId (const DeviceProcessor& d, const KnobSpec& k)
{
    if (k.altWhen.isNotEmpty() && d.get (k.altWhen) > 0.5f) return k.altId;
    return k.id;
}

juce::String categoryOf (const DeviceProcessor& d)
{
    if (auto* f = presets::findFactory (d)) return f->family;
    return presets::userPresetExists (d.type(), d.presetName) ? juce::String ("User") : juce::String();
}

std::unique_ptr<juce::FileChooser>& chooser() { static std::unique_ptr<juce::FileChooser> c; return c; }
} // namespace

// ------------------------------------------------------------------------------------------- section card
SectionCard::SectionCard (AppContext& c, DeviceProcessor& d, const SectionSpec& s, juce::Colour colr, int pattern, int track,
                          std::function<void (int, float)> onParamChange)
    : ctx (c), dev (d), spec (s), colour (colr)
{
    for (auto& k : spec.knobs)
    {
        const int idx = dev.params().indexOf (resolveId (dev, k));
        if (idx < 0) continue;
        auto b = std::make_unique<ParamBinding> (&ctx, &dev, idx, pattern, track);
        b->onChange = onParamChange;
        if (track >= 0) b->learnId = "param:" + juce::String (track) + ":" + dev.params()[idx].id;
        auto kv = std::make_unique<KnobView> (std::move (b), k.label, colour, 20.0f);
        addAndMakeVisible (*kv);
        knobs.push_back (std::move (kv));
    }
    if (spec.choiceId == "ui:lane")
    {
        choice = std::make_unique<ChoiceView> (juce::StringArray { "Vel", "Prob", "Ratchet" }, [this] { return ctx.laneView; },
                                               [this] (int i) { ctx.laneView = i; if (visual) visual->repaint(); }, colour);
        addAndMakeVisible (*choice);
    }
    else if (spec.choiceId.isNotEmpty())
    {
        const int idx = dev.params().indexOf (spec.choiceId);
        if (idx >= 0)
        {
            const auto& info = dev.params()[idx];
            juce::StringArray opts = info.choices;
            if (opts.isEmpty()) for (int i = (int) info.min; i <= (int) info.max; ++i) opts.add (info.format ((float) i));
            choice = std::make_unique<ChoiceView> (opts, [this, idx] { return (int) (dev.get (idx) - dev.params()[idx].min); },
                                                   [this, idx, onParamChange] (int i)
                                                   {
                                                       const float val = dev.params()[idx].min + (float) i;
                                                       dev.set (idx, val);
                                                       ctx.markDirty();
                                                       if (onParamChange) onParamChange (idx, val);
                                                   }, colour);
            addAndMakeVisible (*choice);
        }
    }
    if (spec.toggleId.isNotEmpty())
    {
        const int idx = dev.params().indexOf (spec.toggleId);
        if (idx >= 0)
        {
            toggle = std::make_unique<PaintedButton>();
            toggle->painter = [this, idx] (juce::Graphics& g, juce::Rectangle<float> r, bool, bool)
            {
                const bool on = dev.get (idx) > 0.5f;
                paint::pill (g, r.getX(), r.getY(), spec.toggleLabel, 10, on ? colour : col::control, on ? col::bg : col::quiet, r.getHeight(),
                             9, on ? Weight::Black : Weight::Bold, false, juce::Justification::left, r.getWidth());
            };
            toggle->onClick = [this, idx, onParamChange] (const juce::MouseEvent&)
            {
                dev.set (idx, dev.get (idx) > 0.5f ? 0.0f : 1.0f);
                ctx.markDirty();
                if (onParamChange) onParamChange (idx, dev.get (idx));
            };
            addAndMakeVisible (*toggle);
        }
    }
    if (spec.visual.kind != VisualKind::None)
    {
        visual = std::make_unique<VisualView> (ctx, dev, spec.visual, colour, pattern, track);
        addAndMakeVisible (*visual);
    }
}

void SectionCard::paint (juce::Graphics& g)
{
    paint::card (g, getLocalBounds().toFloat(), spec.title);
}

void SectionCard::resized()
{
    const float w = (float) getWidth(), h = (float) getHeight();
    if (toggle)
    {
        const float tw = textWidth (spec.toggleLabel, 10, Weight::Black) + 18;
        toggle->setBounds (juce::Rectangle<float> (w - 14 - tw, 10, tw, 22).toNearestInt());
    }
    float cy = 42.0f;
    const float ix = 14.0f, iw = w - 28.0f;
    if (choice) { choice->setBounds (juce::Rectangle<float> (ix, cy, iw, 26).toNearestInt()); cy += 38.0f; }
    const int n = (int) knobs.size();
    const int cols = n > 0 ? std::max (1, std::min (n, (int) ((w - 12) / 86))) : 1;
    const int rows = n > 0 ? (n + cols - 1) / cols : 0;
    const float kh = rows * 84.0f;
    if (visual)
    {
        const float vh = (h - 14) - cy - kh - (n > 0 ? 6.0f : 0.0f);
        visual->setBounds (juce::Rectangle<float> (ix, cy, iw, vh).toNearestInt());
        cy += vh + 6.0f;
    }
    else cy += ((h - 14) - cy - kh) / 2.0f;
    const float cellw = iw / cols;
    for (int i = 0; i < n; ++i)
    {
        const int r = i / cols, c = i % cols;
        const int inRow = std::min (cols, n - r * cols);
        const float x0 = ix + (iw - inRow * cellw) / 2.0f;
        const float kx = x0 + cellw * (c + 0.5f), ky = cy + r * 84.0f + 32.0f;
        const float bw = std::min (cellw, 86.0f);
        knobs[(size_t) i]->setBounds (juce::Rectangle<float> (kx - bw / 2, ky - 26.0f, bw, 72.0f).toNearestInt());
    }
}

void SectionCard::refresh()
{
    for (auto& k : knobs) k->refresh();
    if (choice) choice->refresh();
    if (toggle) toggle->repaint();
    if (visual) visual->refresh();
}

// ------------------------------------------------------------------------------------------- body
EditorBody::EditorBody (AppContext& c, juce::Colour colr) : ctx (c), colour (colr) {}

void EditorBody::setDevice (DeviceProcessor* d, int p, int t)
{
    dev = d; pattern = p; track = t;
    builtKey.clear();
    rebuild();
}

juce::String EditorBody::layoutKey() const
{
    if (dev == nullptr) return {};
    juce::String k = juce::String::toHexString ((juce::pointer_sized_int) dev);
    const auto s = editorSpecFor (*dev, ctx.selection (dev));
    for (auto& row : s.rows)
        for (auto& sec : row)
        {
            k << "|" << sec.title << ":" << sec.choiceId;
            for (auto& kn : sec.knobs) k << "," << resolveId (*dev, kn);
        }
    return k;
}

void EditorBody::rebuild()
{
    const auto key = layoutKey();
    if (key == builtKey) return;
    builtKey = key;
    cards.clear();
    if (dev == nullptr) return;
    spec = editorSpecFor (*dev, ctx.selection (dev));
    for (auto& row : spec.rows)
        for (auto& sec : row)
        {
            auto c = std::make_unique<SectionCard> (ctx, *dev, sec, colour, pattern, track, onParamChange);
            addAndMakeVisible (*c);
            cards.push_back (std::move (c));
        }
    resized();
}

void EditorBody::resized()
{
    if (spec.rows.empty()) return;
    const float w = (float) getWidth(), h = (float) getHeight();
    const float rh = (h - GAP * ((float) spec.rows.size() - 1)) / (float) spec.rows.size();
    size_t ci = 0;
    for (size_t ri = 0; ri < spec.rows.size(); ++ri)
    {
        const auto& row = spec.rows[ri];
        float tot = 0.0f;
        for (auto& s : row) tot += s.weight;
        const float unit = (w - GAP * ((float) row.size() - 1)) / tot;
        float x = 0.0f;
        for (auto& s : row)
        {
            if (ci < cards.size()) cards[ci++]->setBounds (juce::Rectangle<float> (x, ri * (rh + GAP), unit * s.weight, rh).toNearestInt());
            x += unit * s.weight + GAP;
        }
    }
}

void EditorBody::refresh()
{
    if (layoutKey() != builtKey) rebuild();
    for (auto& c : cards) c->refresh();
}

// ------------------------------------------------------------------------------------------- preset actions
namespace presetActions
{
void step (AppContext& ctx, DeviceProcessor& d, int dir)
{
    auto list = presets::list (d.type());
    if (list.empty()) return;
    int cur = -1;
    for (int i = 0; i < (int) list.size(); ++i) if (list[(size_t) i].name == d.presetName) { cur = i; break; }
    const int n = (int) list.size();
    const int next = cur < 0 ? (dir > 0 ? 0 : n - 1) : ((cur + dir) % n + n) % n;
    presets::apply (d, list[(size_t) next]);
    ctx.setReference (d);
    ctx.markDirty();
}

void toggleCompare (AppContext& ctx, DeviceProcessor& d)
{
    auto ref = ctx.reference.find (&d);
    if (ref == ctx.reference.end()) return;
    auto cmp = ctx.comparing.find (&d);
    if (cmp != ctx.comparing.end())
    {
        d.load (cmp->second);          // back to the edited sound
        ctx.comparing.erase (cmp);
    }
    else
    {
        juce::ValueTree now ("Device");
        d.save (now);
        ctx.comparing[&d] = now;
        d.load (ref->second);          // the sound as it was loaded or saved
    }
}

void showDeviceMenu (AppContext& ctx, DeviceProcessor& d, juce::Component* anchor, int pattern, int track)
{
    juce::PopupMenu m;
    const int sel = ctx.selection (&d);
    if (d.type() == DeviceType::Sampler)
    {
        m.addItem (1, "Load sample into zone " + juce::String (sel + 1) + "...");
        m.addItem (2, "Load samples as key zones...");
        m.addItem (3, "Clear zone " + juce::String (sel + 1));
    }
    if (d.type() == DeviceType::DrumSampler)
    {
        m.addItem (4, "Load sample into " + d.laneName (sel) + "...");
        m.addItem (5, "Slice a loop across the pads...");
    }
    if (d.type() == DeviceType::FM) m.addItem (6, "Import 32-voice bank...");
    juce::PopupMenu q;
    const int os = qualityOversampling().load();
    q.addItem (11, "Off", true, os == 1); q.addItem (12, "2x", true, os == 2); q.addItem (14, "4x", true, os == 4);
    m.addSubMenu ("Quality", q);
    juce::ignoreUnused (pattern, track);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor), [&ctx, &d, sel] (int id)
    {
        if (id >= 11 && id <= 14) { qualityOversampling() = id - 10; return; }
        auto pick = [] (const juce::String& title, const juce::String& pattern, bool multi, std::function<void (juce::Array<juce::File>)> done)
        {
            chooser() = std::make_unique<juce::FileChooser> (title, juce::File(), pattern);
            const int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | (multi ? juce::FileBrowserComponent::canSelectMultipleItems : 0);
            chooser()->launchAsync (flags, [done] (const juce::FileChooser& fc) { if (! fc.getResults().isEmpty()) done (fc.getResults()); });
        };
        const juce::String audio = "*.wav;*.aif;*.aiff;*.flac";
        if (id == 1) pick ("Load sample", audio, false, [&ctx, &d, sel] (auto f) { static_cast<SamplerDevice&> (d).loadZone (sel, refForFile (f[0])); ctx.markDirty(); });
        if (id == 2) pick ("Load samples as zones", audio, true, [&ctx, &d] (auto f) { static_cast<SamplerDevice&> (d).loadZonesFromFiles (f); ctx.markDirty(); });
        if (id == 3) { static_cast<SamplerDevice&> (d).loadZone (sel, {}); ctx.markDirty(); }
        if (id == 4) pick ("Load sample", audio, false, [&ctx, &d, sel] (auto f) { static_cast<DrumSamplerDevice&> (d).loadPad (sel, refForFile (f[0])); ctx.markDirty(); });
        if (id == 5) pick ("Slice a loop", audio, false, [&ctx, &d] (auto f) { static_cast<DrumSamplerDevice&> (d).sliceLoop (refForFile (f[0])); ctx.markDirty(); });
        if (id == 6) pick ("Import bank", "*.syx", false, [] (auto f)
        {
            juce::MemoryBlock data;
            f[0].loadFileAsData (data);
            auto voices = FmDevice::parseBank (data);
            FmDevice tmp;
            int n = 0;
            for (auto& v : voices)
            {
                tmp.resetToDefaults();
                for (auto& [id2, val] : v.values) tmp.set (id2, val);
                presets::save (tmp, f[0].getFileNameWithoutExtension() + " " + juce::String (++n).paddedLeft ('0', 2) + " " + v.name);
            }
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Bank imported",
                                                    juce::String (n) + " voices are now in the preset browser under User.");
        });
    });
}
} // namespace presetActions

// ------------------------------------------------------------------------------------------- header
EditorHeader::EditorHeader (AppContext& c) : ctx (c) {}

void EditorHeader::refresh()
{
    auto* d = ctx.focusDevice();
    juce::String k;
    if (d != nullptr) k << d->presetName << (int) d->get ("engine") << (int) ctx.comparing.count (d);
    k << ctx.focusTrack() << ctx.focusPattern() << (d != nullptr ? (int) d->type() : -1);
    for (int t = 0; t < kTracks; ++t) k << (ctx.engine.device (ctx.focusPattern(), t) != nullptr ? "1" : "0");
    if (k != shownKey) { shownKey = k; repaint(); }
}

void EditorHeader::paint (juce::Graphics& g)
{
    const int track = ctx.focusTrack();
    auto* d = ctx.focusDevice();
    if (d == nullptr) return;
    const bool plugin = ! ctx.standalone;   // the plugin: same header, no back button or track dots
    const auto colour = plugin ? deviceColour (d->type()) : col::track (track);
    const float y = 0.0f;
    backR = {};
    if (! plugin)
    {
        backR = { M, y + 8, 36, 36 };
        g.setColour (col::control); g.fillEllipse (backR);
        paint::chevron (g, M + 16, y + 26, 12, col::text, 'l', 2.2f);
    }
    const auto name = plugin ? deviceTypeName (d->type()) : juce::String (track + 1) + "  " + deviceTypeName (d->type());
    const float pw = paint::pill (g, M + 48, y + 10, name, 14, colour, col::bg, 32, 12, Weight::Black, true);
    pillR = { M + 48, y + 10, pw, 32 };
    float tx = M + 48 + pw + 18;
    for (int i = 0; i < kTracks && ! plugin; ++i)
    {
        const bool has = ctx.engine.device (ctx.focusPattern(), i) != nullptr;
        const float r = i == track ? 6.0f : 4.5f;
        g.setColour (has ? col::track (i) : col::faint);
        g.fillEllipse (tx + i * 15 - r, y + 26 - r, r * 2, r * 2);
        if (i == track) { g.setColour (col::text); g.drawEllipse (tx + i * 15 - r, y + 26 - r, r * 2, r * 2, 1.5f); }
        dotR[(size_t) i] = { tx + i * 15 - 7.5f, y + 17, 15, 18 };
    }
    engineR = {};
    if (d->type() == DeviceType::Poly)
    {
        engineR = { tx + (plugin ? 0 : kTracks * 15 + 12), y + 10, 120, 32 };
        paint::segmented (g, engineR, { "DCO", "VCO" }, (int) d->get ("engine"), colour, 12);
    }
    // preset browser
    const float px = 560;
    presetR = { px, y + 8, 420, 36 };
    g.setColour (col::control); g.fillRoundedRectangle (presetR, 18);
    prevR = { px, y + 8, 40, 36 }; nextR = { px + 380, y + 8, 40, 36 };
    paint::chevron (g, px + 20, y + 26, 10, col::quiet, 'l');
    paint::chevron (g, px + 400, y + 26, 10, col::quiet, 'r');
    const bool comparing = ctx.comparing.count (d) > 0;
    paint::text (g, d->presetName, px + 190, y + 32, 15, Weight::Black, col::text, juce::Justification::horizontallyCentred);
    const auto cat = categoryOf (*d);
    if (cat.isNotEmpty())
    {
        const float cw = textWidth (cat, 10, Weight::Black) + 16, cx = px + 190 + textWidth (d->presetName, 15, Weight::Black) / 2 + 10;
        g.setColour (colour.withAlpha (0.2f)); g.fillRoundedRectangle (cx, y + 16, cw, 20, 10);
        paint::text (g, cat, cx + cw / 2, y + 30, 10, Weight::Black, colour, juce::Justification::horizontallyCentred);
    }
    saveR = { px + 430, y + 8, 36, 36 };
    g.setColour (col::control); g.fillEllipse (saveR);
    paint::saveIcon (g, px + 448, y + 26, 14, col::quiet);
    const float cw = paint::pill (g, W - M - 46, y + 10, "Compare", 12, comparing ? col::pattern : col::control, comparing ? col::bg : col::quiet,
                                  32, 12, Weight::Bold, false, juce::Justification::right);
    compareR = { W - M - 46 - cw, y + 10, cw, 32 };
    moreR = { W - M - 34, y + 10, 32, 32 };
    g.setColour (col::control); g.fillEllipse (moreR);
    paint::dots (g, W - M - 18, y + 26, col::quiet);
    fb.begin();
    if (! backR.isEmpty()) fb.hot (backR, 18);
    fb.hot (pillR, 16);
    for (int i = 0; i < kTracks && ! plugin; ++i) fb.hot (dotR[(size_t) i], 9);
    if (! engineR.isEmpty()) fb.hot (engineR, 16);
    fb.hot (prevR, 18); fb.hot (nextR, 18); fb.hot (presetR.reduced (40, 0), 4); fb.hot (saveR, 18); fb.hot (compareR, 16); fb.hot (moreR, 16);
    fb.paint (g);
}

void EditorHeader::mouseUp (const juce::MouseEvent& e)
{
    auto* d = ctx.focusDevice();
    if (d == nullptr) return;
    const auto p = e.position;
    if (! backR.isEmpty() && backR.contains (p)) { if (ctx.closeEditor) ctx.closeEditor(); return; }
    if (pillR.contains (p)) { if (ctx.showDevicePicker) ctx.showDevicePicker (ctx.focusTrack(), this); return; }
    for (int i = 0; i < kTracks; ++i)
        if (dotR[(size_t) i].contains (p) && ctx.engine.device (ctx.focusPattern(), i) != nullptr) { if (ctx.openEditor) ctx.openEditor (i); return; }
    if (! engineR.isEmpty() && engineR.contains (p))
    {
        d->set ("engine", p.x < engineR.getCentreX() ? 0.0f : 1.0f);
        d->allNotesOff();
        ctx.markDirty();
        repaint();
        return;
    }
    if (prevR.contains (p)) { presetActions::step (ctx, *d, -1); repaint(); return; }
    if (nextR.contains (p)) { presetActions::step (ctx, *d, 1); repaint(); return; }
    if (presetR.contains (p)) { if (ctx.showPresetBrowser) ctx.showPresetBrowser (d, this); return; }
    if (saveR.contains (p)) { if (ctx.showPresetBrowser) ctx.showPresetBrowser (d, this); return; }   // naming and saving happen in the browser
    if (compareR.contains (p)) { presetActions::toggleCompare (ctx, *d); repaint(); return; }
    if (moreR.contains (p)) presetActions::showDeviceMenu (ctx, *d, this, ctx.focusPattern(), ctx.focusTrack());
}

// ------------------------------------------------------------------------------------------- step strip
StepStrip::StepStrip (AppContext& c) : ctx (c)
{
    setTooltip ("Click: step on/off   Drag: velocity   Shift-click: select a step for parameter locks   Alt-click: slide   Ctrl-click: accent");
    // keys on a focused step: ↑ / ↓ its velocity (Enter on / off, Shift+Enter lock it, Alt+Enter slide, Ctrl+Enter accent)
    fb.keyHandler = [this] (int index, const juce::KeyPress& k)
    {
        const int code = k.getKeyCode();
        if (code != juce::KeyPress::upKey && code != juce::KeyPress::downKey) return false;
        const auto& hots = fb.getHots();
        if (index < 0 || index >= (int) hots.size()) return false;
        const auto h = locate (hots[(size_t) index].r.getCentre());
        if (h.step < 0 || h.lane >= 0) return false;
        const int pat = ctx.focusPattern();
        const int si = ctx.viewBar * ctx.engine.project().stepsPerBar (pat) + h.step;
        auto& s = ctx.engine.project().patterns[(size_t) pat].slots[(size_t) ctx.focusTrack()].track.steps[(size_t) si];
        if (s.noteCount == 0) return false;
        ctx.snapshot (pat);
        { const Engine::ScopedEdit ed (ctx.engine); s.setVelocity (juce::jlimit (1, 127, (int) s.velocity() + (code == juce::KeyPress::upKey ? 8 : -8))); }
        ctx.markDirty();
        repaint();
        return true;
    };
}

void StepStrip::refresh()
{
    const int pat = ctx.focusPattern(), trk = ctx.focusTrack();
    const auto& proj = ctx.engine.project();
    const auto& tr = proj.patterns[(size_t) pat].slots[(size_t) trk].track;
    const int spb = proj.stepsPerBar (pat), first = ctx.viewBar * spb;
    juce::String s;
    s << pat << trk << ctx.viewBar << ctx.lockStep << proj.patterns[(size_t) pat].lengthBars << spb;
    s << (ctx.engine.isPlaying() ? ctx.engine.currentStep() : -1);
    s << (ctx.engine.isRecording() && proj.recordMode == 1 ? ctx.engine.stepRecordCursor.load() : -1);
    for (int i = first; i < first + spb && i < kMaxSteps; ++i)
    {
        const auto& st = tr.steps[(size_t) i];
        s << (int) st.noteCount << (int) st.notes[0] << (int) st.velocity() << (int) st.maxLen() << (int) st.flags;
        for (int l = 0; l < kLanes; ++l) s << (tr.lanes[(size_t) l][(size_t) i].on ? 1 : 0);
    }
    if (tr.pianoRoll) { s << "c" << (int) tr.clip.size(); for (auto& n : tr.clip) s << n.start + n.length * 3 + n.note; }
    if (s != sig) { sig = s; repaint(); }
}

StepStrip::Hit StepStrip::locate (juce::Point<float> p) const
{
    Hit h;
    auto* d = ctx.focusDevice();
    const bool drum = d != nullptr && d->laneCount() > 0;
    const auto& proj = ctx.engine.project();
    const int pat = ctx.focusPattern(), spb = proj.stepsPerBar (pat), bars = proj.patterns[(size_t) pat].lengthBars;
    const float x = 0, y = 0, w = (float) getWidth(), hgt = (float) getHeight();
    for (int b = 0; b < bars; ++b)
        if (juce::Rectangle<float> (x + 16 + (b % 4) * 34, y + 16 + (b / 4) * 30, 28, 24).contains (p)) { h.bar = b; return h; }
    const float sx = x + (drum ? 230.0f : 172.0f), sw = w - (sx - x) - 16;
    if (p.x < sx) return h;
    if (drum)
    {
        const float lh = (hgt - 20) / 8.0f, cw = (sw - 3.0f * (spb - 1)) / spb;
        h.step = juce::jlimit (0, spb - 1, (int) ((p.x - sx) / (cw + 3)));
        h.lane = juce::jlimit (0, 7, (int) ((p.y - y - 10) / lh));
        return h;
    }
    const float gap = 6.0f, cw = (sw - gap * (spb - 1)) / spb;
    h.step = juce::jlimit (0, spb - 1, (int) ((p.x - sx) / (cw + gap)));
    const float cx = sx + h.step * (cw + gap);
    h.badge = d != nullptr && d->type() == DeviceType::Acid && p.x > cx + cw - 24 && p.y < y + 16 + 22;
    return h;
}

void StepStrip::paint (juce::Graphics& g)
{
    const int pat = ctx.focusPattern(), trk = ctx.focusTrack();
    auto* d = ctx.focusDevice();
    const auto colour = col::track (trk);
    const auto r = getLocalBounds().toFloat();
    paint::card (g, r);
    fb.begin();
    if (d == nullptr) return;
    const auto& proj = ctx.engine.project();
    const bool stepRec = ctx.engine.isRecording() && proj.recordMode == 1;
    const int cursor = stepRec ? ctx.engine.stepRecordCursor.load() % std::max (1, proj.patternSteps (pat)) : -1;
    const auto& tr = proj.patterns[(size_t) pat].slots[(size_t) trk].track;
    const int spb = proj.stepsPerBar (pat), bars = proj.patterns[(size_t) pat].lengthBars;
    ctx.viewBar = std::min (ctx.viewBar, bars - 1);
    const int first = ctx.viewBar * spb;
    for (int b = 0; b < bars; ++b)
    {
        const float bx = 16 + (b % 4) * 34, by = 16 + (b / 4) * 30;
        const bool on = b == ctx.viewBar;
        g.setColour (on ? colour : col::control); g.fillRoundedRectangle (bx, by, 28, 24, 8);
        fb.hot ({ bx, by, 28, 24 }, 8);
        paint::text (g, juce::String (b + 1), bx + 14, by + 16, 11, Weight::Black, on ? col::bg : col::quiet, juce::Justification::horizontallyCentred);
    }
    const int playStep = ctx.engine.isPlaying() && ctx.engine.activePattern() == pat ? ctx.engine.currentStep() - first : -1000;
    const bool drum = d->laneCount() > 0;
    const float sx = drum ? 230.0f : 172.0f, sw = r.getWidth() - sx - 16;
    if (drum)
    {
        const float lh = (r.getHeight() - 20) / 8.0f, cw = (sw - 3.0f * (spb - 1)) / spb;
        const int sel = ctx.selection (d);
        for (int l = 0; l < 8; ++l)
        {
            const float ly = 10 + l * lh;
            paint::text (g, d->laneName (l), sx - 10, ly + lh / 2 + 3.5f, 9, Weight::Bold, l == sel ? col::text : col::faint, juce::Justification::right);
            for (int i = 0; i < spb; ++i)
            {
                const auto& ds = tr.lanes[(size_t) l][(size_t) (first + i)];
                g.setColour (ds.on ? colour.withAlpha (0.45f + 0.55f * ds.velocity / 127.0f) : col::control);
                g.fillRoundedRectangle (sx + i * (cw + 3), ly + 1, cw, lh - 3, 3);
                fb.hot ({ sx + i * (cw + 3), ly + 1, cw, lh - 3 }, 3);
            }
        }
        if (playStep >= 0 && playStep < spb) { g.setColour (col::pattern); g.drawRoundedRectangle (sx + playStep * (cw + 3) - 2, 7, cw + 4, r.getHeight() - 14, 5, 1.5f); }
        if (cursor >= first && cursor < first + spb) { g.setColour (col::record); g.drawRoundedRectangle (sx + (cursor - first) * (cw + 3) - 2, 7, cw + 4, r.getHeight() - 14, 5, 2.0f); }
        fb.paint (g);
        return;
    }
    if (tr.pianoRoll)
    {
        // a piano-roll track: its notes in miniature (edited on the Seq page)
        const juce::Rectangle<float> area (sx, 16.0f, sw, r.getHeight() - 32);
        g.setColour (col::control); g.fillRoundedRectangle (area, 9);
        const int stT = ctx.engine.project().stepTicks (ctx.focusPattern()), a = first * stT, b = (first + spb) * stT;
        int lo = 127, hi = 0;
        for (auto& n : tr.clip) { lo = std::min (lo, (int) n.note); hi = std::max (hi, (int) n.note); }
        for (auto& n : tr.clip)
        {
            if (n.end() <= a || n.start >= b) continue;
            const float x0 = area.getX() + area.getWidth() * (float) (std::max (n.start, a) - a) / (float) (b - a);
            const float x1 = area.getX() + area.getWidth() * (float) (std::min (n.end(), b) - a) / (float) (b - a);
            const float yy = area.getBottom() - 10 - (area.getHeight() - 20) * (hi > lo ? (n.note - lo) / (float) (hi - lo) : 0.5f);
            g.setColour (colour.withAlpha (0.5f + 0.5f * n.vel / 127.0f));
            g.fillRoundedRectangle (x0, yy - 2.5f, std::max (4.0f, x1 - x0 - 1), 5, 2.5f);
        }
        paint::text (g, "Piano roll: edit it on the Seq page", area.getRight() - 12, area.getY() + 16, 10, Weight::Bold, col::faint, juce::Justification::right);
        if (playStep >= 0 && playStep < spb)
        {
            const float px = area.getX() + area.getWidth() * playStep / (float) spb;
            g.setColour (col::pattern); g.drawLine (px, area.getY(), px, area.getBottom(), 2.0f);
        }
        fb.paint (g);
        return;
    }
    const float gap = 6.0f, cw = (sw - gap * (spb - 1)) / spb, cy = 16.0f, ch = r.getHeight() - 32;
    for (int i = 0; i < spb; ++i)
    {
        const int si = first + i;
        const auto& s = tr.steps[(size_t) si];
        const float cx = sx + i * (cw + gap);
        g.setColour ((i / 4) % 2 == 0 ? col::control : col::altCell);
        g.fillRoundedRectangle (cx, cy, cw, ch, 9);
        fb.hot ({ cx, cy, cw, ch }, 9);
        if (si == cursor) { g.setColour (col::record); g.drawRoundedRectangle (cx - 3, cy - 4, cw + 6, ch + 8, 11, 2.0f); }
        if (s.noteCount > 0)
        {
            const float vel = s.velocity() / 127.0f;
            g.setColour (colour.withAlpha (0.25f + 0.6f * vel)); g.fillRoundedRectangle (cx, cy, cw, ch, 9);
            auto label = juce::MidiMessage::getMidiNoteName (s.notes[0], true, true, 4);
            if (s.noteCount > 1) label << "+" << (s.noteCount - 1);
            paint::text (g, label, cx + cw / 2, cy + ch / 2 + 5, label.length() < 5 ? 13.0f : 11.0f, Weight::Black, col::bg, juce::Justification::horizontallyCentred);
            g.setColour (col::bg.withAlpha (0.6f)); g.fillRoundedRectangle (cx + 8, cy + ch - 9, (cw - 16) * vel, 3, 1.5f);
            if (s.flags & kAccent) { g.setColour (col::bg); g.fillRoundedRectangle (cx + cw - 20, cy + 6, 14, 14, 4); paint::text (g, "A", cx + cw - 13, cy + 17, 10, Weight::Black, colour, juce::Justification::horizontallyCentred); }
            if ((s.flags & kSlide) && i + 1 < spb)
            {
                juce::Path p;
                p.startNewSubPath (cx + cw / 2, cy - 2);
                p.quadraticTo (cx + cw + gap / 2, cy - 14, cx + cw + gap + cw / 2, cy - 2);
                g.setColour (colour); g.strokePath (p, juce::PathStrokeType (2.0f));
            }
        }
        if (si == ctx.lockStep) { g.setColour (col::text); g.drawRoundedRectangle (cx - 3, cy - 4, cw + 6, ch + 8, 11, 2.5f); paint::lockIcon (g, cx + 12, cy + 14, 10, col::pattern); }
    }
    if (playStep >= 0 && playStep < spb) { g.setColour (col::pattern); g.drawRoundedRectangle (sx + playStep * (cw + gap) - 3, 12, cw + 6, r.getHeight() - 24, 11, 2); }
    fb.paint (g);
}

void StepStrip::mouseDown (const juce::MouseEvent& e)
{
    auto* d = ctx.focusDevice();
    if (d == nullptr) return;
    const auto h = locate (e.position);
    dragStep = -1;
    if (h.bar >= 0) { ctx.viewBar = h.bar; ctx.lockStep = -1; repaint(); return; }
    if (h.step < 0) return;
    const int pat = ctx.focusPattern(), trk = ctx.focusTrack();
    const int si = ctx.viewBar * ctx.engine.project().stepsPerBar (pat) + h.step;
    if (e.mods.isShiftDown()) { ctx.lockStep = ctx.lockStep == si ? -1 : si; repaint(); return; }
    if (d->laneCount() == 0 && ctx.engine.project().patterns[(size_t) pat].slots[(size_t) trk].track.pianoRoll) return;   // a clip: edited in Seq
    ctx.snapshot (pat);
    const Engine::ScopedEdit edit (ctx.engine);
    auto& tr = ctx.engine.project().patterns[(size_t) pat].slots[(size_t) trk].track;
    if (h.lane >= 0) { auto& ds = tr.lanes[(size_t) h.lane][(size_t) si]; ds.on = ! ds.on; if (ds.on) { ds.velocity = 100; ds.probability = 100; ds.ratchet = 1; } ctx.select (d, h.lane); }
    else
    {
        auto& s = tr.steps[(size_t) si];
        if ((h.badge || e.mods.isCommandDown()) && s.noteCount > 0) s.flags ^= kAccent;
        else if (e.mods.isAltDown() && s.noteCount > 0) s.flags ^= kSlide;
        else if (s.noteCount > 0) { s.clear(); }
        else { s.clear(); s.addNote (ctx.lastNote[(size_t) trk], ctx.lastVel[(size_t) trk], ctx.lastGate[(size_t) trk]); dragStep = si; }
        dragStep = s.noteCount > 0 ? si : -1;
    }
    ctx.markDirty();
    repaint();
}

void StepStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStep < 0) return;
    const Engine::ScopedEdit edit (ctx.engine);
    auto& s = ctx.engine.project().patterns[(size_t) ctx.focusPattern()].slots[(size_t) ctx.focusTrack()].track.steps[(size_t) dragStep];
    s.setVelocity (juce::jlimit (1, 127, 100 - e.getDistanceFromDragStartY()));
    ctx.markDirty();
    repaint();
}

// ------------------------------------------------------------------------------------------- editor view
EditorView::EditorView (AppContext& c) : ctx (c), header (c), strip (c)
{
    addAndMakeVisible (header);
    addAndMakeVisible (strip);
}

void EditorView::showTrack (int track)
{
    ctx.engine.focusTrack = track;
    shown = ctx.focusDevice();
    body = std::make_unique<EditorBody> (ctx, col::track (track));
    addAndMakeVisible (*body);
    body->setDevice (shown, ctx.focusPattern(), track);
    if (shown != nullptr && ! ctx.reference.count (shown)) ctx.setReference (*shown);
    resized();
    header.repaint();
}

void EditorView::paint (juce::Graphics&) {}

void EditorView::resized()
{
    const int h = getHeight();
    header.setBounds (0, 8, W, 52);
    const int stripH = 120;
    strip.setBounds (M, h - M - stripH, W - 2 * M, stripH);
    if (body) body->setBounds (M, 72, W - 2 * M, h - M - stripH - GAP - 72);
}

void EditorView::refresh()
{
    if (ctx.focusDevice() != shown) showTrack (ctx.focusTrack());
    header.refresh();
    if (body) body->refresh();
    strip.refresh();
}
} // namespace mb::ui
