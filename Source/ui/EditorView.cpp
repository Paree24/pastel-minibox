#include "EditorView.h"
#include "seq/Chords.h"
#include "app/presets/PresetBank.h"
#include "devices/sampler/SamplerDevice.h"
#include "devices/drumsampler/DrumSamplerDevice.h"
#include "devices/fm/FmDevice.h"
#include "devices/wavetable/WavetableDevice.h"
#include "dsp/Wavetables.h"

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

/** The Wavetable's step strip: the parameter whose step locks show under the notes (-1: none), per device. */
struct StripLock { const char* id; const char* label; };
const StripLock kStripLocks[] = { { "cutoff", "Cutoff" }, { "oscAPos", "Position" }, { "oscBMod", "FM" }, { "distDrive", "Dirt" } };
int& stripLock (const DeviceProcessor* d) { static std::unordered_map<const DeviceProcessor*, int> m; auto it = m.find (d); if (it == m.end()) m[d] = 1; return m[d]; }
int stripLockParam (const DeviceProcessor* d)
{
    if (d == nullptr || d->type() != DeviceType::Wavetable) return -1;
    const int k = stripLock (d);
    return k < 0 ? -1 : d->params().indexOf (kStripLocks[k].id);
}
constexpr float kLockBarH = 16.0f;   // the lock bar under each step

/** What Randomize changes, per device (the scope pill beside it). */
int& randomScope (const DeviceProcessor* d) { static std::unordered_map<const DeviceProcessor*, int> m; return m[d]; }
} // namespace

// ------------------------------------------------------------------------------------------- section card
SectionCard::SectionCard (AppContext& c, DeviceProcessor& d, const SectionSpec& s, juce::Colour colr, int pattern, int track,
                          std::function<void (int, float)> onParamChange)
    : ctx (c), dev (d), spec (s), colour (colr)
{
    for (auto& k : spec.knobs)
    {
        if (const int m = macroIndexOf (k.id); m >= 0)
        {
            auto kv = std::make_unique<KnobView> (std::make_unique<MacroBinding> (&ctx, &dev, m), k.label, colour, spec.band ? 22.0f : 20.0f, spec.band);
            addAndMakeVisible (*kv);
            knobs.push_back (std::move (kv));
            continue;
        }
        const int idx = dev.params().indexOf (resolveId (dev, k));
        if (idx < 0) continue;
        auto b = std::make_unique<ParamBinding> (&ctx, &dev, idx, pattern, track);
        b->onChange = onParamChange;
        if (track >= 0) b->learnId = "param:" + juce::String (track) + ":" + dev.params()[idx].id;
        auto kv = std::make_unique<KnobView> (std::move (b), k.label, colour, spec.compact ? 15.0f : 20.0f);
        if (spec.compact) kv->labelSize = 10.0f;
        addAndMakeVisible (*kv);
        knobs.push_back (std::move (kv));
    }
    if (spec.choiceId == "ui:lane")
    {
        choice = std::make_unique<ChoiceView> (juce::StringArray { "Vel", "Prob", "Ratchet" }, [this] { return ctx.laneView; },
                                               [this] (int i) { ctx.laneView = i; if (visual) visual->repaint(); }, colour);
        addAndMakeVisible (*choice);
    }
    else if (const int bit = uiBit (spec.choiceId); bit != 0)
    {
        // which envelope / LFO the card shows: a bit of the device's selection (the card is rebuilt for it)
        choice = std::make_unique<ChoiceView> (uiChoices (spec.choiceId), [this, bit] { return (ctx.selection (&dev) & bit) != 0 ? 1 : 0; },
                                               [this, bit] (int i)
                                               {
                                                   const int v = (ctx.selection (&dev) & ~bit) | (i ? bit : 0);
                                                   juce::MessageManager::callAsync ([c = &ctx, d = &dev, v] { c->select (d, v); });
                                               }, colour);
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
            choice = std::make_unique<ChoiceView> (opts, [this, idx] { return (int) std::lround (dev.pv (idx) - dev.params()[idx].min); },
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
    if (spec.toggleId == "ui:random")
    {
        toggle = std::make_unique<PaintedButton>();
        toggle->painter = [this] (juce::Graphics& g, juce::Rectangle<float> r, bool over, bool)
        {
            paint::pill (g, r.getX(), r.getY(), spec.toggleLabel, 12, over ? colour.brighter (0.15f) : colour, col::bg, r.getHeight(), 16, Weight::Black,
                         false, juce::Justification::left, r.getWidth());
        };
        toggle->onClick = [this, onParamChange] (const juce::MouseEvent&)
        {
            if (auto* w = dynamic_cast<WavetableDevice*> (&dev))
            {
                w->randomize (randomScope (&dev), (uint32_t) juce::Random::getSystemRandom().nextInt());
                ctx.markDirty();
            }
        };
        addAndMakeVisible (*toggle);
        scope = std::make_unique<PaintedButton>();
        scope->painter = [this] (juce::Graphics& g, juce::Rectangle<float> r, bool, bool)
        {
            paint::pill (g, r.getX(), r.getY(), WavetableDevice::randomizeScopes()[randomScope (&dev)], 12, col::control, col::text, r.getHeight(), 14,
                         Weight::Bold, true, juce::Justification::left, r.getWidth());
        };
        scope->onClick = [this] (const juce::MouseEvent&)
        {
            juce::PopupMenu m;
            const auto names = WavetableDevice::randomizeScopes();
            for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i], true, i == randomScope (&dev));
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (scope.get()), [this, sp = juce::Component::SafePointer<SectionCard> (this)] (int id)
            {
                if (sp == nullptr || id <= 0) return;
                randomScope (&dev) = id - 1;
                scope->repaint();
            });
        };
        addAndMakeVisible (*scope);
    }
    else if (spec.toggleId.isNotEmpty())
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
    if (spec.menuId.isNotEmpty())
    {
        const int idx = dev.params().indexOf (spec.menuId);
        if (idx >= 0)
        {
            menu = std::make_unique<PaintedButton>();
            menu->painter = [this, idx] (juce::Graphics& g, juce::Rectangle<float> r, bool over, bool)
            {
                const auto& info = dev.params()[idx];
                paint::pill (g, r.getX(), r.getY(), info.choices[juce::jlimit (0, info.choices.size() - 1, (int) dev.get (idx))], 11,
                             over ? col::surface.brighter (0.08f) : col::control, colour, r.getHeight(), 10, Weight::Black, true, juce::Justification::left, r.getWidth());
            };
            menu->onClick = [this, idx, onParamChange] (const juce::MouseEvent&)
            {
                // the tables by family, a column each
                juce::PopupMenu m;
                const auto& info = dev.params()[idx];
                const auto fams = dsp::wt::Bank::families();
                juce::String fam;
                for (int i = 0; i < info.choices.size(); ++i)
                {
                    if (fams[i] != fam) { if (fam.isNotEmpty() && fams[i] != "Analog" && fams[i] != "Sub" && fams[i] != "Growl" && fams[i] != "Metal" && fams[i] != "Digital") m.addColumnBreak();
                                          fam = fams[i]; m.addSectionHeader (fam); }
                    m.addItem (i + 1, info.choices[i], true, i == (int) dev.get (idx));
                }
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menu.get()),
                                 [this, idx, onParamChange, sp = juce::Component::SafePointer<SectionCard> (this)] (int id)
                {
                    if (sp == nullptr || id <= 0) return;
                    dev.set (idx, (float) (id - 1));
                    ctx.markDirty();
                    if (onParamChange) onParamChange (idx, (float) (id - 1));
                    if (menu) menu->repaint();
                    if (visual) visual->repaint();
                });
            };
            addAndMakeVisible (*menu);
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
    if (spec.band)
    {
        // the macro band: the knobs spread across, Randomize and its scope at the right
        const float sw = scope ? textWidth (WavetableDevice::randomizeScopes()[randomScope (&dev)], 12, Weight::Bold) + 46 : 0.0f;
        const float rw = toggle ? textWidth (spec.toggleLabel, 12, Weight::Black) + 32 : 0.0f;
        if (scope) scope->setBounds (juce::Rectangle<float> (w - 16 - sw, h / 2 - 16, sw, 32).toNearestInt());
        if (toggle) toggle->setBounds (juce::Rectangle<float> (w - 16 - sw - 10 - rw, h / 2 - 16, rw, 32).toNearestInt());
        const float x0 = 130.0f, x1 = w - 16 - sw - rw - 40;
        const int n = (int) knobs.size();
        const float cw = (x1 - x0) / std::max (1, n);
        for (int i = 0; i < n; ++i)
            knobs[(size_t) i]->setBounds (juce::Rectangle<float> (x0 + cw * (i + 0.5f) - 48, h / 2 - 40, 96, 84).toNearestInt());
        return;
    }
    if (menu)
    {
        const int idx = dev.params().indexOf (spec.menuId);
        const float mw = textWidth (dev.params()[idx].choices[juce::jlimit (0, dev.params()[idx].choices.size() - 1, (int) dev.get (idx))], 11, Weight::Black) + 40;
        menu->setBounds (juce::Rectangle<float> (w - 14 - mw, 9, mw, 24).toNearestInt());
    }
    if (toggle)
    {
        const float tw = textWidth (spec.toggleLabel, 10, Weight::Black) + 18;
        toggle->setBounds (juce::Rectangle<float> (w - 14 - tw, 10, tw, 22).toNearestInt());
    }
    float cy = 42.0f;
    const float ix = 14.0f, iw = w - 28.0f;
    if (choice) { choice->setBounds (juce::Rectangle<float> (ix, cy, iw, 26).toNearestInt()); cy += 38.0f; }
    const int n = (int) knobs.size();
    const float cellMin = spec.compact ? 54.0f : 86.0f, rowH = spec.compact ? 66.0f : 84.0f;
    const int cols = n > 0 ? std::max (1, std::min (n, (int) ((w - 12) / cellMin))) : 1;
    const int rows = n > 0 ? (n + cols - 1) / cols : 0;
    const float kh = rows * rowH;
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
        const float kx = x0 + cellw * (c + 0.5f), ky = cy + r * rowH + (spec.compact ? 24.0f : 32.0f);
        const float bw = std::min (cellw, 86.0f);
        knobs[(size_t) i]->setBounds (juce::Rectangle<float> (kx - bw / 2, ky - (spec.compact ? 20.0f : 26.0f), bw, spec.compact ? 62.0f : 72.0f).toNearestInt());
    }
}

void SectionCard::refresh()
{
    if (menu) { menu->repaint(); resized(); }
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
            for (auto& kn : sec.knobs) k << "," << resolveId (*dev, kn) << "=" << kn.label;
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
    // row heights: by the spec's weights (the Wavetable's macro band is short), else equal
    float wsum = 0.0f;
    for (size_t ri = 0; ri < spec.rows.size(); ++ri) wsum += ri < spec.rowWeights.size() ? spec.rowWeights[ri] : 1.0f;
    const float avail = h - GAP * ((float) spec.rows.size() - 1);
    float y = 0.0f;
    size_t ci = 0;
    for (size_t ri = 0; ri < spec.rows.size(); ++ri)
    {
        const auto& row = spec.rows[ri];
        float tot = 0.0f;
        for (auto& s : row) tot += s.weight;
        const float unit = (w - GAP * ((float) row.size() - 1)) / tot;
        const float rh = avail * (ri < spec.rowWeights.size() ? spec.rowWeights[ri] : 1.0f) / wsum;
        float x = 0.0f;
        for (auto& s : row)
        {
            if (ci < cards.size()) cards[ci++]->setBounds (juce::Rectangle<float> (x, y, unit * s.weight, rh).toNearestInt());
            x += unit * s.weight + GAP;
        }
        y += rh + GAP;
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
    juce::ignoreUnused (pattern, track);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor), [&ctx, &d, sel] (int id)
    {
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
        if (id == 5) pick ("Slice a loop", audio, false, [&ctx, &d] (auto f) { static_cast<DrumSamplerDevice&> (d).sliceLoop (refForFile (f[0]), false, [&ctx] (auto) { ctx.markDirty(); }); });
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
    if (const int lp = stripLockParam (ctx.focusDevice()); lp >= 0)
    {
        s << "L" << lp;
        if (const auto* l = const_cast<Track&> (tr).lockFor (lp, false))
            for (int i = first; i < first + spb && i < kMaxSteps; ++i) s << (std::isnan (l->values[(size_t) i]) ? -1.0f : l->values[(size_t) i]);
    }
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
    // the Wavetable: one parameter's step locks under the notes (Cutoff, Position, FM, Dirt), chosen with a pill
    const int lockP = stripLockParam (d);
    const LockLane* lockLane = lockP >= 0 ? const_cast<Track&> (tr).lockFor (lockP, false) : nullptr;
    if (d->type() == DeviceType::Wavetable)
    {
        const int k = stripLock (d);
        const juce::String txt = k < 0 ? juce::String ("Locks") : "Lock: " + juce::String (kStripLocks[k].label);
        paint::pill (g, 16, r.getHeight() - 38, txt, 11, col::control, k < 0 ? col::quiet : col::text, 26, 10, Weight::Bold, true,
                     juce::Justification::left, 140);
        fb.hot ({ 16, r.getHeight() - 38, 140, 26 }, 13);
    }
    const float gap = 6.0f, cw = (sw - gap * (spb - 1)) / spb, cy = 16.0f, ch = r.getHeight() - 32 - (lockP >= 0 ? kLockBarH + 6 : 0.0f);
    for (int i = 0; i < spb; ++i)
    {
        const int si = first + i;
        const auto& s = tr.steps[(size_t) si];
        const float cx = sx + i * (cw + gap);
        if (lockP >= 0)
        {
            const float by = cy + ch + 6;
            g.setColour (col::bg); g.fillRoundedRectangle (cx, by, cw, kLockBarH, 5);
            const float lv = lockLane != nullptr ? lockLane->values[(size_t) si] : std::numeric_limits<float>::quiet_NaN();
            if (! std::isnan (lv)) { g.setColour (col::pattern); g.fillRoundedRectangle (cx, by, std::max (6.0f, cw * d->params()[lockP].toNorm (lv)), kLockBarH, 5); }
        }
        g.setColour ((i / 4) % 2 == 0 ? col::control : col::altCell);
        g.fillRoundedRectangle (cx, cy, cw, ch, 9);
        fb.hot ({ cx, cy, cw, ch }, 9);
        if (si == cursor) { g.setColour (col::record); g.drawRoundedRectangle (cx - 3, cy - 4, cw + 6, ch + 8, 11, 2.0f); }
        if (s.noteCount > 0)
        {
            const float vel = s.velocity() / 127.0f;
            g.setColour (colour.withAlpha (0.25f + 0.6f * vel)); g.fillRoundedRectangle (cx, cy, cw, ch, 9);
            auto label = chords::noteName (s.notes[0]);
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
    dragStep = -1; dragLock = -1;
    if (h.bar >= 0) { ctx.viewBar = h.bar; ctx.lockStep = -1; repaint(); return; }
    if (d->type() == DeviceType::Wavetable && juce::Rectangle<float> (16, (float) getHeight() - 38, 140, 26).contains (e.position))
    {
        juce::PopupMenu m;
        for (int k = 0; k < 4; ++k) m.addItem (k + 2, kStripLocks[k].label, true, stripLock (d) == k);
        m.addSeparator();
        m.addItem (1, "None", true, stripLock (d) < 0);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, d, sp = juce::Component::SafePointer<StepStrip> (this)] (int id)
        {
            if (sp == nullptr || id <= 0) return;
            stripLock (d) = id - 2;
            sig.clear(); repaint();
        });
        return;
    }
    if (h.step < 0) return;
    const int pat = ctx.focusPattern(), trk = ctx.focusTrack();
    const int si = ctx.viewBar * ctx.engine.project().stepsPerBar (pat) + h.step;
    if (const int lp = stripLockParam (d); lp >= 0 && e.position.y >= (float) getHeight() - 16 - kLockBarH - 3)
    {
        // a step's lock: along its bar (right-click: gone)
        ctx.snapshot (pat);
        dragStep = -1;
        dragLock = si;
        lockAt (e);
        return;
    }
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

void StepStrip::lockAt (const juce::MouseEvent& e)
{
    auto* d = ctx.focusDevice();
    const int lp = stripLockParam (d);
    if (lp < 0 || dragLock < 0) return;
    const auto& proj = ctx.engine.project();
    const int pat = ctx.focusPattern(), spb = proj.stepsPerBar (pat);
    const float sx = 172.0f, sw = (float) getWidth() - sx - 16, gap = 6.0f, cw = (sw - gap * (spb - 1)) / spb;
    const float cx = sx + (float) (dragLock - ctx.viewBar * spb) * (cw + gap);
    {
        const Engine::ScopedEdit edit (ctx.engine);
        auto& tr = ctx.engine.project().patterns[(size_t) pat].slots[(size_t) ctx.focusTrack()].track;
        if (auto* lane = tr.lockFor (lp, true))
            lane->values[(size_t) dragLock] = e.mods.isPopupMenu() ? std::numeric_limits<float>::quiet_NaN()
                                                                    : d->params()[lp].fromNorm (juce::jlimit (0.0f, 1.0f, (e.position.x - cx) / cw));
    }
    ctx.markDirty();
    sig.clear();
    repaint();
}

void StepStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragLock >= 0) { lockAt (e); return; }
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
