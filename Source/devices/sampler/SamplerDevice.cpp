#include "SamplerDevice.h"
#include <juce_events/juce_events.h>

namespace mb
{
using namespace dsp;

SamplerDevice::SamplerDevice() : DeviceProcessor (DeviceType::Sampler)
{
    auto& L = layout;
    L.choice ("playMode", "Play", { "One-shot", "Gate", "Loop" }, 1);
    L.cont ("start", "Start", 0, 0.95f, 0, Unit::Percent);
    L.cont ("length", "Length", 0.02f, 1, 1, Unit::Percent);
    L.cont ("loopStart", "Loop Start", 0, 0.98f, 0.3f, Unit::Percent);
    L.cont ("loopLen", "Loop Len", 0.02f, 1, 1, Unit::Percent);
    L.choice ("reverse", "Reverse", { "Off", "On" }, 0);
    L.cont ("tune", "Tune", -24, 24, 0, Unit::Semis, false, true);
    L.cont ("fine", "Fine", -50, 50, 0, Unit::Cents, false, true);
    L.cont ("pegInt", "PEG Int", -1, 1, 0, Unit::Plain, false, true);
    L.cont ("pegAttack", "PEG Attack", 0, 2, 0, Unit::Seconds);
    L.cont ("pegDecay", "PEG Decay", 0.01f, 4, 0.3f, Unit::Seconds, true);
    L.choice ("character", "Character", { "Clean", "Vintage" }, 0);
    L.integer ("bits", "Bits", 4, 16, 12, Unit::Bits);
    L.cont ("rate", "Rate", 4, 48, 26, Unit::KHz, true);
    L.cont ("drive", "Drive", 0, 1, 0, Unit::Percent);
    L.choice ("filterType", "Filter", { "LP", "BP", "HP" }, 0);
    L.cont ("cutoff", "Cutoff", 30, 18000, 18000, Unit::Hz, true);
    L.cont ("reso", "Reso", 0, 1, 0, Unit::Percent);
    L.cont ("envAmt", "Env", -1, 1, 0, Unit::Plain, false, true);
    L.cont ("key", "Key", 0, 1, 0, Unit::Percent);
    L.cont ("lfoCut", "LFO", 0, 1, 0, Unit::Percent);
    L.cont ("attack", "Attack", 0.001f, 8, 0.002f, Unit::Seconds, true);
    L.cont ("decay", "Decay", 0.005f, 10, 1, Unit::Seconds, true);
    L.cont ("sustain", "Sustain", 0, 1, 1, Unit::Percent);
    L.cont ("release", "Release", 0.005f, 10, 0.15f, Unit::Seconds, true);
    L.cont ("velocity", "Velocity", 0, 1, 0.4f, Unit::Percent);
    L.choice ("lfoShape", "LFO Shape", { "Tri", "Saw", "Sq", "S&H" }, 0);
    L.cont ("lfoRate", "Rate", 0.05f, 30, 4, Unit::Hz, true);
    L.cont ("lfoPitch", "Pitch", 0, 1, 0, Unit::Percent);
    L.integer ("voices", "Voices", 1, 8, 8);
    L.cont ("glide", "Glide", 0, 1, 0, Unit::Seconds);
    L.choice ("voiceMode", "Voice", { "Poly", "Mono" }, 0);
    L.cont ("reverb", "Reverb", 0, 1, 0, Unit::Percent);
    L.cont ("level", "Level", 0, 2, 0.7f, Unit::Percent);
    addReverbParams (L);
    jassert (layout.size() == zoneParams);
    for (int z = 0; z < kZones; ++z)
    {
        const juce::String p = "z" + juce::String (z + 1);
        L.integer (p + "Root", "Root", 0, 127, 60);
        L.integer (p + "Lo", "Lo", 0, 127, z == 0 ? 0 : 127);
        L.integer (p + "Hi", "Hi", 0, 127, 127);
    }
    finishLayout();

    macro (0, "Start", { { "start", 0.0f, 0.6f } });
    macro (1, "Length", { { "length", 0.05f, 1.0f } });
    macro (2, "Tune", { { "tune", 0.25f, 0.75f } });
    macro (3, "Cutoff", { { "cutoff", 0.2f, 1.0f } });
    macro (4, "Pitch EG", { { "pegInt", 0.5f, 1.0f } });
    macro (5, "Decay", { { "decay", 0.1f, 0.9f }, { "release", 0.1f, 0.8f } });
    macro (6, "Bits", { { "bits", 1.0f, 0.0f } });
    macro (7, "Reverb", { { "reverb", 0.0f, 1.0f } });
}

int SamplerDevice::numZones() const
{
    int n = 0;
    for (int z = 0; z < kZones; ++z) if (refs[(size_t) z].isNotEmpty()) n = z + 1;
    return n;
}

juce::String SamplerDevice::zoneRef (int z) const { return refs[(size_t) z]; }

void SamplerDevice::loadZone (int z, const juce::String& ref, bool sync)
{
    if (z < 0 || z >= kZones) return;
    refs[(size_t) z] = ref;
    if (ref.isEmpty()) { zone[z].set ({}); return; }
    if (sync || forceSyncSampleLoads().load() || juce::MessageManager::getInstanceWithoutCreating() == nullptr) { zone[z].set (loadSample (ref)); return; }
    std::weak_ptr<int> alive = lifetime;
    loadSampleAsync (ref, [this, alive, z, ref] (SamplePtr p) { if (! alive.expired() && refs[(size_t) z] == ref) zone[z].set (p); });
}

void SamplerDevice::loadZonesFromFiles (const juce::Array<juce::File>& files)
{
    const int n = std::min (kZones, files.size());
    if (n == 0) return;
    for (int z = 0; z < kZones; ++z) loadZone (z, z < n ? refForFile (files[z]) : juce::String());
    // spread zones evenly from C1 upwards, each rooted in its centre
    const int span = std::max (6, 72 / n);
    for (int z = 0; z < n; ++z)
    {
        const int lo = z == 0 ? 0 : 24 + z * span, hi = z == n - 1 ? 127 : 24 + (z + 1) * span - 1;
        set (zoneLo (z), (float) lo); set (zoneHi (z), (float) hi);
        set (zoneRoot (z), (float) std::clamp (24 + z * span + span / 2, 0, 127));
    }
    for (int z = n; z < kZones; ++z) { set (zoneLo (z), 127.0f); set (zoneHi (z), 127.0f); }
}

SamplerDevice::Region SamplerDevice::region (int len) const
{
    juce::ignoreUnused (len);
    Region r;
    r.s = pv (start);   // step locks count
    r.e = r.s + pv (length) * (1.0 - r.s);
    r.ls = r.s + pv (loopStart) * (r.e - r.s);
    r.le = std::min (r.e, r.ls + std::max (0.002, (double) pv (loopLen) * (r.e - r.ls)));
    return r;
}

int SamplerDevice::zoneFor (int note) const
{
    for (int z = 0; z < kZones; ++z)
        if (zone[z].audio() != nullptr && note >= (int) get (zoneLo (z)) && note <= (int) get (zoneHi (z))) return z;   // not refs[]: a String the message thread writes
    return refs[0].isNotEmpty() ? 0 : -1;
}

void SamplerDevice::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    for (auto& v : voicesArr) v.env.prepare ((float) sr);
    lfo.prepare ((float) sr);
    driveFx.prepare ((float) sr);
    verb.prepare ((float) sr);
    bufL.assign ((size_t) maxBlock, 0.0f); bufR.assign ((size_t) maxBlock, 0.0f); lfoBuf.assign ((size_t) maxBlock, 0.0f);
    levelSm.setTime (15.0f, (float) sr); levelSm.reset (get (level));
}

void SamplerDevice::reset()
{
    for (auto& v : voicesArr) { v.env.kill(); v.slot = {}; v.sample = nullptr; v.filter.reset(); v.filterR.reset(); }
    held.clear();
    verb.reset();
}

bool SamplerDevice::anyVoiceActive() const
{
    for (auto& v : voicesArr) if (v.env.active()) return true;
    return false;
}

void SamplerDevice::noteOn (int note, float vel)
{
    markNote (note, true);
    const int z = zoneFor (note);
    if (z < 0) return;
    const auto* data = zone[z].audio();
    if (data == nullptr || data->length() < 4) return;
    const bool mono = get (voiceMode) > 0.5f;
    Voice* v;
    if (mono) v = &voicesArr[0];
    else
    {
        std::array<VoiceSlot, kVoices> slots;
        for (size_t i = 0; i < kVoices; ++i) slots[i] = voicesArr[i].slot;
        v = &voicesArr[(size_t) allocateVoice (slots, note, (int) get (voices), clock)];
    }
    const bool glideOn = get (glide) > 0.001f && v->env.active();
    held.push (note, vel);
    v->sample = data;
    v->zoneIdx = z;
    v->rootNote = get (zoneRoot (z));
    v->target = (float) note;
    if (! glideOn) v->pitch = (float) note;
    v->vel = vel;
    const auto r = region (data->length());
    const bool rev = pv (reverse) > 0.5f;
    v->forward = ! rev;
    v->pos = (rev ? r.e : r.s) * (data->length() - 1);
    if (! v->env.active()) v->fadeIn = 0.0f;
    v->env.gateOn();
    v->pegValue = 0.0f; v->pegStage = 0;
    v->slot.note = note; v->slot.held = true; v->slot.active = true; v->slot.age = ++clock;
}

void SamplerDevice::noteOff (int note)
{
    markNote (note, false);
    held.remove (note);
    const int mode = (int) get (playMode);
    for (auto& v : voicesArr)
        if (v.slot.note == note && v.slot.held)
        {
            v.slot.held = false;
            if (mode != 0) v.env.gateOff();   // one-shots play to the end
        }
}

void SamplerDevice::handleEvent (const NoteEvent& e)
{
    switch (e.type)
    {
        case NoteEvent::NoteOn: noteOn (e.note, e.velocity); break;
        case NoteEvent::NoteOff: noteOff (e.note); break;
        case NoteEvent::AllNotesOff: held.clear(); for (auto& v : voicesArr) { v.env.gateOff(); v.slot.held = false; } for (auto& w : heldNotes) w.store (0); break;
        case NoteEvent::Panic: reset(); break;
    }
}

void SamplerDevice::render (float* outL, float* outR, int n)
{
    const float xs = expressionSemis (n);   // pitch bend and mod-wheel vibrato
    const float sr = (float) sampleRate;
    std::fill (bufL.begin(), bufL.begin() + n, 0.0f);
    std::fill (bufR.begin(), bufR.begin() + n, 0.0f);
    const int mode = (int) pv (playMode);
    const float tuneSt = pv (tune) + pv (fine) / 100.0f;
    const float pInt = pv (pegInt) * 12.0f, pAtk = pv (pegAttack), pDec = pv (pegDecay);
    const bool vintage = pv (character) > 0.5f;
    const float bitsV = pv (bits), rateHz = pv (rate) * 1000.0f;
    const int fType = (int) pv (filterType);
    const float cut = pv (cutoff), res = pv (reso), envA = pv (envAmt), keyT = pv (key), lCut = pv (lfoCut);
    const bool filterOn = fType != 0 || cut < 17900.0f || envA != 0.0f || lCut > 0.0f;
    const float velSens = pv (velocity);
    static constexpr int shapeMap[4] = { Lfo::Tri, Lfo::SawDown, Lfo::Square, Lfo::SampleHold };
    lfo.setRate (pv (lfoRate));
    const int lShape = shapeMap[std::clamp ((int) pv (lfoShape), 0, 3)];
    const float lPitch = pv (lfoPitch) * 1.5f;
    const float glideSecs = pv (glide) * 1.5f;
    const float glideCoef = glideSecs < 0.002f ? 1.0f : 1.0f - std::exp (-1.0f / (glideSecs * 0.35f * sr));
    const float a = pv (attack), d = pv (decay), s = pv (sustain), r = pv (release);
    const float pegUp = pAtk <= 0.001f ? 1.0f : 1.0f / (pAtk * sr);
    const float pegDn = std::exp (-1.0f / (pDec * sr));
    const float fadeStep = 1.0f / (0.002f * sr);
    for (int i = 0; i < n; ++i) lfoBuf[(size_t) i] = lfo.process (lShape);   // sized in prepare: any block length

    for (auto& v : voicesArr)
    {
        if (! v.env.active() || v.sample == nullptr) { v.slot.active = false; continue; }
        const auto* data = zone[v.zoneIdx].audio();
        if (data != v.sample) { v.env.kill(); v.slot.active = false; continue; }   // sample replaced: stop safely
        v.env.set (a, d, s, r);
        const int len = data->length();
        const auto reg = region (len);
        const double sPos = reg.s * (len - 1), ePos = reg.e * (len - 1);
        const double lsPos = reg.ls * (len - 1), lePos = reg.le * (len - 1);
        const float* chL = data->buffer.getReadPointer (0);
        const float* chR = data->buffer.getReadPointer (std::min (1, data->buffer.getNumChannels() - 1));
        const double srRatio = data->sampleRate / sampleRate;
        // Loop: the last stretch before the loop end fades into the audio just before the loop start (≤ 10 ms, a
        // quarter of the loop), so the wrap is seamless; a one-shot fades over its last 2 ms
        const double loopLen = std::max (1.0, lePos - lsPos);
        const double xf = mode == 2 ? std::max (0.0, std::min ({ loopLen * 0.25, data->sampleRate * 0.01, v.forward ? lsPos : (len - 1) - lePos })) : 0.0;
        const float endFade = 0.002f * sr;
        for (int i = 0; i < n; ++i)
        {
            v.pitch += (v.target - v.pitch) * glideCoef;
            if (v.pegStage == 0) { v.pegValue += pegUp; if (v.pegValue >= 1.0f) { v.pegValue = 1.0f; v.pegStage = 1; } }
            else v.pegValue *= pegDn;
            const float semis = v.pitch - v.rootNote + tuneSt + pInt * v.pegValue + lfoBuf[i] * lPitch + xs;
            const double inc = semisToRatio (semis) * srRatio;
            float l = Resampler::hermite (chL, len, v.pos), rr = Resampler::hermite (chR, len, v.pos);
            if (xf > 1.0)
            {
                const double into = v.forward ? v.pos - (lePos - xf) : (lsPos + xf) - v.pos;
                if (into > 0.0)
                {
                    const float t = (float) std::min (1.0, into / xf);
                    const double other = v.forward ? v.pos - loopLen : v.pos + loopLen;
                    l += (Resampler::hermite (chL, len, other) - l) * t;
                    rr += (Resampler::hermite (chR, len, other) - rr) * t;
                }
            }
            if (vintage)
            {
                l = v.vintage.vintage (l, rateHz, bitsV, sr);
                rr = l;   // vintage path is mono, like the hardware it imitates
            }
            const float env = v.env.process();
            if (filterOn)
            {
                const float c = std::log (cut) + envA * env * 4.6f + (v.pitch - 60.0f) * keyT * 0.0578f + lCut * lfoBuf[i] * 2.0f;
                const float fc = clamp (std::exp (c), 25.0f, 18500.0f);
                v.filter.set (fc, res, sr); v.filterR.set (fc, res, sr);
                l = v.filter.process (l, fType);
                rr = v.filterR.process (rr, fType);
            }
            if (v.fadeIn < 1.0f) v.fadeIn = std::min (1.0f, v.fadeIn + fadeStep);
            float endGain = 1.0f;
            if (mode != 2)
            {
                const double remain = (v.forward ? ePos - v.pos : v.pos - sPos) / std::max (1.0e-6, inc);
                endGain = (float) std::min (1.0, remain / endFade);
            }
            const float g = env * v.fadeIn * endGain * (1.0f - velSens + velSens * v.vel);
            bufL[(size_t) i] += l * g; bufR[(size_t) i] += rr * g;
            // advance and handle the region ends
            if (v.forward)
            {
                v.pos += inc;
                if (mode == 2 && v.pos >= lePos) v.pos = lsPos + std::fmod (v.pos - lePos, std::max (1.0, lePos - lsPos));
                else if (v.pos >= ePos) { v.env.kill(); break; }
            }
            else
            {
                v.pos -= inc;
                if (mode == 2 && v.pos <= lsPos) v.pos = lePos - std::fmod (lsPos - v.pos, std::max (1.0, lePos - lsPos));
                else if (v.pos <= sPos) { v.env.kill(); break; }
            }
        }
        v.slot.level = v.env.value;
    }
    const float dAmt = pv (drive);
    driveFx.setTone (0.8f);
    const float lvl = pv (level);
    for (int i = 0; i < n; ++i)
    {
        float l = bufL[(size_t) i], rr = bufR[(size_t) i];
        if (dAmt > 0.001f) driveFx.process (l, rr, 0, dAmt, 1.0f);
        const float g = levelSm.tick (lvl);
        bufL[(size_t) i] = l * g; bufR[(size_t) i] = rr * g;
    }
    verb.process (bufL.data(), bufR.data(), bufL.data(), bufR.data(), n, pv (reverb) * reverbScale, pv (revSize), pv (revSize + 1), pv (revSize + 2));
    for (int i = 0; i < n; ++i) { outL[i] += bufL[(size_t) i]; outR[i] += bufR[(size_t) i]; }
    trackSilence (bufL.data(), bufR.data(), n);
}

void SamplerDevice::save (juce::ValueTree& tree) const
{
    DeviceProcessor::save (tree);
    juce::ValueTree z ("Zones");
    for (int i = 0; i < kZones; ++i) z.setProperty ("z" + juce::String (i + 1), refs[(size_t) i], nullptr);
    tree.appendChild (z, nullptr);
}

void SamplerDevice::load (const juce::ValueTree& tree)
{
    DeviceProcessor::load (tree);
    auto z = tree.getChildWithName ("Zones");
    for (int i = 0; i < kZones; ++i) loadZone (i, z.getProperty ("z" + juce::String (i + 1)).toString());
}
} // namespace mb
