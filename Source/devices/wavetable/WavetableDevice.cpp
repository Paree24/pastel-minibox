#include "WavetableDevice.h"

namespace mb
{
using namespace dsp;

namespace
{
constexpr int kCtl = 16;   // samples per control block: modulation, filter coefficients, envelopes' sources
float frac (float x) { return x - std::floor (x); }
float clamp01 (float x) { return clamp (x, 0.0f, 1.0f); }
} // namespace

const juce::StringArray& WavetableDevice::sourceNames()
{
    static const juce::StringArray s { "Off", "LFO 1", "LFO 2", "Amp Env", "Filter Env", "Velocity", "Note", "Mod Wheel", "Pressure", "Random" };
    return s;
}

const juce::StringArray& WavetableDevice::destNames()
{
    static const juce::StringArray s { "Off", "Pitch", "Position A", "Position B", "Level A", "Level B", "Osc B Mod", "Pitch B", "Sub", "Noise",
                                       "Cutoff", "Reso", "Filter Drive", "Distortion", "Fold", "Crush", "Detune", "Width", "Pan", "Amp",
                                       "LFO 1 Rate", "LFO 2 Rate", "Squash", "Chorus", "Volume" };
    return s;
}

WavetableDevice::WavetableDevice() : DeviceProcessor (DeviceType::Wavetable)
{
    auto& L = layout;
    const auto tables = wt::Bank::names();
    for (const char* o : { "A", "B" })
    {
        const juce::String p = juce::String ("osc") + o;
        const bool a = juce::String (o) == "A";
        L.choice (p + "Table", "Table " + juce::String (o), tables, a ? 16 : 24);   // Reese Saw, Growl
        L.cont (p + "Pos", "Position", 0, 1, a ? 0.0f : 0.3f, Unit::Percent);
        L.integer (p + "Oct", "Octave", -3, 3, 0);
        L.integer (p + "Semi", "Semi", -12, 12, 0);
        L.cont (p + "Fine", "Fine", -100, 100, 0, Unit::Cents, false, true);
        L.cont (p + "Phase", "Phase", 0, 1, 0, Unit::Percent);
        L.cont (p + "Level", "Level", 0, 1, a ? 0.8f : 0.0f, Unit::Percent);
        if (! a)
        {
            L.choice ("oscBMode", "Osc B Mod", { "Off", "Sync", "FM", "Ring" }, 0);
            L.cont ("oscBMod", "Mod", 0, 1, 0, Unit::Percent);
        }
    }
    L.choice ("subShape", "Sub Shape", { "Sine", "Tri" }, 0);
    L.cont ("subLevel", "Sub", 0, 1, 0.5f, Unit::Percent);
    L.choice ("subOct", "Sub Octave", { "0 oct", "-1 oct", "-2 oct" }, 1);   // 0: the note's own pitch (a low bass note's sub)
    L.choice ("noiseType", "Color", { "White", "Pink" }, 0);
    L.cont ("noiseLevel", "Noise", 0, 1, 0, Unit::Percent);
    L.cont ("noiseTone", "Tone", 0, 1, 0.6f, Unit::Percent);
    L.cont ("noiseDecay", "Decay", 0.01f, 10, 10, Unit::Seconds, true);   // at its top (the default) the noise holds with the note
    L.choice ("filterMode", "Filter", { "LP 24", "LP 12", "HP", "BP", "Notch" }, 0);
    L.cont ("cutoff", "Cutoff", 20, 20000, 2400, Unit::Hz, true);
    L.cont ("reso", "Reso", 0, 1, 0.15f, Unit::Percent);
    L.cont ("filterDrive", "Drive", 0, 1, 0, Unit::Percent);
    L.cont ("filterEnv", "Env", -1, 1, 0.3f, Unit::Plain, false, true);
    L.cont ("filterKey", "Key", 0, 1, 0.3f, Unit::Percent);
    L.cont ("ampA", "Attack", 0.001f, 10, 0.002f, Unit::Seconds, true);
    L.cont ("ampD", "Decay", 0.005f, 10, 0.4f, Unit::Seconds, true);
    L.cont ("ampS", "Sustain", 0, 1, 1, Unit::Percent);
    L.cont ("ampR", "Release", 0.005f, 10, 0.12f, Unit::Seconds, true);
    L.cont ("fltA", "Attack", 0.001f, 10, 0.002f, Unit::Seconds, true);
    L.cont ("fltD", "Decay", 0.005f, 10, 0.35f, Unit::Seconds, true);
    L.cont ("fltS", "Sustain", 0, 1, 0.2f, Unit::Percent);
    L.cont ("fltR", "Release", 0.005f, 10, 0.2f, Unit::Seconds, true);
    for (const char* l : { "lfo1", "lfo2" })
    {
        const juce::String p (l);
        L.choice (p + "Shape", "Shape", { "Sine", "Tri", "Saw", "Square", "S&H", "Smooth" }, 0);
        L.choice (p + "Sync", "Sync", { "Free", "Sync" }, 1);
        L.cont (p + "Rate", "Rate", 0.05f, 30, 2, Unit::Hz, true);
        L.integer (p + "Div", "Rate", 0, kNumDivisions - 1, 7, Unit::Division);
        L.cont (p + "Depth", "Depth", 0, 1, 0, Unit::Percent);
        L.cont (p + "Fade", "Fade", 0, 4, 0, Unit::Seconds);
        L.choice (p + "Dest", "Dest", destNames(), p == "lfo1" ? DstCutoff : DstPosA);
    }
    L.choice ("distType", "Distortion", { "Soft", "Hard", "Tube" }, 0);
    L.cont ("distDrive", "Drive", 0, 1, 0, Unit::Percent);
    L.cont ("distFold", "Fold", 0, 1, 0, Unit::Percent);
    L.cont ("distCrush", "Crush", 0, 1, 0, Unit::Percent);
    L.cont ("distTone", "Tone", 0, 1, 1, Unit::Percent);
    L.cont ("distMix", "Mix", 0, 1, 1, Unit::Percent);
    for (int s = 0; s < kSlots; ++s)
    {
        const juce::String p = "mod" + juce::String (s + 1);
        L.choice (p + "Src", "Source " + juce::String (s + 1), sourceNames(), 0);
        L.choice (p + "Dst", "Dest " + juce::String (s + 1), destNames(), 0);
        L.cont (p + "Amt", "Amount " + juce::String (s + 1), -1, 1, 0, Unit::Plain, false, true);
    }
    L.choice ("voiceMode", "Voice", { "Mono", "Legato", "Poly" }, 0);
    L.integer ("uniVoices", "Voices", 1, kStacks, 1);
    L.cont ("uniDetune", "Detune", 0, 1, 0.2f, Unit::Percent);
    L.cont ("uniWidth", "Width", 0, 1, 0.6f, Unit::Percent);
    L.cont ("monoBass", "Mono Bass", 0, 400, 120, Unit::Hz);
    L.cont ("uniPhase", "Phase", 0, 1, 0, Unit::Percent);
    L.cont ("glide", "Glide", 0, 1.5f, 0, Unit::Seconds);
    L.cont ("level", "Level", 0, 2, 0.7f, Unit::Percent);
    L.cont ("velocity", "Velocity", 0, 1, 0.3f, Unit::Percent);
    L.cont ("squash", "Squash", 0, 1, 0, Unit::Percent);
    L.cont ("chorus", "Chorus", 0, 1, 0, Unit::Percent);
    jassert (layout.size() == numParams);
    finishLayout();

    auto n = [this] (int p, float v) { return layout[p].toNorm (v); };
    macro (0, "Wobble", { { "lfo1Depth", 0.0f, 1.0f }, { "lfo1Div", n (lfo1Div, 4), n (lfo1Div, 10) } });   // ¼ → 1/16
    macro (1, "Movement", { { "oscAPos", 0.0f, 1.0f }, { "oscBPos", 0.0f, 1.0f } });
    macro (2, "Dirt", { { "distDrive", 0.0f, 0.8f }, { "distFold", 0.0f, 0.5f }, { "distCrush", 0.0f, 0.3f }, { "squash", 0.0f, 0.8f } });
    macro (3, "Bite", { { "cutoff", n (cutoff, 200), n (cutoff, 9000) }, { "reso", 0.1f, 0.6f }, { "oscBMod", 0.0f, 0.6f } });
    macro (4, "Width", { { "uniDetune", 0.0f, 0.6f }, { "uniWidth", 0.0f, 1.0f }, { "chorus", 0.0f, 0.6f } });
    macro (5, "Sub", { { "subLevel", 0.0f, 1.0f } });
    macro (6, "Noise", { { "noiseLevel", 0.0f, 0.6f } });
    macro (7, "Glide", { { "glide", 0.0f, n (glide, 0.4f) } });
}

void WavetableDevice::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    bank = &wt::Bank::get();   // built here (message thread) the first time
    for (auto& v : voices)
    {
        v.amp.prepare ((float) sr); v.flt.prepare ((float) sr);
        v.noise.rng.seed (rng.next());
    }
    for (auto& l : lfos) l.rng.seed (rng.next());
    bufL.assign ((size_t) maxBlock, 0.0f); bufR.assign ((size_t) maxBlock, 0.0f);
    levelSm.setTime (15.0f, (float) sr); levelSm.reset (get (level));
    squasher.set ((float) sr);
    chorusFx.prepare ((float) sr);
    reset();
}

void WavetableDevice::reset()
{
    for (auto& v : voices)
    {
        v.amp.kill(); v.flt.kill(); v.slot = {};
        for (auto& f : v.fL) f.reset();
        for (auto& f : v.fR) f.reset();
    }
    held.clear();
    for (auto& o : distOs) o.reset();
    crushHoldL = crushHoldR = crushCount = toneL = toneR = 0.0f;
    for (auto& f : monoHp) f.reset();
    squasher.reset(); chorusFx.reset(); chorusWasOn = false;
}

bool WavetableDevice::anyVoiceActive() const
{
    for (auto& v : voices) if (v.amp.active()) return true;
    return false;
}

int WavetableDevice::stacksNow() const { return std::clamp ((int) get (uniVoices), 1, kStacks); }

int WavetableDevice::maxVoices() const
{
    if ((int) get (voiceMode) != 2) return 1;
    return voiceCap (std::clamp (kStackBudget / stacksNow(), 1, kVoices));
}

void WavetableDevice::voiceOn (Voice& v, int note, float vel, bool retrigger, bool glideOn)
{
    const bool wasActive = v.amp.active();
    v.target = (float) note;
    if (! glideOn) v.pitch = (float) note;
    else if (! wasActive) v.pitch = lastNote;   // poly glide: from the last note played
    v.vel = vel;
    if (retrigger)
    {
        if (! wasActive)
        {
            // a fresh start: the oscillators from their phase (Phase), unison stacks spread by Phase
            const int n = stacksNow();
            v.stacks = n;
            const float spr = get (uniPhase);
            for (int k = 0; k < kStacks; ++k)
            {
                auto& s = v.st[(size_t) k];
                s.spread = n == 1 ? 0.0f : 2.0f * (float) k / (float) (n - 1) - 1.0f;
                const float r = rng.unipolar();
                s.phA = frac (get (oscAPhase) + spr * r);
                s.phB = frac (get (oscBPhase) + spr * rng.unipolar());
            }
            v.subPh = 0.0f;
            for (auto& f : v.fL) f.reset();
            for (auto& f : v.fR) f.reset();
            v.fadeIn = 1.0f;
            v.posA = get (oscAPos); v.posB = get (oscBPos);
        }
        else if (v.slot.note != note && (int) get (voiceMode) == 2) v.fadeIn = 0.0f;   // a stolen voice: a 2 ms fade, no click
        v.amp.gateOn (false); v.flt.gateOn (false);
        v.noiseEnv = 1.0f;
        v.rnd = rng.unipolar();
    }
    v.slot.note = note; v.slot.held = true; v.slot.active = true; v.slot.age = ++clock;
    lastVoice = (int) (&v - voices.data());
    lastNote = (float) note;
}

void WavetableDevice::noteOn (int note, float vel)
{
    markNote (note, true);
    const int mode = (int) get (voiceMode);
    const bool glideOn = get (glide) > 0.001f;
    bool anyHeld = false;
    for (auto& v : voices) anyHeld = anyHeld || v.slot.held;
    if (! anyHeld)   // the free-running LFOs restart with the first key (synced ones follow the bar)
        for (int l = 0; l < 2; ++l) if (get (l == 0 ? lfo1Sync : lfo2Sync) < 0.5f) lfos[(size_t) l].retrigger();
    if (mode != 2)
    {
        const bool legato = held.count > 0;
        held.push (note, vel);
        auto& v = voices[0];
        const bool retrig = mode == 0 || ! legato;
        voiceOn (v, note, vel, retrig, glideOn && (mode == 0 ? v.amp.active() : legato));
        return;
    }
    std::array<VoiceSlot, kVoices> slots;
    for (size_t i = 0; i < (size_t) kVoices; ++i) slots[i] = voices[i].slot;
    const int idx = allocateVoice (slots, note, maxVoices(), clock);
    voiceOn (voices[(size_t) idx], note, vel, true, glideOn);
}

void WavetableDevice::noteOff (int note)
{
    markNote (note, false);
    if ((int) get (voiceMode) != 2)
    {
        held.remove (note);
        auto& v = voices[0];
        if (held.count > 0) { if (v.slot.note != held.top()) voiceOn (v, held.top(), held.topVel(), false, true); }
        else { v.amp.gateOff(); v.flt.gateOff(); v.slot.held = false; }
        return;
    }
    for (auto& v : voices)
        if (v.slot.note == note && v.slot.held) { v.amp.gateOff(); v.flt.gateOff(); v.slot.held = false; }
}

void WavetableDevice::handleEvent (const NoteEvent& e)
{
    switch (e.type)
    {
        case NoteEvent::NoteOn: noteOn (e.note, e.velocity); break;
        case NoteEvent::NoteOff: noteOff (e.note); break;
        case NoteEvent::AllNotesOff:
            held.clear();
            for (auto& v : voices) { v.amp.gateOff(); v.flt.gateOff(); v.slot.held = false; }
            for (auto& w : heldNotes) w.store (0);
            break;
        case NoteEvent::Panic: reset(); break;
        default: break;
    }
}

float WavetableDevice::Lfo::tick (int shape, float inc, float fadeInc)
{
    phase += inc;
    wrapped = false;
    if (phase >= 1.0f)
    {
        phase -= std::floor (phase);
        prevHeld = held; held = rng.bipolar();
        wrapped = synced;   // it took its new value here; the bar's wrap next block mustn't take another
    }
    synced = false;
    fade = std::min (1.0f, fade + fadeInc);
    float v = 0.0f;
    switch (shape)
    {
        case 0: v = std::sin (kTwoPi * phase); break;
        case 1: v = 4.0f * std::abs (phase - 0.5f) - 1.0f; break;
        case 2: v = 1.0f - 2.0f * phase; break;                               // a falling ramp: a "wub" that closes
        case 3: v = phase < 0.5f ? 1.0f : -1.0f; break;
        case 4: v = held; break;
        default: { const float t = phase * phase * (3.0f - 2.0f * phase); v = prevHeld + (held - prevHeld) * t; break; }
    }
    out = v * fade;
    return out;
}

float WavetableDevice::dist (float x, int type, float drive, float fold) const
{
    float y = x;
    if (drive > 0.0f)
    {
        const float g = 1.0f + drive * drive * 40.0f;
        y *= g;
        switch (type)
        {
            case 1: y = clamp (y, -1.0f, 1.0f); break;
            case 2: y = fastTanh (y + 0.35f) - 0.336376f; break;   // tube: uneven, the even harmonics
            default: y = fastTanh (y); break;
        }
        y *= 1.0f / (1.0f + drive * 2.2f) * (1.0f + drive * 0.9f);
    }
    if (fold > 0.0f)
    {
        const float f = std::sin (kPi * 0.5f * y * (1.0f + fold * 6.0f));
        y += (f - y) * std::min (1.0f, fold * 3.0f);
    }
    return y;
}

void WavetableDevice::render (float* outL, float* outR, int n)
{
    if (bank == nullptr) return;
    std::fill (bufL.begin(), bufL.begin() + n, 0.0f);
    std::fill (bufR.begin(), bufR.begin() + n, 0.0f);
    const float xs = expressionSemis (n);
    exprSemis = xs;
    for (int s = 0; s < n; s += kCtl) renderBlock (bufL.data(), bufR.data(), s, std::min (kCtl, n - s));
    for (int i = 0; i < n; ++i) { outL[i] += softLimit (bufL[(size_t) i]); outR[i] += softLimit (bufR[(size_t) i]); }
    trackSilence (bufL.data(), bufR.data(), n);
    for (auto& v : voices) if (! v.amp.active()) { v.slot.active = false; v.slot.level = 0.0f; } else v.slot.level = v.amp.value;
}

void WavetableDevice::renderBlock (float* L, float* R, int start, int len)
{
    const float sr = (float) sampleRate;
    // ---- the LFOs (global): one value per control block
    std::array<float, 2> lfoVal {};
    std::array<float, kNumDst> globalMod {};
    auto slotMods = [this] (const std::array<float, kNumSrc>& src, std::array<float, kNumDst>& dst)
    {
        for (int s = 0; s < kSlots; ++s)
        {
            const int si = (int) pv (slotParam (s, 0)), di = (int) pv (slotParam (s, 1));
            if (si <= 0 || di <= 0 || si >= kNumSrc || di >= kNumDst) continue;
            dst[(size_t) di] += src[(size_t) si] * pv (slotParam (s, 2));
        }
        for (int l = 0; l < 2; ++l)
        {
            const int di = (int) pv (l == 0 ? lfo1Dest : lfo2Dest);
            if (di > 0 && di < kNumDst) dst[(size_t) di] += src[(size_t) (SrcLfo1 + l)];
        }
    };
    {
        // the LFO rates follow the matrix too (from global sources only: an LFO can't depend on a voice)
        std::array<float, kNumSrc> gsrc {};
        gsrc[SrcLfo1] = lfos[0].out; gsrc[SrcLfo2] = lfos[1].out; gsrc[SrcWheel] = modWheel; gsrc[SrcPressure] = pressure;
        slotMods (gsrc, globalMod);
        for (int l = 0; l < 2; ++l)
        {
            const int b = l == 0 ? lfo1Shape : lfo2Shape;   // Shape, Sync, Rate, Div, Depth, Fade, Dest
            const bool synced = pv (b + 1) > 0.5f;
            const double beats = divisionBeats (pv (b + 3)), bpm = std::max (20.0, transport.bpm);
            float hz = synced ? (float) (bpm / 60.0 / beats) : pv (b + 2);
            hz *= std::exp2 (3.0f * clamp (globalMod[(size_t) (DstLfo1Rate + l)], -1.0f, 1.0f));
            auto& lf = lfos[(size_t) l];
            if (synced && transport.playing)
            {
                const double pos = transport.beatPos + (double) start / sampleRate * bpm / 60.0;
                const float want = (float) (pos / beats - std::floor (pos / beats));
                // a new random value only when the bar's cycle really wraps (from near 1 to near 0): the LFO's own phase
                // runs a block ahead of the bar's, so a plain "want < phase" was true almost every block, and the S&H and
                // Smooth shapes drew a new value every block (a hiss, not a movement)
                if (want + 0.5f < lf.phase && ! lf.wrapped) { lf.prevHeld = lf.held; lf.held = lf.rng.bipolar(); }
                lf.phase = want;
                lf.synced = true;
            }
            const float fadeSecs = pv (b + 5);
            const float fadeInc = fadeSecs <= 0.001f ? 1.0f : (float) len / (fadeSecs * sr);
            lfoVal[(size_t) l] = lf.tick ((int) pv (b), hz * (float) len / sr, fadeInc) * pv (b + 4);
            lf.out = lfoVal[(size_t) l];
        }
    }
    std::array<float, kNumSrc> base {};
    base[SrcLfo1] = lfoVal[0]; base[SrcLfo2] = lfoVal[1]; base[SrcWheel] = modWheel; base[SrcPressure] = pressure;

    // ---- the voices
    const int tA = (int) pv (oscATable), tB = (int) pv (oscBTable), bMode = (int) pv (oscBMode);
    const float semA = pv (oscAOct) * 12.0f + pv (oscASemi) + pv (oscAFine) * 0.01f;
    const float semB = pv (oscBOct) * 12.0f + pv (oscBSemi) + pv (oscBFine) * 0.01f;
    const int fMode = (int) pv (filterMode), sShape = (int) pv (subShape), nType = (int) pv (noiseType);
    const float subOctSemis = -12.0f * std::round (pv (subOct));
    const float nDecay = pv (noiseDecay);
    const float nDecayMul = nDecay >= 9.9f ? 1.0f : std::exp (-1.0f / (nDecay * sr));
    const float nToneA = 1.0f - std::exp (-kTwoPi * std::min (200.0f * std::pow (100.0f, pv (noiseTone)), sr * 0.45f) / sr);
    const float glideSecs = pv (glide);
    const float glideCoef = glideSecs <= 0.001f ? 1.0f : 1.0f - std::exp (-(float) len / (glideSecs * 0.35f * sr));
    const float velSens = pv (velocity);
    const float fadeStep = 1.0f / (0.002f * sr);
    std::array<float, kNumDst> lastMod = globalMod;
    bool anyVoice = false;
    for (int vi = 0; vi < kVoices; ++vi)
    {
        auto& v = voices[(size_t) vi];
        if (! v.amp.active()) continue;
        anyVoice = true;
        v.amp.set (pv (ampA), pv (ampD), pv (ampS), pv (ampR));
        v.flt.set (pv (fltA), pv (fltD), pv (fltS), pv (fltR));
        auto src = base;
        src[SrcAmpEnv] = v.amp.value; src[SrcFltEnv] = v.flt.value; src[SrcVel] = v.vel;
        src[SrcNote] = (v.pitch - 60.0f) / 48.0f; src[SrcRandom] = v.rnd * 2.0f - 1.0f;
        std::array<float, kNumDst> m {};
        slotMods (src, m);
        if (vi == lastVoice) lastMod = m;

        v.pitch += (v.target - v.pitch) * glideCoef;
        const float note = v.pitch + exprSemis + m[DstPitch] * 12.0f;
        const float pitchB = note + semB + m[DstPitchB] * 12.0f + (bMode == 1 ? clamp01 (pv (oscBMod) + m[DstBMod]) * 36.0f : 0.0f);
        const float incA = midiToHz (note + semA) / sr, incB = midiToHz (pitchB) / sr;
        const float bMod = clamp01 (pv (oscBMod) + m[DstBMod]);
        const float fm = bMode == 2 ? bMod * bMod * 1.5f : 0.0f;
        const int lvA = wt::Bank::levelFor (incA * 1.03f);
        const int lvB = std::min (wt::kLevels - 1, wt::Bank::levelFor (incB * 1.03f) + (fm > 0.0f ? 1 + (int) (fm * 2.0f) : 0));
        const float lA = clamp01 (pv (oscALevel) + m[DstLevelA]), lB = clamp01 (pv (oscBLevel) + m[DstLevelB]);
        const float posA1 = clamp01 (pv (oscAPos) + m[DstPosA]), posB1 = clamp01 (pv (oscBPos) + m[DstPosB]);
        const float posA0 = v.posA, posB0 = v.posB;
        v.posA = posA1; v.posB = posB1;
        const float subL = clamp01 (pv (subLevel) + m[DstSub]) * 0.8f;
        const float subInc = midiToHz (v.pitch + exprSemis + subOctSemis) / sr;
        const float noiseL = clamp01 (pv (noiseLevel) + m[DstNoise]) * 0.7f;
        // unison: each stack a little sharp or flat, spread across the field
        const float det = clamp01 (pv (uniDetune) + m[DstDetune]);
        const float width = clamp01 (pv (uniWidth) + m[DstWidth]);
        const float detSemis = det * det * 0.9f;
        const int ns = v.stacks;
        std::array<float, kStacks> ratio {}, gl {}, gr {};
        for (int k = 0; k < ns; ++k)
        {
            const float sp = v.st[(size_t) k].spread;
            ratio[(size_t) k] = semisToRatio (sp * detSemis);
            const float p = clamp (sp * width, -1.0f, 1.0f);
            gl[(size_t) k] = std::sqrt (0.5f * (1.0f - p)); gr[(size_t) k] = std::sqrt (0.5f * (1.0f + p));
        }
        const float stackNorm = 1.0f / std::sqrt ((float) ns) * 0.7f;
        // the filter for this block
        const float cutNote = std::log2 (pv (cutoff)) + pv (filterEnv) * v.flt.value * 8.0f + pv (filterKey) * (v.pitch - 60.0f) / 12.0f
                              + m[DstCutoff] * 5.0f;
        const float fc = clamp (std::exp2 (cutNote), 18.0f, std::min (20000.0f, sr * 0.45f));
        const float res = clamp01 (pv (reso) + m[DstReso]);
        const float q = 2.0f - 1.97f * res;
        const bool two = fMode == 0;
        v.fL[0].set (fc, two ? std::max (q, 1.0f) : q, sr); v.fR[0].set (fc, two ? std::max (q, 1.0f) : q, sr);
        v.fL[1].set (fc, q, sr); v.fR[1].set (fc, q, sr);
        const float fdrv = clamp01 (pv (filterDrive) + m[DstFilterDrive]);
        const float fdGain = 1.0f + fdrv * 6.0f, fdMake = 1.0f / (1.0f + fdrv * 2.0f);
        const float pan = clamp (m[DstPan], -1.0f, 1.0f);
        const float panL = 1.0f - std::max (0.0f, pan), panR = 1.0f + std::min (0.0f, pan);
        const float ampMod = clamp (1.0f + m[DstAmp], 0.0f, 2.0f);
        const float velGain = 1.0f - velSens * (1.0f - v.vel);
        for (int i = 0; i < len; ++i)
        {
            const float t = (float) (i + 1) / (float) len;
            const float pa = posA0 + (posA1 - posA0) * t, pb = posB0 + (posB1 - posB0) * t;
            float sl = 0.0f, sr2 = 0.0f;
            for (int k = 0; k < ns; ++k)
            {
                auto& s = v.st[(size_t) k];
                const float a = bank->sample (tA, lvA, pa, s.phA);
                float phB = s.phB;
                if (fm > 0.0f) phB = frac (phB + fm * a);
                float b = bank->sample (tB, lvB, pb, phB);
                if (bMode == 3) b += (b * a * 1.4f - b) * bMod;
                const float x = a * lA + b * lB;
                sl += x * gl[(size_t) k]; sr2 += x * gr[(size_t) k];
                const float ia = incA * ratio[(size_t) k];
                s.phA += ia;
                if (s.phA >= 1.0f)
                {
                    s.phA -= 1.0f;
                    if (bMode == 1) s.phB = s.phA * (incB / incA);   // hard sync: B restarts with A
                }
                s.phB += incB * ratio[(size_t) k];
                s.phB -= std::floor (s.phB);
            }
            sl *= stackNorm; sr2 *= stackNorm;
            // the sub: one voice, in the middle
            v.subPh += subInc; if (v.subPh >= 1.0f) v.subPh -= 1.0f;
            const float sub = (sShape == 0 ? std::sin (kTwoPi * v.subPh) : 1.0f - 4.0f * std::abs (v.subPh - 0.5f)) * subL;
            float nz = 0.0f;
            if (noiseL > 0.0f)
            {
                const float w = nType == 0 ? v.noise.white() : v.noise.pink() * 2.5f;
                v.noiseLp += nToneA * (w - v.noiseLp);
                nz = v.noiseLp * noiseL * v.noiseEnv;
                v.noiseEnv *= nDecayMul;
            }
            float xl = sl + sub + nz, xr = sr2 + sub + nz;
            if (fdrv > 0.0f) { xl = fastTanh (xl * fdGain) * fdMake * 1.6f; xr = fastTanh (xr * fdGain) * fdMake * 1.6f; }
            auto filt = [&] (std::array<Svf, 2>& f, float x)
            {
                float lp, bp, hp;
                f[0].tick (x, lp, bp, hp);
                switch (fMode)
                {
                    case 0: { float lp2, bp2, hp2; f[1].tick (lp, lp2, bp2, hp2); return lp2; }
                    case 1: return lp;
                    case 2: return hp;
                    case 3: return bp * q;
                    default: return lp + hp;
                }
            };
            xl = filt (v.fL, xl); xr = filt (v.fR, xr);
            const float env = v.amp.process();
            v.flt.process();
            if (v.fadeIn < 1.0f) v.fadeIn = std::min (1.0f, v.fadeIn + fadeStep);
            const float g = env * velGain * ampMod * v.fadeIn;
            L[start + i] += xl * g * panL;
            R[start + i] += xr * g * panR;
        }
    }
    if (! anyVoice) lastMod = globalMod;

    // ---- distortion, the mono low end, the level (the whole instrument)
    const int dType = (int) pv (distType);
    const float drive = clamp01 (pv (distDrive) + lastMod[DstDist]), fold = clamp01 (pv (distFold) + lastMod[DstFold]);
    const float crush = clamp01 (pv (distCrush) + lastMod[DstCrush]), mix = pv (distMix), tone = pv (distTone);
    if ((drive > 0.001f || fold > 0.001f || crush > 0.001f) && mix > 0.001f)
    {
        const float toneA = tone >= 0.999f ? 1.0f : 1.0f - std::exp (-kTwoPi * 600.0f * std::pow (33.0f, tone) / sr);
        const float q = crush > 0.001f ? std::exp2 (15.0f - 13.0f * crush) : 0.0f;
        const float hold = 1.0f + crush * crush * 15.0f;
        for (int i = start; i < start + len; ++i)
        {
            const float dl = L[i], dr = R[i];
            // 4x (2x inside 2x): hard clipping and folding of a bright, squashed sound alias audibly at 2x
            float wl = run2x (distOs[0], dl, [&] (float x) { return run2x (distOs[2], x, [&] (float y) { return dist (y, dType, drive, fold); }); });
            float wr = run2x (distOs[1], dr, [&] (float x) { return run2x (distOs[3], x, [&] (float y) { return dist (y, dType, drive, fold); }); });
            if (q > 0.0f)
            {
                if ((crushCount += 1.0f) >= hold) { crushCount -= hold; crushHoldL = std::round (wl * q) / q; crushHoldR = std::round (wr * q) / q; }
                wl = crushHoldL; wr = crushHoldR;
            }
            if (toneA < 1.0f) { toneL += toneA * (wl - toneL); toneR += toneA * (wr - toneR); wl = toneL; wr = toneR; }
            L[i] = dl + (wl - dl) * mix; R[i] = dr + (wr - dr) * mix;
        }
    }
    // Squash: each band toward one level (gains set once a block, ramped across it)
    if (const float sq = clamp01 (pv (squash) + lastMod[DstSquash]); sq > 0.001f)
    {
        std::array<float, 3> pk {};
        static thread_local std::array<std::array<float, 3>, 2 * 16> bands;   // a block's bands (kCtl samples, both sides)
        for (int i = start; i < start + len; ++i)
        {
            auto& bl = bands[(size_t) ((i - start) * 2)]; auto& br = bands[(size_t) ((i - start) * 2 + 1)];
            squasher.split (0, L[i], bl[0], bl[1], bl[2]);
            squasher.split (1, R[i], br[0], br[1], br[2]);
            for (int b = 0; b < 3; ++b) pk[(size_t) b] += bl[(size_t) b] * bl[(size_t) b] + br[(size_t) b] * br[(size_t) b];
        }
        for (auto& v : pk) v = std::sqrt (v / (2.0f * (float) len));   // each band's RMS over the block (its energy, not its peaks)
        // each band against the whole sound's level (not a fixed level: the same at any volume): the mids and the top
        // are pulled up toward it (upward compression, how a squashed bass gets its density and edge) and pulled down
        // when they pass it; the low band is left alone, so the sub stays as it is
        const float atk = 1.0f - std::exp (-(float) len / (0.002f * sr)), rel = 1.0f - std::exp (-(float) len / (0.09f * sr));
        std::array<float, 3> g0 = squasher.gain;
        const float rmsCoef = 1.0f - std::exp (-1.0f / (0.03f * sr));
        float all = 0.0f;
        for (int b = 0; b < 3; ++b)
        {
            auto& e = squasher.env[(size_t) b];
            e += (pk[(size_t) b] - e) * (pk[(size_t) b] > e ? atk : rel);
            all = std::max (all, e);
        }
        // where each band settles under the loudest (dB RMS); heavy Squash brings the top up close (the flat, bright top of
        // a heavily squashed growl: -10 dB at light settings, -4 dB at full)
        const float offset[3] = { 0.0f, -3.0f, -10.0f + 6.0f * sq * sq };
        const float ref = gainToDb (all);
        for (int b = 1; b < 3; ++b)
        {
            const float lvl = gainToDb (squasher.env[(size_t) b]);
            float gdb = 0.0f;
            if (ref > -70.0f) gdb = juce::jlimit (-12.0f, 24.0f, (ref + offset[b] - lvl) * 0.85f * sq) * clamp ((ref + 70.0f) / 20.0f, 0.0f, 1.0f);
            // a band far under the rest holds no content, only its floor (aliasing, the chorus's hiss): don't lift it
            if (gdb > 0.0f) gdb *= clamp ((lvl - ref + 50.0f) / 15.0f, 0.0f, 1.0f);
            squasher.gain[(size_t) b] = dbToGain (gdb);
        }
        squasher.gain[0] = 1.0f;
        for (int i = start; i < start + len; ++i)
        {
            const float t = (float) (i - start + 1) / (float) len;
            const auto& bl = bands[(size_t) ((i - start) * 2)]; const auto& br = bands[(size_t) ((i - start) * 2 + 1)];
            float l = 0.0f, r = 0.0f;
            for (int b = 0; b < 3; ++b)
            {
                const float g = g0[(size_t) b] + (squasher.gain[(size_t) b] - g0[(size_t) b]) * t;
                l += bl[(size_t) b] * g; r += br[(size_t) b] * g;
            }
            // the peaks the lifted bands make are rounded off against the running level (a squashed sound is dense: its
            // peaks stay about 6 dB over its body, as the reference basses')
            squasher.rms += (0.5f * (l * l + r * r) - squasher.rms) * rmsCoef;
            const float th = std::max (1.0e-4f, (2.2f - 0.7f * sq) * std::sqrt (squasher.rms));
            const float cl = th * fastTanh (l / th), cr = th * fastTanh (r / th);
            const float w = std::min (1.0f, sq * 1.6f);
            L[i] = l + (cl - l) * w; R[i] = r + (cr - r) * w;
        }
    }
    // Chorus: width (the low end is made mono again below)
    const float ch = clamp01 (pv (chorus) + lastMod[DstChorus]);
    const bool chOn = ch > 0.001f;
    if (chOn && ! chorusWasOn) chorusFx.reset();
    chorusWasOn = chOn;
    if (chOn) for (int i = start; i < start + len; ++i) chorusFx.process (L[i], R[i], 3, ch);
    const float mb = pv (monoBass);
    if (mb >= 20.0f)
    {
        // below Mono Bass the two sides become one: the side (L − R) through a 24 dB high-pass, the middle untouched,
        // so a wide stack keeps a solid, centred low end
        for (int k = 0; k < 2; ++k) monoHp[(size_t) k].set (mb, 1.414f, sr);
        for (int i = start; i < start + len; ++i)
        {
            const float mid = 0.5f * (L[i] + R[i]);
            float side = 0.5f * (L[i] - R[i]), lp, bp, hp;
            monoHp[0].tick (side, lp, bp, hp); monoHp[1].tick (hp, lp, bp, side);
            L[i] = mid + side; R[i] = mid - side;
        }
    }
    // Volume: the whole instrument's level after the distortion and Squash (±24 dB), so an envelope there swells the sound
    // the drive would otherwise flatten (a Foghorn's ramp)
    const float lv = pv (level) * juce::Decibels::decibelsToGain (clamp (lastMod[DstVolume], -1.0f, 1.0f) * 24.0f);
    for (int i = start; i < start + len; ++i) { const float g = levelSm.tick (lv); L[i] *= g; R[i] *= g; }
}

void WavetableDevice::randomize (int scope, uint32_t seed)
{
    Rng r { seed * 2654435761u + 1u };
    auto u = [&] { return r.unipolar(); };
    auto range = [&] (float a, float b) { return a + (b - a) * u(); };
    auto pick = [&] (std::initializer_list<float> xs) { auto it = xs.begin(); std::advance (it, std::min ((int) xs.size() - 1, (int) (u() * xs.size()))); return *it; };
    auto setv = [this] (int p, float v) { set (p, v); };
    const bool all = scope == 0;
    if (all || scope == 1)
    {
        setv (oscATable, (float) (int) (u() * wt::kTables)); setv (oscAPos, u());
        setv (oscAOct, 0); setv (oscASemi, 0); setv (oscAFine, 0); setv (oscAPhase, pick ({ 0.0f, 0.0f, 0.25f }));
        setv (oscALevel, range (0.6f, 0.9f));
        setv (oscBTable, (float) (int) (u() * wt::kTables)); setv (oscBPos, u());
        setv (oscBOct, pick ({ 0, 0, 1, -1 })); setv (oscBSemi, pick ({ 0, 0, 7, 12, -12, 5 })); setv (oscBFine, range (-12, 12));
        setv (oscBPhase, u()); setv (oscBLevel, pick ({ 0.0f, range (0.3f, 0.8f), range (0.3f, 0.8f) }));
        setv (oscBMode, (float) (int) (u() * 4)); setv (oscBMod, range (0.0f, 0.7f));
        setv (subShape, pick ({ 0, 0, 1 })); setv (subLevel, range (0.3f, 0.9f)); setv (subOct, pick ({ 0, 0, 1 }));
        setv (noiseLevel, pick ({ 0.0f, 0.0f, range (0.05f, 0.35f) })); setv (noiseTone, u()); setv (noiseDecay, range (0.03f, 0.5f));
    }
    if (all || scope == 2)
    {
        setv (filterMode, pick ({ 0, 0, 0, 1, 3, 4 })); setv (cutoff, std::exp2 (range (std::log2 (180.0f), std::log2 (9000.0f))));
        setv (reso, range (0.0f, 0.65f)); setv (filterDrive, pick ({ 0.0f, range (0.1f, 0.6f) })); setv (filterEnv, range (-0.2f, 0.8f));
        setv (filterKey, range (0.0f, 0.6f));
        setv (ampA, range (0.001f, 0.03f)); setv (ampD, range (0.1f, 1.2f)); setv (ampS, range (0.5f, 1.0f)); setv (ampR, range (0.04f, 0.5f));
        setv (fltA, range (0.001f, 0.05f)); setv (fltD, range (0.08f, 1.0f)); setv (fltS, range (0.0f, 0.6f)); setv (fltR, range (0.05f, 0.6f));
    }
    if (all || scope == 3)
    {
        for (int l = 0; l < 2; ++l)
        {
            const int b = l == 0 ? lfo1Shape : lfo2Shape;
            setv (b, (float) (int) (u() * 6)); setv (b + 1, u() < 0.8f ? 1.0f : 0.0f); setv (b + 2, range (0.2f, 8.0f));
            setv (b + 3, pick ({ 4, 7, 8, 10 })); setv (b + 4, pick ({ 0.0f, range (0.2f, 0.8f) })); setv (b + 5, 0.0f);
            setv (b + 6, pick ({ (float) DstCutoff, (float) DstPosA, (float) DstPosB, (float) DstBMod, (float) DstOff }));
        }
        for (int s = 0; s < kSlots; ++s)
        {
            const bool on = s < 3 && u() < 0.6f;
            setv (slotParam (s, 0), on ? (float) pick ({ (float) SrcFltEnv, (float) SrcVel, (float) SrcRandom, (float) SrcAmpEnv, (float) SrcNote }) : 0.0f);
            setv (slotParam (s, 1), on ? (float) pick ({ (float) DstPosA, (float) DstPosB, (float) DstBMod, (float) DstCutoff, (float) DstDist, (float) DstNoise }) : 0.0f);
            setv (slotParam (s, 2), on ? range (-0.6f, 0.6f) : 0.0f);
        }
    }
    if (all || scope == 4)
    {
        setv (distType, (float) (int) (u() * 3)); setv (distDrive, pick ({ 0.0f, range (0.1f, 0.6f), range (0.1f, 0.6f) }));
        setv (distFold, pick ({ 0.0f, 0.0f, range (0.05f, 0.4f) })); setv (distCrush, pick ({ 0.0f, 0.0f, 0.0f, range (0.05f, 0.3f) }));
        setv (distTone, range (0.5f, 1.0f)); setv (distMix, 1.0f);
        setv (uniVoices, pick ({ 1, 1, 3, 5, 7 })); setv (uniDetune, range (0.05f, 0.45f)); setv (uniWidth, range (0.3f, 1.0f));
        setv (monoBass, range (80.0f, 180.0f)); setv (uniPhase, u());
    }
}
} // namespace mb
