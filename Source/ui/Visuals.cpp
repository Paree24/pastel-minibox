#include "Visuals.h"
#include "devices/fm/FmDevice.h"
#include "devices/fm/Algorithms.h"
#include "devices/sampler/SamplerDevice.h"
#include "devices/drumsampler/DrumSamplerDevice.h"
#include "devices/drums/DrumsDevice.h"

namespace mb::ui
{
using R = juce::Rectangle<float>;

namespace
{
void strokeAndFill (juce::Graphics& g, const std::vector<juce::Point<float>>& pts, float baseY, juce::Colour c, float alpha = 0.14f, float sw = 2.0f)
{
    if (pts.size() < 2) return;
    juce::Path line, area;
    line.startNewSubPath (pts[0]);
    for (size_t i = 1; i < pts.size(); ++i) line.lineTo (pts[i]);
    area = line;
    area.lineTo (pts.back().x, baseY); area.lineTo (pts.front().x, baseY); area.closeSubPath();
    g.setColour (c.withAlpha (alpha));
    g.fillPath (area);
    g.setColour (c);
    g.strokePath (line, juce::PathStrokeType (sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

/** The load bar along the top of a sample well: the sample's name on the left, then Browse (factory
    sounds) and Load… (a file from disk) on the right. */
struct LoadBar { juce::Rectangle<float> browse, load; };
LoadBar loadBar (juce::Rectangle<float> r)
{
    const float y = r.getY() + 8, h = 20;
    LoadBar b;
    b.load = { r.getRight() - 10 - 58, y, 58, h };
    b.browse = { b.load.getX() - 6 - 64, y, 64, h };
    return b;
}
void drawLoadBar (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, const juce::String& name)
{
    const auto b = loadBar (r);
    for (auto [rect, label] : { std::pair { b.browse, "Browse" }, { b.load, "Load\xe2\x80\xa6" } })
    {
        g.setColour (col::control);
        g.fillRoundedRectangle (rect, 6);
        g.setColour (col::controlEdge);
        g.drawRoundedRectangle (rect.reduced (0.5f), 6, 1);
        paint::textCentred (g, juce::String (juce::CharPointer_UTF8 (label)), rect, 10, Weight::Bold, col::text);
    }
    const float maxW = b.browse.getX() - r.getX() - 22;
    juce::String shown = name.isEmpty() ? juce::String ("No sample") : name;
    while (shown.length() > 3 && textWidth (shown, 10, Weight::Bold) > maxW) shown = shown.dropLastCharacters (2).trimEnd() + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
    paint::text (g, shown, r.getX() + 12, r.getY() + 22, 10, Weight::Bold, name.isEmpty() ? col::faint : c);
}

/** A readable name for a loaded sample: factory sounds by their library name ("Classic Analog · Kick"),
    files by their file name. */
juce::String sampleDisplayName (const juce::String& ref, const SamplePtr& data)
{
    if (ref.startsWith ("factory:"))
    {
        static const auto names = []
        {
            std::map<juce::String, juce::String> m;
            const auto man = juce::JSON::parse (factoryDir().getChildFile ("samples").getChildFile ("manifest.json"));
            if (auto* a = man["sampler"].getArray()) for (auto& v : *a) m[v["file"].toString()] = v["name"].toString();
            static const std::pair<const char*, const char*> parts[8] = { { "_kick", "Kick" }, { "_snare", "Snare" }, { "_chat", "Closed Hat" }, { "_ohat", "Open Hat" },
                                                                         { "_clap", "Clap" }, { "_tom", "Tom" }, { "_cym", "Cymbal" }, { "_perc", "Perc" } };
            if (auto* kits = man["kits"].getArray())
                for (auto& kit : *kits)
                    if (auto* files = kit["files"].getArray())
                        for (auto& f : *files)
                            for (auto& [suffix, part] : parts)
                                if (f.toString().endsWith (suffix)) m[f.toString()] = kit["name"].toString() + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + part;
            return m;
        }();
        const auto file = ref.fromFirstOccurrenceOf ("factory:", false, false);
        if (auto it = names.find (file); it != names.end()) return it->second;
        if (file.contains ("_loop_"))
            return file.upToFirstOccurrenceOf ("_", false, false).substring (0, 1).toUpperCase() + file.upToFirstOccurrenceOf ("_", false, false).substring (1)
                   + " loop, " + file.fromLastOccurrenceOf ("_", false, false) + " BPM";
    }
    return data ? data->name : juce::String();
}

/** Keyswitch legend along the bottom of a visual: key name in the device colour, then what it selects
    (the active articulation in full text colour). */
void keysLegend (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, std::initializer_list<std::pair<const char*, const char*>> items, std::initializer_list<bool> active)
{
    float x = r.getX() + 10;
    auto on = active.begin();
    for (auto& [key, what] : items)
    {
        const auto k = juce::String (juce::CharPointer_UTF8 (key)), w = juce::String (what);
        paint::text (g, k, x, r.getBottom() - 8, 9, Weight::Black, juce::String (what).isEmpty() ? col::quiet : c);
        x += (float) k.length() * 5.6f + 4;
        paint::text (g, w, x, r.getBottom() - 8, 9, Weight::Bold, *on++ ? col::text : col::quiet);
        x += w.isEmpty() ? 2.0f : (float) w.length() * 5.2f + 12;
    }
}

void strokePts (juce::Graphics& g, const std::vector<juce::Point<float>>& pts, juce::Colour c, float sw = 2.0f)
{
    if (pts.size() < 2) return;
    juce::Path p;
    p.startNewSubPath (pts[0]);
    for (size_t i = 1; i < pts.size(); ++i) p.lineTo (pts[i]);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

float waveValue (int kind, float ph, float pw = 0.5f)   // 0 saw, 1 pulse, 2 tri, 3 sine, 4 square, 5 ramp up
{
    ph -= std::floor (ph);
    switch (kind)
    {
        case 0: return 1.0f - 2.0f * ph;
        case 1: return ph < pw ? 1.0f : -1.0f;
        case 2: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
        case 3: return std::sin (dsp::kTwoPi * ph);
        case 4: return ph < 0.5f ? 1.0f : -1.0f;
        case 5: return 2.0f * ph - 1.0f;
        default: return 0.0f;
    }
}

float sh (float ph)   // deterministic sample & hold steps for the LFO drawing
{
    static const float v[16] = { 0.6f, -0.3f, 0.9f, -0.8f, 0.1f, 0.5f, -0.6f, 0.3f, -0.1f, 0.8f, -0.9f, 0.2f, 0.7f, -0.4f, 0.0f, -0.7f };
    return v[(int) (std::floor (ph * 4.0f)) & 15];
}
} // namespace

namespace draw
{
void filter (juce::Graphics& g, R r, juce::Colour c, int mode, float cutHz, float res, int poles, bool grid)
{
    paint::well (g, r);
    if (grid)
    {
        g.setColour (col::divider);
        for (int i = 1; i < 4; ++i) g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * i / 4.0f), r.getY() + 8, r.getBottom() - 8);
        g.drawHorizontalLine (juce::roundToInt (r.getY() + r.getHeight() * 0.42f), r.getX() + 8, r.getRight() - 8);
    }
    const float q = 0.7f + res * 7.0f, pad = 10.0f;
    std::vector<juce::Point<float>> pts;
    const int N = 120;
    float minY = 1.0e9f, cutX = r.getX();
    for (int i = 0; i <= N; ++i)
    {
        const float fr = 20.0f * std::pow (1000.0f, (float) i / N);
        const float x = fr / std::max (20.0f, cutHz);
        const float den = std::sqrt ((1 - x * x) * (1 - x * x) + (x / q) * (x / q));
        float mag;
        if (mode == 1) mag = (x / q) / den * q * 0.6f;
        else if (mode == 2) mag = x * x / den;
        else if (mode == 3) mag = 1.0f / (1.0f + x * x);
        else mag = 1.0f / den;
        if (poles == 4 && mode == 0) mag *= 1.0f / std::sqrt ((1 - x * x) * (1 - x * x) + (x / 0.7f) * (x / 0.7f));
        const float db = juce::jlimit (-48.0f, 18.0f, 20.0f * std::log10 (std::max (mag, 1.0e-6f)));
        const float px = r.getX() + 6 + (r.getWidth() - 12) * i / N;
        const float py = r.getY() + pad + (18.0f - db) / 66.0f * (r.getHeight() - pad * 2);
        pts.push_back ({ px, py });
        if (fr <= cutHz) cutX = px;
        minY = std::min (minY, py);
    }
    strokeAndFill (g, pts, r.getBottom() - 4, c);
    float cy = minY;
    for (auto& p : pts) if (std::abs (p.x - cutX) < 3.0f) cy = p.y;
    g.setColour (c);
    g.fillEllipse (cutX - 4.5f, (mode == 0 ? std::min (cy, minY + 6) : cy) - 4.5f, 9, 9);
}

void adsr (juce::Graphics& g, R r, juce::Colour c, float a, float d, float s, float rel)
{
    paint::well (g, r);
    const float x0 = r.getX() + 10, y0 = r.getBottom() - 12, W = r.getWidth() - 20, H = r.getHeight() - 24;
    auto t = [] (float sec) { return 0.08f + std::log2 (1.0f + sec * 40.0f) / 9.0f; };   // compress long times
    const float ta = t (a), td = t (d), tr = t (rel), hold = 0.25f;
    const float sc = W / (ta + td + hold + tr);
    std::vector<juce::Point<float>> p = { { x0, y0 }, { x0 + ta * sc, y0 - H }, { x0 + (ta + td) * sc, y0 - H * s },
                                          { x0 + (ta + td + hold) * sc, y0 - H * s }, { x0 + W, y0 } };
    strokeAndFill (g, p, y0, c);
    for (int i = 1; i < 4; ++i)
    {
        g.setColour (col::bg); g.fillEllipse (p[(size_t) i].x - 3.5f, p[(size_t) i].y - 3.5f, 7, 7);
        g.setColour (c); g.drawEllipse (p[(size_t) i].x - 3.5f, p[(size_t) i].y - 3.5f, 7, 7, 1.8f);
    }
}

void wave (juce::Graphics& g, R r, juce::Colour c, std::function<float (float)> fn, float cycles, std::function<float (float)> overlay)
{
    paint::well (g, r);
    g.setColour (col::divider);
    g.drawHorizontalLine (juce::roundToInt (r.getCentreY()), r.getX() + 8, r.getRight() - 8);
    const float amp = r.getHeight() * 0.32f;
    auto trace = [&] (std::function<float (float)>& f, juce::Colour cc, float sw)
    {
        std::vector<juce::Point<float>> pts;
        for (int i = 0; i <= 240; ++i)
        {
            const float t = i / 240.0f;
            pts.push_back ({ r.getX() + 12 + (r.getWidth() - 24) * t, r.getCentreY() - juce::jlimit (-1.3f, 1.3f, f (t * cycles)) * amp });
        }
        strokePts (g, pts, cc, sw);
    };
    if (overlay) trace (overlay, col::quiet.withAlpha (0.6f), 1.5f);
    trace (fn, c, 2.2f);
}

void waveform (juce::Graphics& g, R r, juce::Colour c, const std::vector<float>& peaks, float from, float to, float dimBefore, float dimAfter)
{
    if (peaks.size() < 4) return;
    const int bins = (int) peaks.size() / 2;
    const int cols = std::max (1, (int) (r.getWidth() / 3.0f));
    for (int i = 0; i < cols; ++i)
    {
        const float t = from + (to - from) * (float) i / cols;
        const int b = juce::jlimit (0, bins - 1, (int) (t * bins));
        const float lo = peaks[(size_t) b * 2], hi = peaks[(size_t) b * 2 + 1];
        const float x = r.getX() + i * 3.0f + 1.0f;
        g.setColour (t < dimBefore || t > dimAfter ? col::faint : c);
        g.drawLine (x, r.getCentreY() - hi * r.getHeight() * 0.48f, x, r.getCentreY() - lo * r.getHeight() * 0.48f, 1.8f);
    }
}

void algo (juce::Graphics& g, R r, juce::Colour c, int algorithm, int selected)
{
    paint::well (g, r);
    const auto L = fm::layoutFor (algorithm);
    const auto& A = fm::algorithms()[(size_t) std::clamp (algorithm, 1, 32) - 1];
    int rows = 1;
    for (int s = 0; s < L.numStacks; ++s) rows = std::max (rows, L.stackLen[s]);
    const float bw = std::min (34.0f, (r.getWidth() - 30) / L.numStacks - 12), bh = std::min (24.0f, (r.getHeight() - 40) / rows - 10);
    const float busY = r.getBottom() - 16;
    std::map<int, juce::Point<float>> pos;
    for (int s = 0; s < L.numStacks; ++s)
    {
        const float cx = r.getX() + r.getWidth() * (s + 0.5f) / L.numStacks;
        const int off = rows - L.stackLen[s];
        for (int k = 0; k < L.stackLen[s]; ++k)
            pos[L.stacks[s][k]] = { cx, r.getY() + 16 + (k + off) * (r.getHeight() - 44) / rows + bh / 2 };
    }
    g.setColour (col::quiet);
    for (int to = 0; to < 6; ++to)
        for (int from = 0; from < 6; ++from)
            if ((A.mods[(size_t) to] >> from) & 1u)
                g.drawLine (pos[from].x, pos[from].y + bh / 2, pos[to].x, pos[to].y - bh / 2, 1.5f);
    float minX = 1e9f, maxX = -1e9f;
    for (int op = 0; op < 6; ++op)
    {
        const auto p = pos[op];
        const bool car = (A.carriers >> op) & 1u;
        if (car) { g.setColour (col::quiet); g.drawLine (p.x, p.y + bh / 2, p.x, busY, 1.5f); minX = std::min (minX, p.x); maxX = std::max (maxX, p.x); }
        const R box (p.x - bw / 2, p.y - bh / 2, bw, bh);
        g.setColour (car ? c : col::control);
        g.fillRoundedRectangle (box, 6.0f);
        if (! car) { g.setColour (c); g.drawRoundedRectangle (box, 6.0f, 1.0f); }
        paint::text (g, juce::String (op + 1), p.x, p.y + 4.5f, 12, Weight::Black, car ? col::bg : col::text, juce::Justification::horizontallyCentred);
        if (op == A.feedbackOp)
        {
            juce::Path fb;
            fb.startNewSubPath (p.x + bw / 2, p.y); fb.lineTo (p.x + bw / 2 + 8, p.y); fb.lineTo (p.x + bw / 2 + 8, p.y - bh * 0.9f);
            fb.lineTo (p.x, p.y - bh * 0.9f); fb.lineTo (p.x, p.y - bh / 2);
            g.setColour (col::quiet);
            g.strokePath (fb, juce::PathStrokeType (1.5f));
        }
        if (op == selected) { g.setColour (col::text); g.drawRoundedRectangle (box.expanded (4), 8.0f, 1.5f); }
    }
    g.setColour (col::quiet);
    g.drawLine (minX, busY, maxX, busY, 1.5f);
}

void ring (juce::Graphics& g, R r, juce::Colour c, float decay, float cycles)
{
    paint::well (g, r);
    std::vector<juce::Point<float>> pts;
    for (int i = 0; i <= 260; ++i)
    {
        const float t = i / 260.0f;
        pts.push_back ({ r.getX() + 10 + (r.getWidth() - 20) * t, r.getCentreY() - std::sin (dsp::kTwoPi * cycles * t) * std::exp (-t / std::max (0.03f, decay)) * r.getHeight() * 0.38f });
    }
    g.setColour (col::divider);
    g.drawHorizontalLine (juce::roundToInt (r.getCentreY()), r.getX() + 8, r.getRight() - 8);
    strokePts (g, pts, c, 1.8f);
}

void delayTaps (juce::Graphics& g, R r, juce::Colour c, float fb)
{
    paint::well (g, r);
    const float base = r.getBottom() - 10;
    for (int i = 0; i < 6; ++i)
    {
        const float x = r.getX() + 16 + i * (r.getWidth() - 32) / 5.5f;
        const float lvl = std::pow (std::max (0.05f, fb), (float) i);
        g.setColour (c.withAlpha (i == 0 ? 1.0f : 0.5f + 0.4f * lvl));
        g.fillRoundedRectangle (x - 4, base - (r.getHeight() - 24) * lvl, 8, (r.getHeight() - 24) * lvl, 4);
    }
}

void chorus (juce::Graphics& g, R r, juce::Colour c, int mode)
{
    paint::well (g, r);
    for (int k = 0; k < 2; ++k)
    {
        if (k == 1 && mode <= 1) continue;   // off: a flat line; mode I: one tap
        std::vector<juce::Point<float>> pts;
        const float depth = mode == 0 ? 0.0f : (mode == 3 ? 0.12f : 0.28f);
        const float rate = mode == 3 ? 6.0f : 2.0f;
        for (int i = 0; i <= 120; ++i)
        {
            const float t = i / 120.0f;
            pts.push_back ({ r.getX() + 10 + (r.getWidth() - 20) * t, r.getCentreY() - std::sin (dsp::kTwoPi * (t * rate + k * 0.5f)) * r.getHeight() * depth });
        }
        strokePts (g, pts, c.withAlpha (k == 0 ? 1.0f : 0.55f), 2.0f);
    }
}

void pads (juce::Graphics& g, R r, juce::Colour c, const juce::StringArray& names, int selected, const std::vector<bool>& lit, int cols,
           const std::vector<const std::vector<float>*>& waves)
{
    const float gap = 8.0f;
    const int rows = (names.size() + cols - 1) / cols;
    const float pw = (r.getWidth() - gap * (cols - 1)) / cols, ph = (r.getHeight() - gap * (rows - 1)) / rows;
    for (int i = 0; i < names.size(); ++i)
    {
        const R p (r.getX() + (i % cols) * (pw + gap), r.getY() + (i / cols) * (ph + gap), pw, ph);
        const bool on = i < (int) lit.size() && lit[(size_t) i], active = i == selected;
        g.setColour (on ? c.withAlpha (0.9f) : col::control);
        g.fillRoundedRectangle (p, 10.0f);
        if (active && ! on) { g.setColour (c); g.drawRoundedRectangle (p.reduced (0.75f), 10.0f, 1.5f); }
        if (i < (int) waves.size() && waves[(size_t) i] != nullptr)
            waveform (g, R (p.getX() + 8, p.getY() + ph * 0.38f, pw - 16, ph * 0.44f), on ? col::bg : (active ? c : col::quiet), *waves[(size_t) i]);
        paint::text (g, names[i], p.getX() + 10, p.getY() + 19, 11, active ? Weight::Black : Weight::Bold,
                     on ? col::bg : (active ? col::text : col::quiet));
    }
}
} // namespace draw

// ------------------------------------------------------------------------------------------- VisualView
VisualView::VisualView (AppContext& c, DeviceProcessor& d, VisualSpec s, juce::Colour colr, int pat, int trk)
    : ctx (c), dev (d), spec (std::move (s)), colour (colr), pattern (pat), track (trk)
{
    for (auto& id : spec.ids) idx.push_back (dev.params().indexOf (id));
    fb.keyHandler = [this] (int index, const juce::KeyPress& k) { return keyOnHot (index, k); };
}

juce::Rectangle<float> VisualView::waveRect() const
{
    const bool sampler = spec.variant == 0;
    const R r = getLocalBounds().toFloat();
    const float zh = sampler ? 30.0f : 0.0f;
    return { r.getX() + 12, r.getY() + 36, r.getWidth() - 24, r.getHeight() - 46 - zh };
}

float VisualView::markerPos (int marker) const
{
    if (spec.variant == 0)
    {
        const auto reg = static_cast<SamplerDevice&> (dev).region (0);
        return (float) (marker == 0 ? reg.s : (marker == 1 ? reg.ls : reg.e));
    }
    auto& ds = static_cast<DrumSamplerDevice&> (dev);
    const int p = ctx.selection (&dev);
    const float s = ds.get (ds.padParam (p, DrumSamplerDevice::Start));
    return marker == 0 ? s : s + ds.get (ds.padParam (p, DrumSamplerDevice::Length)) * (1 - s);
}

void VisualView::setMarker (int marker, float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    if (spec.variant == 0)
    {
        auto& sd = static_cast<SamplerDevice&> (dev);
        const auto reg = sd.region (0);
        if (marker == 0) { const double newS = std::min ((double) t, reg.e - 0.01); sd.set (SamplerDevice::start, (float) newS); sd.set (SamplerDevice::length, (float) ((reg.e - newS) / std::max (1.0e-6, 1.0 - newS))); }
        else if (marker == 2) sd.set (SamplerDevice::length, (float) ((std::max ((double) t, reg.s + 0.01) - reg.s) / std::max (1.0e-6, 1.0 - reg.s)));
        else sd.set (SamplerDevice::loopStart, (float) ((t - reg.s) / std::max (1.0e-6, reg.e - reg.s)));
    }
    else
    {
        auto& ds = static_cast<DrumSamplerDevice&> (dev);
        const int p = ctx.selection (&dev);
        const float s = ds.get (ds.padParam (p, DrumSamplerDevice::Start));
        const float en = s + ds.get (ds.padParam (p, DrumSamplerDevice::Length)) * (1 - s);
        if (marker == 0) { const float ns = std::min (t, en - 0.01f); ds.set (ds.padParam (p, DrumSamplerDevice::Start), ns); ds.set (ds.padParam (p, DrumSamplerDevice::Length), (en - ns) / std::max (1.0e-6f, 1 - ns)); }
        else ds.set (ds.padParam (p, DrumSamplerDevice::Length), (std::max (t, s + 0.01f) - s) / std::max (1.0e-6f, 1 - s));
    }
    ctx.markDirty();
    repaint();
}

bool VisualView::keyOnHot (int index, const juce::KeyPress& k)
{
    // the arrows drag what the mouse drags: an envelope point (← → its time, ↑ ↓ its level), a sample marker
    // (← →, Shift: fine), a lane step's value (↑ ↓)
    if (index < 0 || index >= (int) hotUse.size()) return false;
    const auto use = hotUse[(size_t) index];
    const int code = k.getKeyCode();
    const int dx = code == juce::KeyPress::leftKey ? -1 : (code == juce::KeyPress::rightKey ? 1 : 0);
    const int dy = code == juce::KeyPress::upKey ? 1 : (code == juce::KeyPress::downKey ? -1 : 0);
    const bool fine = k.getModifiers().isShiftDown();
    if (dx == 0 && dy == 0) return false;
    if (use.kind == 1)
    {
        auto setK = [&] (int kk, float val) { if (kk < (int) idx.size() && idx[(size_t) kk] >= 0) dev.set (idx[(size_t) kk], juce::jlimit (0.0f, 1.0f, val)); };
        if (dx != 0) setK (use.a, v (use.a) - dx * (fine ? 0.005f : 0.03f));
        if (dy != 0 && use.a < 3) setK (4 + use.a, v (4 + use.a) + dy * (fine ? 0.005f : 0.03f));
        ctx.markDirty();
        repaint();
        return true;
    }
    if (use.kind == 2 && dx != 0) { setMarker (use.a, markerPos (use.a) + dx * (fine ? 0.001f : 0.005f)); return true; }
    if (use.kind == 3 && dy != 0 && pattern >= 0 && track >= 0)
    {
        const int spb = ctx.engine.project().stepsPerBar (pattern);
        const Engine::ScopedEdit edit (ctx.engine);
        auto& d = ctx.engine.project().patterns[(size_t) pattern].slots[(size_t) track].track.lanes[(size_t) ctx.selection (&dev)][(size_t) (ctx.viewBar * spb + use.a)];
        if (! d.on) return false;
        if (ctx.laneView == 0) d.velocity = (uint8_t) juce::jlimit (1, 127, (int) d.velocity + dy * (fine ? 1 : 8));
        else if (ctx.laneView == 1) d.probability = (uint8_t) juce::jlimit (0, 100, (int) d.probability + dy * (fine ? 1 : 10));
        else d.ratchet = (uint8_t) juce::jlimit (1, 4, (int) d.ratchet + dy);
        ctx.markDirty();
        repaint();
        return true;
    }
    return false;
}

float VisualView::v (int k) const
{
    if (k < 0 || k >= (int) idx.size() || idx[(size_t) k] < 0) return 0.0f;
    return dev.pv (idx[(size_t) k]);   // automation moves the curve too
}

uint64_t VisualView::signature() const
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h] (uint64_t x) { h ^= x; h *= 1099511628211ull; };
    for (int i : idx) if (i >= 0) { float f = dev.pv (i); uint32_t u; std::memcpy (&u, &f, 4); mix (u); }
    mix ((uint64_t) ctx.selection (&dev));
    mix ((uint64_t) ctx.viewBar * 31 + (uint64_t) ctx.laneView);
    for (auto& w : dev.heldNotes) mix (w.load());
    if (auto* dr = dynamic_cast<DrumsDevice*> (&dev)) for (auto& hc : dr->hits) mix ((uint64_t) hc.load());
    if (auto* ds = dynamic_cast<DrumSamplerDevice*> (&dev))
    {
        for (auto& hc : ds->hits) mix ((uint64_t) hc.load());
        for (int p = 0; p < 8; ++p) mix ((uint64_t) (uintptr_t) ds->pad[p].get().get());
    }
    if (auto* sd = dynamic_cast<SamplerDevice*> (&dev)) for (int z = 0; z < 8; ++z) mix ((uint64_t) (uintptr_t) sd->zone[z].get().get());
    if (spec.kind == VisualKind::Lane && pattern >= 0 && track >= 0)
    {
        const auto& lanes = ctx.engine.project().patterns[(size_t) pattern].slots[(size_t) track].track.lanes;
        const int lane = ctx.selection (&dev);
        for (int i = 0; i < 64; ++i) { const auto& ds = lanes[(size_t) lane][(size_t) i]; mix ((uint64_t) ds.on * 1000 + ds.velocity * 7 + ds.probability * 3 + ds.ratchet); }
        mix ((uint64_t) ctx.engine.currentStep());
    }
    mix (dropHover ? 1 : 0);
    return h;
}

void VisualView::refresh()
{
    const auto s = signature();
    if (s != lastSig) { lastSig = s; repaint(); }
}

void VisualView::paint (juce::Graphics& g)
{
    const R r = inner();
    switch (spec.kind)
    {
        case VisualKind::Filter:
        {
            const int mode = spec.variant == 2 ? iv (2) : (spec.variant == 3 ? 3 : 0);
            draw::filter (g, r, colour, mode, v (0), spec.variant == 3 ? 0.0f : v (1), spec.variant == 0 ? 4 : 2);
            break;
        }
        case VisualKind::Hpf:
        {
            paint::well (g, r);
            const int sel = iv (0);
            static constexpr float cuts[4] = { 0.0f, 0.18f, 0.3f, 0.42f };
            for (int i = 0; i < 4; ++i)
            {
                std::vector<juce::Point<float>> pts;
                for (int k = 0; k <= 60; ++k)
                {
                    const float t = k / 60.0f;
                    float db;
                    if (i == 0) db = t < 0.12f ? 4.0f : std::max (0.0f, 4.0f - (t - 0.12f) * 40.0f);
                    else { const float x = (t + 0.001f) / cuts[i]; db = 20.0f * std::log10 (x * x / std::sqrt ((1 - x * x) * (1 - x * x) + (x / 0.7f) * (x / 0.7f)) + 1.0e-6f); }
                    db = juce::jlimit (-36.0f, 8.0f, db);
                    pts.push_back ({ r.getX() + 8 + (r.getWidth() - 16) * t, r.getY() + 14 + (8 - db) / 44.0f * (r.getHeight() - 28) });
                }
                if (i == sel) strokeAndFill (g, pts, r.getBottom() - 4, colour);
                else strokePts (g, pts, col::faint, 1.2f);
            }
            break;
        }
        case VisualKind::Adsr: draw::adsr (g, r, colour, v (0), v (1), v (2), v (3)); break;
        case VisualKind::Decay:
        {
            // attack + exponential decay (variant 1: gate / decay amp of the acid voice)
            if (spec.variant == 1 && iv (0) == 0) { draw::adsr (g, r, colour, 0.002f, 0.05f, 0.9f, 0.01f); break; }
            paint::well (g, r);
            const float x0 = r.getX() + 10, y0 = r.getBottom() - 12, W = r.getWidth() - 20, H = r.getHeight() - 24;
            const float atk = spec.variant == 1 ? 0.0f : v (0), dec = spec.variant == 1 ? v (1) : v (1);
            std::vector<juce::Point<float>> p { { x0, y0 } };
            const float ax = W * juce::jlimit (0.0f, 0.4f, atk / 1.0f);
            p.push_back ({ x0 + ax + 1, y0 - H });
            for (int i = 1; i <= 60; ++i)
            {
                const float t = i / 60.0f;
                p.push_back ({ x0 + ax + (W - ax) * t, y0 - H * std::exp (-t * 2.5f / std::max (0.02f, dec)) });
            }
            strokeAndFill (g, p, y0, colour);
            break;
        }
        case VisualKind::Rate4:
        case VisualKind::MacroEnv:
        {
            paint::well (g, r);
            const float x0 = r.getX() + 10, y0 = r.getBottom() - 12, W = r.getWidth() - 20, H = r.getHeight() - 24;
            float rates[4], lv[4];
            if (spec.kind == VisualKind::Rate4)
            {
                for (int k = 0; k < 4; ++k) { rates[k] = v (k); lv[k] = v (4 + k); }
            }
            else
            {
                // macro layer: offsets drawn on a typical envelope (attack, decay, level)
                const float a = v (0), d = v (1), l = v (2);
                rates[0] = 0.85f - a * 0.35f; rates[1] = 0.55f - d * 0.3f; rates[2] = 0.5f - d * 0.3f; rates[3] = 0.6f;
                lv[0] = juce::jlimit (0.1f, 1.0f, 0.85f + l * 0.25f); lv[1] = lv[0] * 0.7f; lv[2] = lv[0] * 0.55f; lv[3] = 0.0f;
            }
            float widths[4], tot = 0.0f;
            for (int k = 0; k < 4; ++k) { widths[k] = 0.12f + (1.0f - rates[k]) * 0.9f; tot += widths[k]; }
            std::vector<juce::Point<float>> p { { x0, y0 } };
            float cx = x0;
            for (int k = 0; k < 4; ++k) { cx += W * widths[k] / tot; p.push_back ({ cx, y0 - H * lv[k] }); }
            strokeAndFill (g, p, y0, colour);
            for (size_t k = 1; k < p.size(); ++k)
            {
                g.setColour (col::bg); g.fillEllipse (p[k].x - 3.5f, p[k].y - 3.5f, 7, 7);
                g.setColour (colour); g.drawEllipse (p[k].x - 3.5f, p[k].y - 3.5f, 7, 7, 1.6f);
            }
            break;
        }
        case VisualKind::PitchEnv:
        {
            paint::well (g, r);
            const float x0 = r.getX() + 10, y0 = r.getBottom() - 12, W = r.getWidth() - 20, H = r.getHeight() - 24;
            std::vector<juce::Point<float>> p;
            for (int i = 0; i <= 80; ++i)
            {
                const float t = i / 80.0f;
                float y;
                if (spec.variant == 0) y = 0.18f + 0.82f * v (0) * std::exp (-t * 1.2f / std::max (0.004f, v (1)));     // drums: bend, bend time
                else if (spec.variant == 1) { const float atk = v (1), dec = v (2), env = t * 2.0f < atk ? t * 2.0f / std::max (atk, 1e-3f) : std::exp (-(t * 2.0f - atk) / std::max (0.01f, dec)); y = 0.5f + 0.45f * v (0) * env; }
                else if (spec.variant == 2) y = 0.5f + 0.45f * v (0) * std::exp (-t * 2.0f / std::max (0.005f, v (1)));       // drum sampler PEG
                else
                {
                    // FM pitch EG: three rate/level segments
                    const float seg = t * 3.0f;
                    const int k = std::min (2, (int) seg);
                    const float from = k == 0 ? 0.0f : v (3 + k - 1), to = v (3 + k);
                    y = 0.5f + 0.45f * (from + (to - from) * std::min (1.0f, (seg - k) * (0.5f + v (k))));
                }
                p.push_back ({ x0 + W * t, y0 - H * juce::jlimit (0.0f, 1.0f, y) });
            }
            strokeAndFill (g, p, y0, colour);
            break;
        }
        case VisualKind::Wave:
        {
            std::function<float (float)> fn, overlay;
            if (spec.variant == 0)   // Mono: pulse, saw, sub, noise mix
            {
                const float mp = v (0), ms = v (1), msub = v (2), pw = v (4);
                const float norm = 1.0f / std::max (0.5f, mp + ms + msub * 0.8f);
                fn = [=] (float ph) { return (mp * waveValue (1, ph, pw) + ms * waveValue (0, ph) + msub * 0.8f * waveValue (4, ph * 0.5f)) * norm; };
            }
            else if (spec.variant == 1)   // Poly DCO
            {
                const int w = iv (0); const float pw = v (1);
                fn = [=] (float ph) { return w == 0 ? waveValue (1, ph, pw) : (w == 1 ? waveValue (0, ph) : 0.6f * (waveValue (0, ph) + waveValue (1, ph, pw))); };
                if (v (2) > 0.01f) overlay = [] (float ph) { return 0.55f * waveValue (4, ph * 0.5f); };
            }
            else if (spec.variant == 2)   // Poly VA
            {
                const int w = iv (0); const float pw = v (1);
                fn = [=] (float ph) { return waveValue (w == 0 ? 0 : (w == 1 ? 1 : 2), ph, pw); };
                if (v (2) > 0.01f) overlay = [] (float ph) { return 0.55f * waveValue (4, ph * 0.5f); };
            }
            else if (spec.variant == 4)   // Bassline pulse: the width, with the PWM sweep as a lighter overlay
            {
                const float pw = v (0), depth = v (1);
                fn = [=] (float ph) { return waveValue (1, ph, pw); };
                if (depth > 0.01f) overlay = [=] (float ph) { return 0.55f * waveValue (1, ph, juce::jlimit (0.05f, 0.95f, pw + 0.35f * depth)); };
            }
            else   // Acid: saw ↔ square morph
            {
                const float m = v (0), pw = v (1);
                fn = [=] (float ph) { return (1.0f - m) * waveValue (0, ph) + m * waveValue (1, ph, 0.5f + (pw - 0.5f) * m); };
            }
            draw::wave (g, r, colour, fn, 2.0f, overlay);
            break;
        }
        case VisualKind::Lfo:
        {
            const int s = iv (0);
            static constexpr int maps[3][6] = { { 2, 4, 6, 7, 0, 0 }, { 2, 0, 4, 6, 0, 0 }, { 3, 2, 0, 5, 4, 6 } };
            const int kind = maps[std::clamp (spec.variant, 0, 2)][std::clamp (s, 0, 5)];
            draw::wave (g, r, colour, [kind] (float ph)
            {
                if (kind == 6) return sh (ph);
                if (kind == 7) { const float a = sh (ph), b = sh (ph + 0.25f); const float f = ph * 4.0f - std::floor (ph * 4.0f); return a + (b - a) * f * f * (3 - 2 * f); }
                return waveValue (kind, ph);
            }, 2.0f);
            break;
        }
        case VisualKind::Algo: draw::algo (g, r, colour, iv (0), ctx.selection (&dev)); break;
        case VisualKind::Ops:
        {
            paint::well (g, r);
            auto& fmd = static_cast<FmDevice&> (dev);
            const float cw = (r.getWidth() - 16) / 6.0f;
            static const char* waves[4] = { "Sine", "Half", "Sq", "Saw" };
            for (int o = 0; o < 6; ++o)
            {
                const float cx = r.getX() + 8 + cw * o;
                const bool sel = o == ctx.selection (&dev), car = fmd.isCarrier (o);
                const bool muted = fmd.get (fmd.opParam (o, FmDevice::Mute)) > 0.5f;
                if (sel) { g.setColour (col::surface); g.fillRoundedRectangle (cx + 3, r.getY() + 6, cw - 6, r.getHeight() - 12, 8); g.setColour (colour); g.drawRoundedRectangle (cx + 3, r.getY() + 6, cw - 6, r.getHeight() - 12, 8, 1.5f); }
                paint::text (g, juce::String (o + 1), cx + cw / 2, r.getY() + 26, 13, Weight::Black, col::text, juce::Justification::horizontallyCentred);
                g.setColour (car ? colour : col::control);
                g.fillRoundedRectangle (cx + cw / 2 - 11, r.getY() + 34, 22, 16, 8);
                paint::text (g, car ? "C" : "M", cx + cw / 2, r.getY() + 46, 10, Weight::Black, car ? col::bg : col::quiet, juce::Justification::horizontallyCentred);
                const bool fixed = fmd.get (fmd.opParam (o, FmDevice::Fixed)) > 0.5f;
                const auto ratio = fixed ? juce::String (juce::roundToInt (fmd.get (fmd.opParam (o, FmDevice::Freq)))) + "Hz"
                                         : juce::String (fmd.get (fmd.opParam (o, FmDevice::Ratio)), 2);
                paint::text (g, ratio, cx + cw / 2, r.getY() + 68, 11, Weight::Bold, col::quiet, juce::Justification::horizontallyCentred);
                const float barTop = r.getY() + 78, barH = r.getHeight() - 170;
                const float lvl = fmd.get (fmd.opParam (o, FmDevice::Level));
                g.setColour (col::control); g.fillRoundedRectangle (cx + cw / 2 - 5, barTop, 10, barH, 5);
                g.setColour (muted ? col::faint : colour); g.fillRoundedRectangle (cx + cw / 2 - 5, barTop + barH * (1 - lvl), 10, barH * lvl, 5);
                paint::text (g, waves[std::clamp ((int) fmd.get (fmd.opParam (o, FmDevice::Wave)), 0, 3)], cx + cw / 2, r.getBottom() - 70, 10, Weight::Bold, col::faint, juce::Justification::horizontallyCentred);
                const float ey = r.getBottom() - 58, ew = cw - 16;
                const float l1 = fmd.get (fmd.opParam (o, FmDevice::L1)), l3 = fmd.get (fmd.opParam (o, FmDevice::L3));
                const float r1 = fmd.get (fmd.opParam (o, FmDevice::R1));
                strokePts (g, { { cx + 8, ey + 26 }, { cx + 8 + ew * 0.15f * (1.0f - r1) + 1, ey + 26 * (1 - l1) }, { cx + 8 + ew * 0.55f, ey + 26 * (1 - l3) },
                                { cx + 8 + ew * 0.8f, ey + 26 * (1 - l3) }, { cx + 8 + ew, ey + 26 } }, sel ? colour : col::quiet, 1.5f);
                g.setColour (muted ? col::faint : colour);
                g.fillEllipse (cx + cw / 2 - 4, r.getBottom() - 22, 8, 8);
            }
            break;
        }
        case VisualKind::Sample:
        {
            paint::well (g, r);
            const bool sampler = spec.variant == 0;
            SamplePtr data;
            double s = 0, e = 1, ls = -1, le = -1;
            if (sampler)
            {
                auto& sd = static_cast<SamplerDevice&> (dev);
                data = sd.zone[ctx.selection (&dev)].get();
                const auto reg = sd.region (0);
                s = reg.s; e = reg.e;
                if ((int) sd.get (SamplerDevice::playMode) == 2) { ls = reg.ls; le = reg.le; }
            }
            else
            {
                auto& ds = static_cast<DrumSamplerDevice&> (dev);
                const int p = ctx.selection (&dev);
                data = ds.pad[p].get();
                s = ds.get (ds.padParam (p, DrumSamplerDevice::Start));
                e = s + ds.get (ds.padParam (p, DrumSamplerDevice::Length)) * (1.0 - s);
            }
            const float zh = sampler ? 30.0f : 0.0f;
            const R wr (r.getX() + 12, r.getY() + 36, r.getWidth() - 24, r.getHeight() - 46 - zh);
            drawLoadBar (g, r, colour, sampleDisplayName (sampler ? static_cast<SamplerDevice&> (dev).zoneRef (ctx.selection (&dev))
                                                                  : static_cast<DrumSamplerDevice&> (dev).padRef (ctx.selection (&dev)), data));
            if (! data)
            {
                paint::textCentred (g, dropHover ? "Drop to load" : "Drop a sample, or Browse / Load", wr, 12, Weight::Bold, dropHover ? colour : col::faint);
            }
            else
            {
                if (ls >= 0) { g.setColour (colour.withAlpha (0.1f)); g.fillRect (R (wr.getX() + wr.getWidth() * (float) ls, wr.getY(), wr.getWidth() * (float) (le - ls), wr.getHeight())); }
                draw::waveform (g, wr, colour, data->peaks, 0, 1, (float) s, (float) e);
                auto marker = [&] (const char* lab, double pos)
                {
                    const float mx = wr.getX() + wr.getWidth() * (float) pos;
                    g.setColour (col::text);
                    g.drawLine (mx, wr.getY(), mx, wr.getBottom(), 1.5f);
                    g.fillRoundedRectangle (mx - 9, wr.getY(), 18, 16, 4);
                    paint::text (g, lab, mx, wr.getY() + 12, 10, Weight::Black, col::bg, juce::Justification::horizontallyCentred);
                };
                marker ("S", s);
                if (ls >= 0) marker ("L", ls);
                marker ("E", e);
            }
            if (sampler)
            {
                // key zones: lo/hi ranges with their root, selected zone highlighted
                auto& sd = static_cast<SamplerDevice&> (dev);
                const R zr (wr.getX(), r.getBottom() - zh - 4, wr.getWidth(), zh);
                const float kw = zr.getWidth() / 128.0f;
                for (int k = 0; k < 128; k += 1)
                {
                    const bool black = juce::MidiMessage::isMidiNoteBlack (k);
                    g.setColour (black ? col::divider : col::control);
                    g.fillRect (zr.getX() + k * kw, zr.getY() + 4, kw - 0.5f, zh - 8);
                }
                for (int z = 0; z < SamplerDevice::kZones; ++z)
                {
                    if (sd.zoneRef (z).isEmpty()) continue;
                    const float lo = sd.get (sd.zoneLo (z)), hi = sd.get (sd.zoneHi (z)), root = sd.get (sd.zoneRoot (z));
                    const bool sel = z == ctx.selection (&dev);
                    g.setColour ((sel ? colour : col::faint).withAlpha (sel ? 0.35f : 0.25f));
                    g.fillRoundedRectangle (zr.getX() + lo * kw, zr.getY(), (hi - lo + 1) * kw - 1, zh, 5);
                    g.setColour (sel ? colour : col::quiet);
                    g.fillEllipse (zr.getX() + (root + 0.5f) * kw - 4, zr.getCentreY() - 4, 8, 8);
                }
            }
            if (dropHover) { g.setColour (colour); g.drawRoundedRectangle (r.reduced (1), 8, 2); }
            break;
        }
        case VisualKind::Slices:
        {
            paint::well (g, r);
            auto& ds = static_cast<DrumSamplerDevice&> (dev);
            auto data = ds.pad[0].get();
            bool shared = data != nullptr;
            for (int p = 1; p < 8 && shared; ++p) shared = ds.padRef (p) == ds.padRef (0);
            const R wr (r.getX() + 10, r.getY() + 36, r.getWidth() - 20, r.getHeight() - 44);
            drawLoadBar (g, r, colour, shared ? sampleDisplayName (ds.padRef (0), data) : juce::String());
            if (! shared)
            {
                paint::textCentred (g, dropHover ? "Drop a loop to slice" : "Drop a loop, or Browse / Load", wr, 12, Weight::Bold, dropHover ? colour : col::faint);
            }
            else
            {
                draw::waveform (g, wr, colour.withAlpha (0.85f), data->peaks);
                const int sel = ctx.selection (&dev);
                for (int p = 0; p < 8; ++p)
                {
                    const float x = wr.getX() + wr.getWidth() * ds.get (ds.padParam (p, DrumSamplerDevice::Start));
                    if (p == sel)
                    {
                        const float e = ds.get (ds.padParam (p, DrumSamplerDevice::Start)) + ds.get (ds.padParam (p, DrumSamplerDevice::Length)) * (1 - ds.get (ds.padParam (p, DrumSamplerDevice::Start)));
                        g.setColour (colour.withAlpha (0.14f));
                        g.fillRect (R (x, wr.getY(), wr.getX() + wr.getWidth() * e - x, wr.getHeight()));
                    }
                    g.setColour ((p == 0 ? col::quiet : col::text).withAlpha (0.8f));
                    g.drawVerticalLine (juce::roundToInt (x), wr.getY(), wr.getBottom());
                    paint::text (g, juce::String (p + 1), x + 5, wr.getY() + 10, 9, Weight::Black, col::quiet);
                }
            }
            if (dropHover) { g.setColour (colour); g.drawRoundedRectangle (r.reduced (1), 8, 2); }
            break;
        }
        case VisualKind::Pads:
        {
            paint::well (g, r);
            juce::StringArray names;
            std::vector<bool> lit (8, false);
            std::vector<const std::vector<float>*> waves;
            std::vector<SamplePtr> keep;
            for (int i = 0; i < 8; ++i) names.add (dev.laneName (i));
            if (auto* ds = dynamic_cast<DrumSamplerDevice*> (&dev))
                for (int i = 0; i < 8; ++i) { keep.push_back (ds->pad[i].get()); waves.push_back (keep.back() ? &keep.back()->peaks : nullptr); }
            for (int i = 0; i < 8; ++i) lit[(size_t) i] = ((dev.heldNotes[(size_t) (dev.laneNote (i) >> 5)].load() >> (dev.laneNote (i) & 31)) & 1u) != 0;
            draw::pads (g, r.reduced (8), colour, names, ctx.selection (&dev), lit, spec.variant == 2 ? 4 : 2, waves);
            break;
        }
        case VisualKind::Lane:
        {
            paint::well (g, r);
            if (pattern < 0 || track < 0) break;
            const auto& proj = ctx.engine.project();
            const int spb = proj.stepsPerBar (pattern);
            const int lane = ctx.selection (&dev);
            const auto& steps = proj.patterns[(size_t) pattern].slots[(size_t) track].track.lanes[(size_t) lane];
            const int first = ctx.viewBar * spb;
            const float gap = 4.0f, bw = (r.getWidth() - 16 - gap * (spb - 1)) / spb;
            const int playing = ctx.engine.isPlaying() && ctx.engine.activePattern() == pattern ? ctx.engine.currentStep() - first : -1;
            for (int i = 0; i < spb; ++i)
            {
                const float bx = r.getX() + 8 + i * (bw + gap);
                const R cell (bx, r.getY() + 8, bw, r.getHeight() - 16);
                g.setColour (col::control);
                g.fillRoundedRectangle (cell, 3);
                const auto& d = steps[(size_t) (first + i)];
                if (d.on)
                {
                    g.setColour (colour);
                    if (ctx.laneView == 2)
                    {
                        const float seg = cell.getHeight() / 4.0f;
                        for (int k = 0; k < d.ratchet; ++k) g.fillRoundedRectangle (bx, cell.getBottom() - (k + 1) * seg + 2, bw, seg - 3, 2);
                    }
                    else
                    {
                        const float val = ctx.laneView == 0 ? d.velocity / 127.0f : d.probability / 100.0f;
                        g.fillRoundedRectangle (bx, cell.getY() + cell.getHeight() * (1 - val), bw, cell.getHeight() * val, 3);
                    }
                }
                if (i % 4 == 0) { g.setColour (col::faint); g.fillEllipse (bx + bw / 2 - 1.5f, r.getBottom() - 4.5f, 3, 3); }
                if (i == playing) { g.setColour (col::pattern); g.drawRoundedRectangle (cell.expanded (2, 3), 4, 1.5f); }
            }
            break;
        }
        case VisualKind::Accent:
        {
            paint::well (g, r);
            const float acc = v (0), sweep = v (1);
            static const int pattern8[8] = { 1, 0, 1, 1, 1, 0, 0, 1 };
            float charge = 0.0f;
            const float bw = (r.getWidth() - 20) / 8.0f, base = r.getBottom() - 24;
            const int art = iv (2);
            keysLegend (g, r, colour, { { "Hold", "" }, { "C\xe2\x99\xaf" "0", "Accent" }, { "D\xe2\x99\xaf" "0", "Acc+Slide" } }, { false, art == 1, art == 3 });
            for (int i = 0; i < 8; ++i)
            {
                const bool a = pattern8[i];
                charge = a ? std::min (1.0f, charge + 0.15f + sweep * 0.3f) : charge * 0.35f;
                const float lvl = a ? 0.25f + 0.65f * charge * (0.4f + acc * 0.6f) : 0.18f;
                const float bx = r.getX() + 10 + i * bw;
                std::vector<juce::Point<float>> p { { bx + 3, base }, { bx + 5, base - (r.getHeight() - 38) * lvl } };
                for (int k = 1; k <= 12; ++k) p.push_back ({ bx + 5 + (bw - 10) * k / 12.0f, base - (r.getHeight() - 38) * lvl * std::exp (-k / 4.0f) });
                strokeAndFill (g, p, base, a ? colour : col::faint, a ? 0.14f : 0.1f, 1.8f);
                if (a) { g.setColour (colour); g.fillRoundedRectangle (bx + bw / 2 - 7, r.getY() + 6, 14, 13, 4); paint::text (g, "A", bx + bw / 2, r.getY() + 16, 9, Weight::Black, col::bg, juce::Justification::horizontallyCentred); }
            }
            break;
        }
        case VisualKind::Reverb:
        {
            // the dry hit, the pre-delay gap, then a dense decaying tail: longer with Size, finer detail with
            // Tone, taller with Mix
            paint::well (g, r);
            const float mix = v (0), size = v (1), tone = v (2), pre = v (3) / 0.1f;
            const float base = r.getBottom() - 12, top = r.getY() + 12, x0 = r.getX() + 12;
            g.setColour (col::quiet);
            g.drawLine (x0, base, x0, top + 4, 2.2f);
            const float gap = 6 + pre * (r.getWidth() - 30) * 0.45f;
            const float span = r.getWidth() - 24 - gap, rt = 0.25f + 0.75f * size;
            const int bars = (int) (span / 3);
            uint32_t seed = 7;
            std::vector<juce::Point<float>> env;
            g.setColour (colour);
            for (int i = 0; i < bars; ++i)
            {
                seed = (seed * 1103515245u + 12345u) & 0x7fffffffu;
                const float jit = 0.55f + 0.45f * (float) seed / (float) 0x7fffffff * (0.4f + 0.6f * tone);
                const float t = (float) i / (float) std::max (1, bars - 1);
                const float e = std::exp (-t * 3.2f / rt) * std::min (1.0f, (float) i / 4.0f + 0.25f);
                const float hgt = (base - top) * (0.3f + 0.7f * mix) * e;
                const float bx = x0 + gap + (float) i * 3.0f;
                g.drawLine (bx, base, bx, base - hgt * jit, 1.6f);
                env.push_back ({ bx, base - hgt });
            }
            strokeAndFill (g, env, base, colour, 0.12f, 1.4f);
            break;
        }
        case VisualKind::Ring: draw::ring (g, r, colour, 0.15f + v (1) * 0.5f, 4.0f + std::log2 (std::max (40.0f, v (0)) / 40.0f) * 4.0f); break;
        case VisualKind::Chorus: draw::chorus (g, r, colour, spec.variant == 1 ? iv (0) + 1 : iv (0)); break;
        case VisualKind::Delay: draw::delayTaps (g, r, colour, v (0)); break;
        case VisualKind::Slide:
        {
            paint::well (g, r);
            static const int steps[8] = { 3, 3, 7, 5, 5, 10, 8, 8 };
            const float bw = (r.getWidth() - 20) / 8.0f;
            const float time = juce::jlimit (0.05f, 0.95f, v (0) * 2.0f);
            const bool keyed = spec.variant == 0;   // the bass-line voice: slide is also a keyswitch articulation
            const float foot = keyed ? 12.0f : 0.0f;
            auto py = [&] (int s) { return r.getBottom() - 16 - foot - (s - 3) / 7.0f * (r.getHeight() - 34 - foot); };
            if (keyed) { const int art = iv (1); keysLegend (g, r, colour, { { "Hold", "" }, { "D0", "Slide" } }, { false, (art & 2) != 0 }); }
            const bool slides = spec.variant == 0 || iv (1) > 0;
            g.setColour (colour);
            for (int i = 0; i < 8; ++i)
            {
                const float bx = r.getX() + 10 + i * bw;
                const bool glide = slides && (i == 1 || i == 4);
                if (glide && i + 1 < 8)
                {
                    const float split = bw * (1.0f - time * 0.8f);
                    g.drawLine (bx + 2, py (steps[i]), bx + split, py (steps[i]), 2.2f);
                    juce::Path p;
                    p.startNewSubPath (bx + split, py (steps[i]));
                    p.cubicTo (bx + split + (bw - split) * 0.5f, py (steps[i]), bx + bw - 4, py (steps[i + 1]), bx + bw + 2, py (steps[i + 1]));
                    g.strokePath (p, juce::PathStrokeType (2.2f));
                }
                else g.drawLine (bx + 2, py (steps[i]), bx + bw - 4, py (steps[i]), 2.2f);
                g.fillEllipse (bx, py (steps[i]) - 2.5f, 5, 5);
            }
            break;
        }
        case VisualKind::Drive:
        {
            paint::well (g, r);
            const float cx = r.getCentreX(), cy = r.getCentreY(), sz = std::min (r.getWidth(), r.getHeight()) / 2 - 12;
            g.setColour (col::divider);
            g.drawLine (cx - sz, cy, cx + sz, cy); g.drawLine (cx, cy - sz, cx, cy + sz);
            g.setColour (col::faint);
            const float dashes[] = { 3.0f, 4.0f };
            g.drawDashedLine ({ cx - sz, cy + sz, cx + sz, cy - sz }, dashes, 2);
            const float amount = v (0), gain = 1.0f + amount * 5.0f;
            const int type = iv (1);
            std::vector<juce::Point<float>> p;
            for (int i = 0; i <= 80; ++i)
            {
                const float t = -1.0f + 2.0f * i / 80.0f;
                float y;
                if (spec.variant == 1)   // the pedals: overdrive (soft, plus clean), distortion (hard diodes), fuzz (two stages)
                {
                    const float u = t * gain * 2.0f;
                    y = type == 2 ? 0.9f * std::tanh (std::tanh (t * gain * 3.0f) * 3.0f)
                      : (type == 1 ? 0.85f * u / std::pow (1.0f + std::pow (std::abs (u), 4.0f), 0.25f) : 0.3f * t + 0.6f * std::tanh (t * gain * 1.5f));
                }
                else y = type == 2 ? std::sin (t * gain * 1.2f) * 0.9f : (type == 1 ? std::tanh (t * gain + 0.25f) - std::tanh (0.25f) * 0.8f : std::tanh (t * gain));
                p.push_back ({ cx + t * sz, cy - juce::jlimit (-1.0f, 1.0f, y) * sz });
            }
            strokePts (g, p, colour, 2.2f);
            break;
        }
        case VisualKind::Voices:
        {
            paint::well (g, r);
            const int voices = spec.variant;
            std::vector<int> notes;
            for (int n = 0; n < 128 && (int) notes.size() < voices; ++n)
                if ((dev.heldNotes[(size_t) (n >> 5)].load() >> (n & 31)) & 1u) notes.push_back (n);
            const float gap = 6.0f, vw = (r.getWidth() - 20 - gap * (voices - 1)) / voices;
            for (int i = 0; i < voices; ++i)
            {
                const float vx = r.getX() + 10 + i * (vw + gap);
                const R cell (vx, r.getY() + 10, vw, r.getHeight() - 20);
                g.setColour (col::control); g.fillRoundedRectangle (cell, 6);
                const bool on = i < (int) notes.size();
                if (on)
                {
                    const float lvl = 0.85f - 0.1f * i;
                    g.setColour (colour.withAlpha (0.35f)); g.fillRoundedRectangle (vx, cell.getY() + cell.getHeight() * (1 - lvl), vw, cell.getHeight() * lvl, 6);
                    g.setColour (colour); g.fillRoundedRectangle (vx, cell.getY() + cell.getHeight() * (1 - lvl), vw, 3, 1.5f);
                }
                if (voices == 3)   // Acid stack: the interval of each oscillator
                {
                    static const char* iv[3][3] = { { "-c", "0", "+c" }, { "0", "+7", "+12" }, { "-12", "0", "+12" } };
                    const int m = std::clamp ((int) dev.get ("stackMode"), 0, 2);
                    const bool stacked = dev.get ("stack") > 0.5f;
                    paint::text (g, iv[m][i], vx + vw / 2, cell.getY() + 16, 10, Weight::Bold, stacked || i == 1 ? col::text : col::faint, juce::Justification::horizontallyCentred);
                }
                else if (vw > 22) paint::text (g, on ? juce::MidiMessage::getMidiNoteName (notes[(size_t) i], true, true, 4) : "-", vx + vw / 2, cell.getY() + 16, 10, Weight::Bold, on ? col::text : col::faint, juce::Justification::horizontallyCentred);
            }
            break;
        }
        case VisualKind::Arp:
        {
            paint::well (g, r);
            const int mode = iv (0), range = std::max (1, iv (1));
            static const int up[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
            const float bw = (r.getWidth() - 20) / 8.0f;
            for (int i = 0; i < 8; ++i)
            {
                int p = up[i] % (3 * range);
                if (mode == 2) p = 3 * range - 1 - p;
                else if (mode == 3) p = i < 4 ? i : 7 - i;
                else if (mode == 4) p = (i * 5 + 3) % (3 * range);
                const float top = (float) (3 * range - 1);
                const float by = r.getBottom() - 18 - (mode == 0 ? 0.5f : p / std::max (1.0f, top)) * (r.getHeight() - 36);
                g.setColour (mode == 0 ? col::faint : colour);
                g.fillRoundedRectangle (r.getX() + 10 + i * bw + 2, by - 4, (bw - 4) * 0.6f, 8, 4);
            }
            break;
        }
        case VisualKind::Bits:
        {
            paint::well (g, r);
            const float levels = std::exp2 (juce::jlimit (2.0f, 8.0f, v (0) - 1.0f) * 0.75f), stepsN = juce::jlimit (6.0f, 60.0f, v (1) * 1.2f);
            std::vector<juce::Point<float>> smooth, stepped;
            for (int i = 0; i <= 200; ++i)
            {
                const float t = i / 200.0f;
                smooth.push_back ({ r.getX() + 10 + (r.getWidth() - 20) * t, r.getCentreY() - std::sin (dsp::kTwoPi * 1.5f * t) * r.getHeight() * 0.32f });
            }
            const bool vintage = spec.variant == 0 ? iv (2) == 1 : true;
            for (int k = 0; k < (int) stepsN; ++k)
            {
                const float t0 = k / stepsN, t1 = (k + 1) / stepsN;
                float vv = std::sin (dsp::kTwoPi * 1.5f * t0);
                if (vintage) vv = std::round (vv * levels / 2) / (levels / 2);
                const float yy = r.getCentreY() - vv * r.getHeight() * 0.32f;
                stepped.push_back ({ r.getX() + 10 + (r.getWidth() - 20) * t0, yy });
                stepped.push_back ({ r.getX() + 10 + (r.getWidth() - 20) * t1, yy });
            }
            strokePts (g, smooth, col::faint, 1.2f);
            if (vintage) strokePts (g, stepped, colour, 2.0f);
            break;
        }
        default: break;
    }
    // hover / press feedback on the controls inside the visual
    fb.begin();
    hotUse.clear();
    if (spec.kind == VisualKind::Sample || spec.kind == VisualKind::Slices)
    {
        const auto b = loadBar (r);
        hot (b.browse, 6); hot (b.load, 6);
        if (spec.kind == VisualKind::Sample)
        {
            // the markers: start, loop (the sampler), end
            const auto wr = waveRect();
            for (int m : { 0, 1, 2 })
            {
                if (spec.variant != 0 && m == 1) continue;
                const float t = markerPos (m);
                if (t < 0.0f || t > 1.0f) continue;
                hot (R (wr.getX() + wr.getWidth() * t - 6, wr.getY(), 12, wr.getHeight()), 4, 2, m);
            }
            if (spec.variant == 0)
            {
                // the zones (a click selects one)
                auto& sd = static_cast<SamplerDevice&> (dev);
                for (int z = 0; z < SamplerDevice::kZones; ++z)
                {
                    if (sd.zoneRef (z).isEmpty()) continue;
                    const float lo = sd.get (sd.zoneLo (z)), hi = sd.get (sd.zoneHi (z));
                    hot (R (wr.getX() + lo * wr.getWidth() / 128.0f, r.getBottom() - 30, std::max (8.0f, (hi - lo + 1) * wr.getWidth() / 128.0f), 26), 4);
                }
            }
        }
    }
    else if (spec.kind == VisualKind::Pads)
    {
        const R pr = r.reduced (16);
        const int cols = spec.variant == 2 ? 4 : 2, rows = 8 / cols;
        for (int i = 0; i < 8; ++i)
            hot (R (pr.getX() + (i % cols) * pr.getWidth() / cols, pr.getY() + (i / cols) * pr.getHeight() / rows, pr.getWidth() / cols, pr.getHeight() / rows).reduced (4), 8);
    }
    else if (spec.kind == VisualKind::Ops)
    {
        // each operator, and its wave and mute buttons
        const float cw = (r.getWidth() - 16) / 6.0f;
        for (int o = 0; o < 6; ++o)
        {
            const float ox = r.getX() + 8 + o * cw;
            hot (R (ox, r.getY() + 6, cw, r.getBottom() - 90 - r.getY()), 8);
            hot (R (ox + 4, r.getBottom() - 84, cw - 8, 20), 6);
            hot (R (ox + 4, r.getBottom() - 30, cw - 8, 26), 6);
        }
    }
    else if (spec.kind == VisualKind::Rate4)
    {
        // the envelope's breakpoints
        const float x0 = r.getX() + 10, W = r.getWidth() - 20;
        float widths[4], tot = 0.0f;
        for (int k = 0; k < 4; ++k) { widths[k] = 0.12f + (1.0f - v (k)) * 0.9f; tot += widths[k]; }
        float cx = x0;
        for (int k = 0; k < 4; ++k)
        {
            cx += W * widths[k] / tot;
            const float y = r.getBottom() - 12 - (r.getHeight() - 24) * v (4 + k);
            hot (R (cx - 9, y - 9, 18, 18), 9, 1, k);
        }
    }
    else if (spec.kind == VisualKind::Lane && pattern >= 0 && track >= 0)
    {
        const int spb = ctx.engine.project().stepsPerBar (pattern);
        const float gap = 4.0f, bw = (r.getWidth() - 16 - gap * (spb - 1)) / spb;
        for (int i = 0; i < spb; ++i) hot (R (r.getX() + 8 + i * (bw + gap), r.getY() + 8, bw, r.getHeight() - 16), 4, 3, i);
    }
    fb.paint (g);
}

// ------------------------------------------------------------------------------------------- interaction
void VisualView::mouseDown (const juce::MouseEvent& e)
{
    const R r = inner();
    dragMarker = dragPoint = dragStep = -1;
    if (spec.kind == VisualKind::Sample || spec.kind == VisualKind::Slices)
    {
        const auto b = loadBar (r);
        if (b.browse.contains (e.position)) { browseFactory(); return; }
        if (b.load.contains (e.position)) { loadFromDisk(); return; }
        if (e.position.y < r.getY() + 32) return;
    }
    switch (spec.kind)
    {
        case VisualKind::Pads:
        {
            const R pr = r.reduced (16);
            const int cols = spec.variant == 2 ? 4 : 2, rows = 8 / cols;
            const int c = juce::jlimit (0, cols - 1, (int) ((e.position.x - pr.getX()) / (pr.getWidth() / cols)));
            const int rr = juce::jlimit (0, rows - 1, (int) ((e.position.y - pr.getY()) / (pr.getHeight() / rows)));
            const int pad = rr * cols + c;
            ctx.select (&dev, pad);
            dragStep = pad;
            ctx.engine.injectNote (track < 0 ? 0 : track, dev.laneNote (pad), 0.85f, true, pattern);
            break;
        }
        case VisualKind::Ops:
        {
            auto& fmd = static_cast<FmDevice&> (dev);
            const float cw = (r.getWidth() - 16) / 6.0f;
            const int o = juce::jlimit (0, 5, (int) ((e.position.x - r.getX() - 8) / cw));
            if (e.position.y > r.getBottom() - 30) fmd.set (fmd.opParam (o, FmDevice::Mute), fmd.get (fmd.opParam (o, FmDevice::Mute)) > 0.5f ? 0.0f : 1.0f);
            else if (std::abs (e.position.y - (r.getBottom() - 74)) < 10) fmd.set (fmd.opParam (o, FmDevice::Wave), (float) (((int) fmd.get (fmd.opParam (o, FmDevice::Wave)) + 1) % 4));
            ctx.select (&dev, o);
            ctx.markDirty();
            break;
        }
        case VisualKind::Rate4:
        {
            // nearest breakpoint
            float best = 1e9f;
            const float x0 = r.getX() + 10, W = r.getWidth() - 20;
            float widths[4], tot = 0.0f;
            for (int k = 0; k < 4; ++k) { widths[k] = 0.12f + (1.0f - v (k)) * 0.9f; tot += widths[k]; }
            float cx = x0;
            for (int k = 0; k < 4; ++k)
            {
                cx += W * widths[k] / tot;
                const float y = r.getBottom() - 12 - (r.getHeight() - 24) * v (4 + k);
                const float d = e.position.getDistanceFrom ({ cx, y });
                if (d < best) { best = d; dragPoint = k; }
            }
            if (dragPoint >= 0) dragStartValue = v (dragPoint);
            break;
        }
        case VisualKind::Sample:
        {
            const bool sampler = spec.variant == 0;
            const float zh = sampler ? 30.0f : 0.0f;
            const R wr (r.getX() + 12, r.getY() + 36, r.getWidth() - 24, r.getHeight() - 46 - zh);
            if (sampler && e.position.y > r.getBottom() - zh - 4)
            {
                auto& sd = static_cast<SamplerDevice&> (dev);
                const int note = juce::jlimit (0, 127, (int) ((e.position.x - wr.getX()) / (wr.getWidth() / 128.0f)));
                for (int z = 0; z < SamplerDevice::kZones; ++z)
                    if (sd.zoneRef (z).isNotEmpty() && note >= sd.get (sd.zoneLo (z)) && note <= sd.get (sd.zoneHi (z))) { ctx.select (&dev, z); break; }
                break;
            }
            const float t = (e.position.x - wr.getX()) / wr.getWidth();
            double s, en, ls = -10;
            if (sampler) { const auto reg = static_cast<SamplerDevice&> (dev).region (0); s = reg.s; en = reg.e; ls = reg.ls; }
            else
            {
                auto& ds = static_cast<DrumSamplerDevice&> (dev);
                const int p = ctx.selection (&dev);
                s = ds.get (ds.padParam (p, DrumSamplerDevice::Start));
                en = s + ds.get (ds.padParam (p, DrumSamplerDevice::Length)) * (1 - s);
            }
            const double ds_ = std::abs (t - s), de = std::abs (t - en), dl = std::abs (t - ls);
            dragMarker = ds_ <= de && ds_ <= dl ? 0 : (dl < de ? 1 : 2);
            break;
        }
        case VisualKind::Lane:
        {
            if (pattern < 0 || track < 0) break;
            const int spb = ctx.engine.project().stepsPerBar (pattern);
            const float gap = 4.0f, bw = (r.getWidth() - 16 - gap * (spb - 1)) / spb;
            dragStep = juce::jlimit (0, spb - 1, (int) ((e.position.x - r.getX() - 8) / (bw + gap)));
            const Engine::ScopedEdit edit (ctx.engine);
            auto& d = ctx.engine.project().patterns[(size_t) pattern].slots[(size_t) track].track.lanes[(size_t) ctx.selection (&dev)][(size_t) (ctx.viewBar * spb + dragStep)];
            if (! d.on) { d.on = true; d.velocity = 100; d.probability = 100; d.ratchet = 1; }
            else if (e.mods.isRightButtonDown() || e.mods.isAltDown()) d.on = false;
            ctx.markDirty();
            mouseDrag (e);
            break;
        }
        default: break;
    }
    repaint();
}

void VisualView::mouseDrag (const juce::MouseEvent& e)
{
    const R r = inner();
    if (spec.kind == VisualKind::Rate4 && dragPoint >= 0)
    {
        auto setK = [&] (int k, float val) { if (k < (int) idx.size() && idx[(size_t) k] >= 0) dev.set (idx[(size_t) k], juce::jlimit (0.0f, 1.0f, val)); };
        setK (dragPoint, dragStartValue - (float) e.getDistanceFromDragStartX() / r.getWidth() * 1.5f);
        if (dragPoint < 3) setK (4 + dragPoint, (r.getBottom() - 12 - e.position.y) / (r.getHeight() - 24));
        ctx.markDirty();
        repaint();
    }
    else if (spec.kind == VisualKind::Sample && dragMarker >= 0)
    {
        const bool sampler = spec.variant == 0;
        const float zh = sampler ? 30.0f : 0.0f;
        const R wr (r.getX() + 12, r.getY() + 36, r.getWidth() - 24, r.getHeight() - 46 - zh);
        const float t = juce::jlimit (0.0f, 1.0f, (e.position.x - wr.getX()) / wr.getWidth());
        if (sampler)
        {
            auto& sd = static_cast<SamplerDevice&> (dev);
            const auto reg = sd.region (0);
            if (dragMarker == 0) { const double newS = std::min ((double) t, reg.e - 0.01); sd.set (SamplerDevice::start, (float) newS); sd.set (SamplerDevice::length, (float) ((reg.e - newS) / std::max (1.0e-6, 1.0 - newS))); }
            else if (dragMarker == 2) sd.set (SamplerDevice::length, (float) ((std::max ((double) t, reg.s + 0.01) - reg.s) / std::max (1.0e-6, 1.0 - reg.s)));
            else sd.set (SamplerDevice::loopStart, (float) ((t - reg.s) / std::max (1.0e-6, reg.e - reg.s)));
        }
        else
        {
            auto& ds = static_cast<DrumSamplerDevice&> (dev);
            const int p = ctx.selection (&dev);
            const float s = ds.get (ds.padParam (p, DrumSamplerDevice::Start));
            const float en = s + ds.get (ds.padParam (p, DrumSamplerDevice::Length)) * (1 - s);
            if (dragMarker == 0) { const float ns = std::min (t, en - 0.01f); ds.set (ds.padParam (p, DrumSamplerDevice::Start), ns); ds.set (ds.padParam (p, DrumSamplerDevice::Length), (en - ns) / std::max (1.0e-6f, 1 - ns)); }
            else ds.set (ds.padParam (p, DrumSamplerDevice::Length), (std::max (t, s + 0.01f) - s) / std::max (1.0e-6f, 1 - s));
        }
        ctx.markDirty();
        repaint();
    }
    else if (spec.kind == VisualKind::Lane && dragStep >= 0 && pattern >= 0 && track >= 0)
    {
        const int spb = ctx.engine.project().stepsPerBar (pattern);
        const float gap = 4.0f, bw = (r.getWidth() - 16 - gap * (spb - 1)) / spb;
        const int step = juce::jlimit (0, spb - 1, (int) ((e.position.x - r.getX() - 8) / (bw + gap)));
        const float val = juce::jlimit (0.0f, 1.0f, (r.getBottom() - 8 - e.position.y) / (r.getHeight() - 16));
        const Engine::ScopedEdit edit (ctx.engine);
        auto& d = ctx.engine.project().patterns[(size_t) pattern].slots[(size_t) track].track.lanes[(size_t) ctx.selection (&dev)][(size_t) (ctx.viewBar * spb + step)];
        if (! d.on) return;
        if (ctx.laneView == 0) d.velocity = (uint8_t) juce::jlimit (1, 127, juce::roundToInt (val * 127));
        else if (ctx.laneView == 1) d.probability = (uint8_t) juce::jlimit (0, 100, juce::roundToInt (val * 100));
        else d.ratchet = (uint8_t) juce::jlimit (1, 4, 1 + (int) (val * 3.99f));
        ctx.markDirty();
        repaint();
    }
}

void VisualView::mouseUp (const juce::MouseEvent&)
{
    if (spec.kind == VisualKind::Pads && dragStep >= 0)
        ctx.engine.injectNote (track < 0 ? 0 : track, dev.laneNote (dragStep), 0.0f, false, pattern);
    dragMarker = dragPoint = dragStep = -1;
}

bool VisualView::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (spec.kind != VisualKind::Sample && spec.kind != VisualKind::Slices && spec.kind != VisualKind::Pads) return false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac")) return true;
    return false;
}

void VisualView::loadDroppedSamples (const juce::StringArray& files)
{
    juce::Array<juce::File> list;
    for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac")) list.add (juce::File (f));
    if (list.isEmpty()) return;
    if (auto* sd = dynamic_cast<SamplerDevice*> (&dev))
    {
        if (list.size() > 1) sd->loadZonesFromFiles (list);
        else sd->loadZone (ctx.selection (&dev), refForFile (list[0]));
    }
    else if (auto* ds = dynamic_cast<DrumSamplerDevice*> (&dev))
        ds->loadPad (ctx.selection (&dev), refForFile (list[0]));
    ctx.markDirty();
}

void VisualView::sliceDropped (const juce::File& f)
{
    auto* ds = dynamic_cast<DrumSamplerDevice*> (&dev);
    if (ds == nullptr) return;
    // the loop decodes on the loader thread; the lanes are filled when the slices are ready
    juce::Component::SafePointer<VisualView> safe (this);
    ds->sliceLoop (refForFile (f), false, [safe, ds] (std::vector<double> starts)
    {
        if (safe == nullptr || starts.empty() || safe->pattern < 0 || safe->track < 0) return;
        auto& ctx = safe->ctx;
        const int pattern = safe->pattern, track = safe->track;
        // fill the lanes so the loop plays back at the project tempo
        auto data = ds->pad[0].get();
        if (! data) return;
        const double seconds = data->length() / data->sampleRate;
        auto& proj = ctx.engine.project();
        const double stepSecs = proj.stepQuarters (pattern) * 60.0 / proj.bpm;
        const int loopSteps = std::max (1, (int) std::round (seconds / stepSecs));
        const Engine::ScopedEdit edit (ctx.engine);
        auto& tr = proj.patterns[(size_t) pattern].slots[(size_t) track].track;
        for (auto& lane : tr.lanes) for (auto& d : lane) d.clear();
        const int total = proj.patternSteps (pattern);
        for (int k = 0; k < (int) starts.size() && k < 8; ++k)
            for (int rep = 0; rep * loopSteps < total; ++rep)
            {
                const int s = rep * loopSteps + (int) std::round (starts[(size_t) k] * loopSteps);
                if (s < total) { auto& d = tr.lanes[(size_t) k][(size_t) s]; d.on = true; d.velocity = 110; }
            }
        ctx.markDirty();
    });
}

void VisualView::browseFactory()
{
    // the factory samples: sampler sources by family; drum kits by name (one sound per part) and loops
    const auto dir = factoryDir().getChildFile ("samples");
    const auto manifest = juce::JSON::parse (dir.getChildFile ("manifest.json"));
    std::vector<juce::String> refs;
    auto item = [&refs] (juce::PopupMenu& menu, const juce::String& label, const juce::String& file)
    {
        refs.push_back ("factory:" + file);
        menu.addItem ((int) refs.size(), label);
    };
    const bool slices = spec.kind == VisualKind::Slices;
    juce::PopupMenu m, loops;
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*_loop_*.wav"))
    {
        const auto n = f.getFileNameWithoutExtension();
        item (loops, n.upToFirstOccurrenceOf ("_", false, false).substring (0, 1).toUpperCase()
                         + n.upToFirstOccurrenceOf ("_", false, false).substring (1) + " loop, "
                         + n.fromLastOccurrenceOf ("_", false, false) + " BPM", n);
    }
    if (dynamic_cast<SamplerDevice*> (&dev) != nullptr)
    {
        std::map<juce::String, juce::PopupMenu> fam;
        juce::StringArray order;
        if (auto* list = manifest["sampler"].getArray())
            for (auto& v : *list)
            {
                const auto f = v["family"].toString();
                order.addIfNotAlreadyThere (f);
                item (fam[f], v["name"].toString(), v["file"].toString());
            }
        for (auto& f : order) m.addSubMenu (f, fam[f]);
    }
    else if (slices) m = loops;
    else
    {
        static const char* parts[8] = { "Kick", "Snare", "Closed Hat", "Open Hat", "Clap", "Tom", "Cymbal", "Perc" };
        static const char* suffix[8] = { "kick", "snare", "chat", "ohat", "clap", "tom", "cym", "perc" };
        if (auto* kits = manifest["kits"].getArray())
            for (int k = 0; k < kits->size(); ++k)
            {
                juce::PopupMenu km;
                const auto prefix = "kit" + juce::String (k + 1).paddedLeft ('0', 2) + "_";
                for (int p = 0; p < 8; ++p) item (km, parts[p], prefix + suffix[p]);
                m.addSubMenu ((*kits)[k]["name"].toString(), km);
            }
        m.addSeparator();
        m.addSubMenu ("Loops", loops);
    }
    m.showMenuAsync (menuAt (*this, loadBar (inner()).browse), [safe = juce::Component::SafePointer<VisualView> (this), refs, slices] (int id)
    {
        if (safe == nullptr || id <= 0 || id > (int) refs.size()) return;
        const auto ref = refs[(size_t) id - 1];
        if (slices) safe->sliceDropped (resolveSampleRef (ref));
        else if (auto* sd = dynamic_cast<SamplerDevice*> (&safe->dev)) sd->loadZone (safe->ctx.selection (&safe->dev), ref);
        else if (auto* ds = dynamic_cast<DrumSamplerDevice*> (&safe->dev)) ds->loadPad (safe->ctx.selection (&safe->dev), ref);
        safe->ctx.markDirty();
        safe->repaint();
    });
}

void VisualView::loadFromDisk()
{
    const bool slices = spec.kind == VisualKind::Slices;
    chooser = std::make_unique<juce::FileChooser> (slices ? "Slice a loop" : "Load sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    const bool multi = dynamic_cast<SamplerDevice*> (&dev) != nullptr;   // several files become key zones
    const int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                      | (multi ? juce::FileBrowserComponent::canSelectMultipleItems : 0);
    chooser->launchAsync (flags, [safe = juce::Component::SafePointer<VisualView> (this), slices] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResults().isEmpty()) return;
        if (slices) safe->sliceDropped (fc.getResults()[0]);
        else
        {
            juce::StringArray paths;
            for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
            safe->loadDroppedSamples (paths);
        }
        safe->repaint();
    });
}

void VisualView::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    if (spec.kind == VisualKind::Slices && ! files.isEmpty()) sliceDropped (juce::File (files[0]));
    else loadDroppedSamples (files);
    repaint();
}

// ------------------------------------------------------------------------------------------- rack visuals
void VisualView::drawRackVisual (juce::Graphics& g, R r, DeviceProcessor& d, juce::Colour c)
{
    switch (d.type())
    {
        case DeviceType::Drums:
        case DeviceType::DrumSampler:
        {
            paint::well (g, r);
            juce::StringArray names;
            std::vector<bool> lit;
            for (int i = 0; i < 8; ++i)
            {
                names.add (d.laneName (i));
                lit.push_back (((d.heldNotes[(size_t) (d.laneNote (i) >> 5)].load() >> (d.laneNote (i) & 31)) & 1u) != 0);
            }
            draw::pads (g, r.reduced (8), c, names, -1, lit, 4);
            break;
        }
        case DeviceType::Acid: draw::filter (g, r, c, 0, d.get ("cutoff"), d.get ("reso"), 4, false); break;
        case DeviceType::Mono: draw::filter (g, r, c, 0, d.get ("cutoff"), d.get ("reso"), 4, false); break;
        case DeviceType::Poly:
            if (d.get ("chorusMode") > 0.5f) draw::chorus (g, r, c, (int) d.get ("chorusMode"));
            else draw::filter (g, r, c, 0, d.get ("cutoff"), d.get ("reso"), d.get ("engine") > 0.5f ? 2 : 4, false);
            break;
        case DeviceType::FM: draw::algo (g, r, c, (int) d.get ("algorithm"), -1); break;
        case DeviceType::Sampler:
        {
            paint::well (g, r);
            if (auto s = static_cast<SamplerDevice&> (d).zone[0].get()) draw::waveform (g, r.reduced (12, 10), c, s->peaks);
            break;
        }
        default: paint::well (g, r); break;
    }
}
} // namespace mb::ui
