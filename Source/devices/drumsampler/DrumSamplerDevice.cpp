#include "DrumSamplerDevice.h"
#include <juce_events/juce_events.h>

namespace mb
{
using namespace dsp;

namespace
{
const int kPadNotes[8] = { 36, 38, 42, 46, 39, 45, 49, 37 };
}

DrumSamplerDevice::DrumSamplerDevice() : DeviceProcessor (DeviceType::DrumSampler)
{
    auto& L = layout;
    for (int p = 0; p < kPads; ++p)
    {
        const juce::String id = "pd" + juce::String (p + 1);
        L.cont (id + "Start", "Start", 0, 0.95f, 0, Unit::Percent);
        L.cont (id + "Length", "Length", 0.01f, 1, 1, Unit::Percent);
        L.cont (id + "Pitch", "Pitch", -24, 24, 0, Unit::Semis, false, true);
        L.cont (id + "Attack", "Attack", 0, 0.5f, 0, Unit::Seconds);
        L.cont (id + "Decay", "Decay", 0.01f, 4, 1.5f, Unit::Seconds, true);
        L.cont (id + "PegInt", "PEG Int", -1, 1, 0, Unit::Plain, false, true);
        L.cont (id + "PegDecay", "PEG Decay", 0.005f, 1, 0.08f, Unit::Seconds, true);
        L.cont (id + "HiCut", "Hi-Cut", 200, 20000, 20000, Unit::Hz, true);
        L.cont (id + "Level", "Level", 0, 1, 0.8f, Unit::Percent);
        L.cont (id + "Pan", "Pan", -1, 1, 0, Unit::Pan, false, true);
        L.cont (id + "SendDelay", "Delay", 0, 1, 0, Unit::Percent);
        L.cont (id + "SendReverb", "Reverb", 0, 1, 0, Unit::Percent);
        L.integer (id + "Bits", "Bits", 4, 16, 16, Unit::Bits);
        L.cont (id + "Drive", "Drive", 0, 1, 0, Unit::Percent);
        L.choice (id + "Mode", "Play", { "One-shot", "Gate" }, 0);
        L.choice (id + "Reverse", "Reverse", { "Off", "On" }, 0);
        L.integer (id + "Choke", "Choke", 0, 2, (p == 2 || p == 3) ? 1 : 0);
    }
    pSliceMode = L.choice ("sliceMode", "Slice", { "Transient", "Equal" }, 0);
    pSliceSens = L.cont ("sliceSens", "Sensitivity", 0, 1, 0.5f, Unit::Percent);
    pKitLevel = L.cont ("kitLevel", "Level", 0, 2, 0.8f, Unit::Percent);
    pRevMix = L.cont ("kitReverb", "Mix", 0, 1, 0.6f, Unit::Percent);   // the reverb card's return; pads set their sends
    pRevSize = L.size();
    addReverbParams (L);
    finishLayout();

    auto all = [] (const char* field, float lo, float hi)
    {
        std::vector<std::tuple<std::string, float, float>> v;
        for (int p = 1; p <= 8; ++p) v.push_back ({ "pd" + std::to_string (p) + field, lo, hi });
        return v;
    };
    macro (0, "Kick Pitch", { { "pd1Pitch", 0.25f, 0.75f } });
    macro (1, "Snare Pitch", { { "pd2Pitch", 0.25f, 0.75f } });
    macro (2, "Hat Decay", { { "pd3Decay", 0.05f, 0.7f }, { "pd4Decay", 0.2f, 0.9f } });
    auto setAll = [this] (int m, const juce::String& name, const std::vector<std::tuple<std::string, float, float>>& t)
    {
        auto& mm = macroMaps[(size_t) m];
        mm.name = name; mm.count = 0;
        for (auto& [id, lo, hi] : t) mm.targets[(size_t) mm.count++] = { layout.indexOf (juce::String (id)), lo, hi };
    };
    setAll (3, "Start", all ("Start", 0.0f, 0.5f));
    setAll (4, "Hi-Cut", all ("HiCut", 0.15f, 1.0f));
    setAll (5, "Bits", all ("Bits", 1.0f, 0.0f));
    setAll (6, "Drive", all ("Drive", 0.0f, 1.0f));
    setAll (7, "Reverb", all ("SendReverb", 0.0f, 1.0f));
    defaultMacroMaps = macroMaps;
}

juce::String DrumSamplerDevice::laneName (int lane) const
{
    const auto s = pad[std::clamp (lane, 0, 7)].get();
    // still loading (a sample loads in the background): named from its file meanwhile
    const auto& ref = refs[(size_t) std::clamp (lane, 0, 7)];
    if (! s && ref.isEmpty()) return "Pad " + juce::String (lane + 1);
    const auto raw = s ? s->name : ref.fromLastOccurrenceOf ("/", false, false).fromFirstOccurrenceOf ("factory:", false, false).upToLastOccurrenceOf (".", false, false);
    auto n = raw.containsChar ('_') ? raw.fromLastOccurrenceOf ("_", false, false) : raw;
    if (refs[(size_t) lane] == refs[(size_t) ((lane + 1) % kPads)] && refs[(size_t) lane] == refs[(size_t) ((lane + 2) % kPads)])
        n = "Slice " + juce::String (lane + 1);   // a sliced loop
    static const juce::StringPairArray names = []
    {
        juce::StringPairArray a;
        a.set ("ohat", "O Hat"); a.set ("hat", "C Hat"); a.set ("chat", "C Hat"); a.set ("cym", "Cymbal");
        return a;
    }();
    if (names.containsKey (n)) n = names[n];
    return (n.substring (0, 1).toUpperCase() + n.substring (1)).substring (0, 10);
}
int DrumSamplerDevice::laneNote (int lane) const { return kPadNotes[std::clamp (lane, 0, 7)]; }

int DrumSamplerDevice::padForNote (int note) const
{
    for (int p = 0; p < kPads; ++p) if (kPadNotes[p] == note) return p;
    static const int padForRole[8] = { 0, 1, 4, 5, 2, 3, 6, 7 };   // kick snare clap tom chat ohat cym perc → pads
    if (const int r = gmDrumRole (note); r >= 0) return padForRole[r];
    const int pc = ((note % 12) + 12) % 12;
    return pc < 8 ? pc : -1;
}

void DrumSamplerDevice::loadPad (int p, const juce::String& ref, bool sync)
{
    if (p < 0 || p >= kPads) return;
    refs[(size_t) p] = ref;
    if (ref.isEmpty()) { pad[p].set ({}); return; }
    if (sync || forceSyncSampleLoads().load() || ! canLoadAsync()) { pad[p].set (loadSample (ref)); return; }
    std::weak_ptr<int> alive = lifetime;
    loadSampleAsync (ref, [this, alive, p, ref] (SamplePtr s) { if (! alive.expired() && refs[(size_t) p] == ref) pad[p].set (s); });
}

std::vector<double> DrumSamplerDevice::findSlices (const SampleData& d, bool transients, float sens, int count)
{
    std::vector<double> starts;
    const int n = d.length();
    if (n == 0) return starts;
    if (! transients)
    {
        for (int i = 0; i < count; ++i) starts.push_back ((double) i / count);
        return starts;
    }
    // spectral-flux-like onset curve from a 5 ms energy envelope
    const int hop = std::max (1, (int) (d.sampleRate * 0.005));
    std::vector<float> energy;
    for (int i = 0; i < n; i += hop)
    {
        float e = 0.0f;
        for (int k = i; k < std::min (n, i + hop); ++k)
            for (int ch = 0; ch < d.buffer.getNumChannels(); ++ch) e += d.buffer.getSample (ch, k) * d.buffer.getSample (ch, k);
        energy.push_back (std::sqrt (e / hop));
    }
    std::vector<std::pair<float, int>> onsets;
    const float thresh = 0.02f + (1.0f - sens) * 0.2f;
    const float peak = energy.empty() ? 0.0f : *std::max_element (energy.begin(), energy.end());   // once, not per frame
    for (size_t i = 1; i + 1 < energy.size(); ++i)
    {
        const float flux = energy[i] - energy[i - 1];
        if (flux > thresh * peak && flux >= energy[i + 1] - energy[i])
            onsets.push_back ({ flux, (int) i });
    }
    std::sort (onsets.begin(), onsets.end(), [] (auto& a, auto& b) { return a.first > b.first; });
    std::vector<int> picks { 0 };
    const int minGap = (int) (0.06 * d.sampleRate / hop);
    for (auto& [f, idx] : onsets)
    {
        if ((int) picks.size() >= count) break;
        bool ok = true;
        for (int p : picks) if (std::abs (p - idx) < minGap) ok = false;
        if (ok) picks.push_back (idx);
    }
    std::sort (picks.begin(), picks.end());
    for (int p : picks) starts.push_back (std::min (1.0, (double) p * hop / n));
    // fewer onsets than pads: split the widest gaps (an equal-parts fill could repeat a start: a zero-length slice)
    while ((int) starts.size() < count)
    {
        double widest = 0.0, at = 0.0;
        for (size_t i = 0; i < starts.size(); ++i)
        {
            const double next = i + 1 < starts.size() ? starts[i + 1] : 1.0;
            if (next - starts[i] > widest) { widest = next - starts[i]; at = starts[i]; }
        }
        starts.push_back (at + widest * 0.5);
        std::sort (starts.begin(), starts.end());
    }
    return starts;
}

std::vector<double> DrumSamplerDevice::sliceLoop (const juce::String& ref, bool sync, std::function<void (std::vector<double>)> done)
{
    if (sync || forceSyncSampleLoads().load() || ! canLoadAsync())
    {
        auto starts = applySlices (ref, loadSample (ref));
        if (done) done (starts);
        return starts;
    }
    // a long loop decodes on the loader thread (the UI keeps running); the pads change when it is ready
    std::weak_ptr<int> alive = lifetime;
    loadSampleAsync (ref, [this, alive, ref, done] (SamplePtr data)
    {
        if (alive.expired()) return;
        auto starts = applySlices (ref, data);
        if (done) done (starts);
    });
    return {};
}

std::vector<double> DrumSamplerDevice::applySlices (const juce::String& ref, SamplePtr data)
{
    if (! data) return {};
    auto starts = findSlices (*data, get (pSliceMode) < 0.5f, get (pSliceSens), kPads);
    for (int p = 0; p < kPads; ++p)
    {
        const double s = starts[(size_t) p], e = p + 1 < kPads ? starts[(size_t) p + 1] : 1.0;
        refs[(size_t) p] = ref;
        pad[p].set (data);
        set (padParam (p, Start), (float) s);
        set (padParam (p, Length), (float) std::clamp ((e - s) / std::max (1.0e-6, 1.0 - s), 0.01, 1.0));
        set (padParam (p, Mode), 0.0f);
        set (padParam (p, Decay), 4.0f);
        set (padParam (p, Choke), 1.0f);   // slices of one loop cut each other off
    }
    return starts;
}

void DrumSamplerDevice::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    for (auto& v : voices) { v.drive.prepare ((float) sr); v.drive.setTone (0.8f); }
    bufL.assign ((size_t) maxBlock, 0.0f); bufR.assign ((size_t) maxBlock, 0.0f);
    sendL.assign ((size_t) maxBlock, 0.0f); sendR.assign ((size_t) maxBlock, 0.0f);
    levelSm.setTime (15.0f, (float) sr); levelSm.reset (get (pKitLevel));
    verb.prepare ((float) sr);
}

void DrumSamplerDevice::reset() { for (auto& v : voices) v.active = false; for (auto& v : tails) v.active = false; verb.reset(); }

bool DrumSamplerDevice::anyVoiceActive() const
{
    for (auto& v : voices) if (v.active) return true;
    for (auto& v : tails) if (v.active) return true;
    return false;
}

void DrumSamplerDevice::trigger (int p, float vel)
{
    const auto* data = pad[p].audio();
    if (data == nullptr || data->length() < 4) return;
    const int group = (int) pv (padParam (p, Choke));   // a step lock (Start, Length, Reverse, Choke) counts from the trigger on
    if (group > 0)
        for (int q = 0; q < kPads; ++q)
            if (q != p && voices[(size_t) q].active && (int) pv (padParam (q, Choke)) == group) voices[(size_t) q].choking = true;
    auto& v = voices[(size_t) p];
    // a retrigger: the sounding playback becomes the pad's tail and fades out over 4 ms (cutting it would click)
    if (v.active) { tails[(size_t) p] = v; tails[(size_t) p].fadeStep = 1.0f / (0.004f * (float) sampleRate); }
    v.sample = data;
    const double s = pv (padParam (p, Start)) * (data->length() - 1);
    const double e = s + pv (padParam (p, Length)) * (data->length() - 1 - s);
    v.forward = pv (padParam (p, Reverse)) < 0.5f;
    v.pos = v.forward ? s : e;
    v.end = v.forward ? e : s;
    v.t = 0; v.vel = vel; v.amp = 1.0f; v.rel = 1.0f; v.choke = 1.0f; v.choking = false; v.releasing = false;
    v.fadeOut = 1.0f; v.fadeStep = 0.0f;
    v.active = true; v.held = true;
    hits[(size_t) p].fetch_add (1);
}

void DrumSamplerDevice::handleEvent (const NoteEvent& e)
{
    if (e.type == NoteEvent::NoteOn) { const int p = padForNote (e.note); if (p >= 0) trigger (p, e.velocity); markNote (e.note, true); }
    else if (e.type == NoteEvent::NoteOff)
    {
        markNote (e.note, false);
        const int p = padForNote (e.note);
        if (p >= 0) { voices[(size_t) p].held = false; if (get (padParam (p, Mode)) > 0.5f) voices[(size_t) p].releasing = true; }
    }
    else if (e.type == NoteEvent::Panic) reset();
    else if (e.type == NoteEvent::AllNotesOff)
    {
        for (auto& w : heldNotes) w.store (0);
        // Stop: Gate pads let go (held, they would never decay)
        for (int p = 0; p < kPads; ++p)
            if (voices[(size_t) p].held) { voices[(size_t) p].held = false; if (get (padParam (p, Mode)) > 0.5f) voices[(size_t) p].releasing = true; }
    }
}

void DrumSamplerDevice::renderVoice (Voice& v, int p, int n)
{
    if (! v.active) return;
    const float sr = (float) sampleRate;
    {
        const auto* data = pad[p].audio();
        if (data != v.sample || data == nullptr) { v.active = false; return; }
        const float pitch = pv (padParam (p, Pitch)), atk = pv (padParam (p, Attack)), dec = pv (padParam (p, Decay));
        const float pegI = pv (padParam (p, PegInt)) * 24.0f, pegD = pv (padParam (p, PegDecay));
        const float hiCut = pv (padParam (p, HiCut)), lvl = pv (padParam (p, Level)), pan = pv (padParam (p, Pan));
        const float sD = pv (padParam (p, SendDelay)), sR = pv (padParam (p, SendReverb));
        const float bits = pv (padParam (p, Bits)), drv = pv (padParam (p, Drive));
        v.lp1L.setCutoff (hiCut, sr); v.lp2L.setCutoff (hiCut, sr); v.lp1R.setCutoff (hiCut, sr); v.lp2R.setCutoff (hiCut, sr);
        const float decCoef = std::exp (-1.0f / (dec * sr));
        const float relCoef = std::exp (-1.0f / (0.03f * sr));
        const float chokeCoef = std::exp (-1.0f / (0.0042f * sr));   // a choked pad fades in ~4 ms at any rate
        const float endFade = 0.002f * sr;                            // the last 2 ms before the end (or a Length cut) fade
        const float gl = std::sqrt (0.5f * (1.0f - pan)), gr = std::sqrt (0.5f * (1.0f + pan));
        const int len = data->length();
        const float* cl = data->buffer.getReadPointer (0);
        const float* cr = data->buffer.getReadPointer (std::min (1, data->buffer.getNumChannels() - 1));
        const double srRatio = data->sampleRate / sampleRate;
        const bool gateMode = pv (padParam (p, Mode)) > 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) v.t / sr;
            const float semis = pitch + pegI * std::exp (-t / pegD);
            const double inc = semisToRatio (semis) * srRatio;
            float l = Resampler::hermite (cl, len, v.pos), r = Resampler::hermite (cr, len, v.pos);
            if (bits < 15.5f) { l = v.crush.vintage (l, sr, bits, sr); r = v.crushR.vintage (r, sr, bits, sr); }
            if (hiCut < 19900.0f) { l = v.lp2L.lp (v.lp1L.lp (l)); r = v.lp2R.lp (v.lp1R.lp (r)); }
            // the attack rises from silence (0.3 ms at least: a start inside the waveform would click)
            const float attackGain = std::min (1.0f, t / std::max (0.0003f, atk));
            if (! gateMode || v.held) v.amp *= gateMode ? 1.0f : decCoef;
            if (v.releasing) v.rel *= relCoef;
            if (v.choking) v.choke *= chokeCoef;
            if (v.fadeStep > 0.0f) v.fadeOut = std::max (0.0f, v.fadeOut - v.fadeStep);
            const float remain = (float) (std::abs (v.end - v.pos) / std::max (1.0e-6, inc));
            const float endGain = std::min (1.0f, remain / endFade);
            float g = attackGain * v.amp * v.rel * v.choke * v.vel * lvl * v.fadeOut * endGain;
            if (drv > 0.001f) v.drive.process (l, r, 0, drv, 1.0f);
            l *= g; r *= g;
            bufL[(size_t) i] += l * gl * 1.41f; bufR[(size_t) i] += r * gr * 1.41f;
            sendTo (0, i, l * sD, r * sD);
            sendL[(size_t) i] += l * gl * 1.41f * sR; sendR[(size_t) i] += r * gr * 1.41f * sR;
            ++v.t;
            v.pos += v.forward ? inc : -inc;
            if ((v.forward && v.pos >= v.end) || (! v.forward && v.pos <= v.end) || v.amp * v.rel * v.choke * v.fadeOut < 1.0e-4f)
            { v.active = false; break; }
        }
    }
}

void DrumSamplerDevice::render (float* outL, float* outR, int n)
{
    std::fill (bufL.begin(), bufL.begin() + n, 0.0f);
    std::fill (bufR.begin(), bufR.begin() + n, 0.0f);
    std::fill (sendL.begin(), sendL.begin() + n, 0.0f);
    std::fill (sendR.begin(), sendR.begin() + n, 0.0f);
    for (int p = 0; p < kPads; ++p)
    {
        renderVoice (tails[(size_t) p], p, n);
        renderVoice (voices[(size_t) p], p, n);
    }
    const float kl = pv (pKitLevel);
    for (int i = 0; i < n; ++i)
    {
        const float g = levelSm.tick (kl);
        bufL[(size_t) i] *= g; bufR[(size_t) i] *= g;
        sendL[(size_t) i] *= g; sendR[(size_t) i] *= g;
    }
    reverbCard (verb, sendL.data(), sendR.data(), bufL.data(), bufR.data(), n, pv (pRevMix) * 1.4f * reverbScale, pv (pRevSize), pv (pRevSize + 1), pv (pRevSize + 2));
    for (int i = 0; i < n; ++i) { outL[i] += bufL[(size_t) i]; outR[i] += bufR[(size_t) i]; }
    trackSilence (bufL.data(), bufR.data(), n);
}

void DrumSamplerDevice::save (juce::ValueTree& tree) const
{
    DeviceProcessor::save (tree);
    juce::ValueTree z ("Pads");
    for (int i = 0; i < kPads; ++i) z.setProperty ("pad" + juce::String (i + 1), refs[(size_t) i], nullptr);
    tree.appendChild (z, nullptr);
}

void DrumSamplerDevice::load (const juce::ValueTree& tree)
{
    DeviceProcessor::load (tree);
    auto z = tree.getChildWithName ("Pads");
    // pads sharing one ref (a sliced loop) decode it once
    for (int i = 0; i < kPads; ++i)
    {
        refs[(size_t) i] = z.getProperty ("pad" + juce::String (i + 1)).toString();
        if (refs[(size_t) i].isEmpty()) pad[i].set ({});
    }
    // each file decodes once, for every pad that plays it (a sliced loop: one decode for its 8 pads)
    juce::StringArray files;
    for (auto& r : refs) if (r.isNotEmpty()) files.addIfNotAlreadyThere (r);
    for (auto& ref : files)
    {
        auto apply = [this, ref] (SamplePtr data) { for (int p = 0; p < kPads; ++p) if (refs[(size_t) p] == ref) pad[p].set (data); };
        if (forceSyncSampleLoads().load() || ! canLoadAsync()) { apply (loadSample (ref)); continue; }
        std::weak_ptr<int> alive = lifetime;
        loadSampleAsync (ref, [alive, apply] (SamplePtr data) { if (! alive.expired()) apply (data); });
    }
}
} // namespace mb
