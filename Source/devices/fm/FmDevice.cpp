#include "FmDevice.h"

namespace mb
{
using namespace dsp;

FmDevice::FmDevice() : DeviceProcessor (DeviceType::FM)
{
    auto& L = layout;
    pAlgorithm = L.integer ("algorithm", "Algorithm", 1, 32, 5);
    pFeedback = L.cont ("feedback", "Feedback", 0, 1, 0, Unit::Percent);
    opBase = L.size();
    for (int o = 0; o < kOps; ++o)
    {
        const juce::String p = "op" + juce::String (o + 1);
        const bool car = o == 0;   // Init: algorithm 5 carriers, but only OP1 audible
        L.cont (p + "Ratio", "Ratio", 0.5f, 31, 1, Unit::Ratio, true);
        L.choice (p + "Fixed", "Mode", { "Ratio", "Fixed" }, 0);
        L.cont (p + "Freq", "Freq", 1, 9000, 440, Unit::Hz, true);
        L.cont (p + "Detune", "Detune", -7, 7, 0, Unit::Cents, false, true);
        L.cont (p + "Level", "Level", 0, 1, car ? 1.0f : 0.0f, Unit::Percent);
        L.cont (p + "Vel", "Vel Sens", 0, 1, 0.3f, Unit::Percent);
        L.cont (p + "KeyScale", "Key Scale", 0, 1, 0, Unit::Percent);
        L.cont (p + "RateScale", "Rate Scale", 0, 1, 0.2f, Unit::Percent);
        L.cont (p + "R1", "Rate 1", 0, 1, 0.95f, Unit::Percent);
        L.cont (p + "R2", "Rate 2", 0, 1, 0.6f, Unit::Percent);
        L.cont (p + "R3", "Rate 3", 0, 1, 0.5f, Unit::Percent);
        L.cont (p + "R4", "Rate 4", 0, 1, 0.65f, Unit::Percent);
        L.cont (p + "L1", "Level 1", 0, 1, 1, Unit::Percent);
        L.cont (p + "L2", "Level 2", 0, 1, 1, Unit::Percent);
        L.cont (p + "L3", "Level 3", 0, 1, 1, Unit::Percent);
        L.cont (p + "L4", "Level 4", 0, 1, 0, Unit::Percent);
        L.choice (p + "Wave", "Wave", { "Sine", "Half", "Square", "Saw" }, 0);
        L.choice (p + "Mute", "Mute", { "On", "Muted" }, 0);
    }
    const char* peg[6] = { "pegR1", "pegR2", "pegR3", "pegL1", "pegL2", "pegL3" };
    const char* pegN[6] = { "Rate 1", "Rate 2", "Rate 3", "Level 1", "Level 2", "Level 3" };
    for (int i = 0; i < 6; ++i)
        pPeg[i] = i < 3 ? L.cont (peg[i], pegN[i], 0, 1, 0.8f, Unit::Percent) : L.cont (peg[i], pegN[i], -1, 1, 0, Unit::Plain, false, true);
    pLfoShape = L.choice ("lfoShape", "LFO Shape", { "Sine", "Tri", "Saw Dn", "Saw Up", "Sq", "S&H" }, 0);
    pLfoRate = L.cont ("lfoRate", "Rate", 0.05f, 30, 5, Unit::Hz, true);
    pLfoDelay = L.cont ("lfoDelay", "Delay", 0, 4, 0, Unit::Seconds);
    pLfoPitch = L.cont ("lfoPitch", "Pitch", 0, 1, 0, Unit::Percent);
    pLfoAmp = L.cont ("lfoAmp", "Amp", 0, 1, 0, Unit::Percent);
    pVoiceMode = L.choice ("voiceMode", "Voice", { "Poly", "Mono", "Legato" }, 0);
    pVoices = L.integer ("voices", "Voices", 1, 8, 8);
    pTranspose = L.cont ("transpose", "Transpose", -24, 24, 0, Unit::Semis, false, true);
    pGlide = L.cont ("glide", "Glide", 0, 1, 0, Unit::Seconds);
    const char* mac[6] = { "modAttack", "modDecay", "modLevel", "carAttack", "carDecay", "carLevel" };
    const char* macN[6] = { "Mod Attack", "Mod Decay", "Mod Level", "Car Attack", "Car Decay", "Car Level" };
    for (int i = 0; i < 6; ++i) pMacro[i] = L.cont (mac[i], macN[i], -1, 1, 0, Unit::Plain, false, true);
    pChorusMode = L.choice ("chorusMode", "Chorus", { "I", "II", "I+II" }, 0);
    pChorusRate = L.cont ("chorusRate", "Rate", 0, 1, 0.5f, Unit::Percent);
    pChorusDepth = L.cont ("chorusDepth", "Depth", 0, 1, 0.5f, Unit::Percent);
    pChorusMix = L.cont ("chorusMix", "Mix", 0, 1, 0, Unit::Percent);
    pReverb = L.cont ("reverb", "Reverb", 0, 1, 0, Unit::Percent);
    pLevel = L.cont ("level", "Level", 0, 2, 0.7f, Unit::Percent);
    pRevSize = L.size();
    addReverbParams (L);
    finishLayout();

    macro (0, "Mod Level", { { "modLevel", 0.25f, 1.0f } });
    macro (1, "Feedback", { { "feedback", 0.0f, 1.0f } });
    macro (2, "Mod Attack", { { "modAttack", 0.0f, 1.0f } });
    macro (3, "Mod Decay", { { "modDecay", 0.0f, 1.0f } });
    macro (4, "Car Attack", { { "carAttack", 0.0f, 1.0f } });
    macro (5, "Car Decay", { { "carDecay", 0.0f, 1.0f } });
    macro (6, "Algorithm", { { "algorithm", 0.0f, 1.0f } });
    macro (7, "Chorus", { { "chorusMix", 0.0f, 1.0f } });
}

bool FmDevice::isCarrier (int op) const
{
    const auto& a = fm::algorithms()[(size_t) std::clamp ((int) get (pAlgorithm), 1, 32) - 1];
    return (a.carriers >> op) & 1u;
}

void FmDevice::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    for (auto& v : voices)
    {
        for (auto& e : v.env) e.prepare ((float) sr);
        v.pitchEnv.prepare ((float) sr);
    }
    lfo.prepare ((float) sr);
    fx.prepare ((float) sr); verb.prepare ((float) sr);
    bufL.assign ((size_t) maxBlock, 0.0f); bufR.assign ((size_t) maxBlock, 0.0f);
    levelSm.setTime (15.0f, (float) sr); levelSm.reset (get (pLevel) * kLevelScale);
}

void FmDevice::reset()
{
    for (auto& v : voices) { for (auto& e : v.env) e.kill(); v.pitchEnv.kill(); v.slot = {}; }
    held.clear();
    fx.reset(); verb.reset();
}

bool FmDevice::anyVoiceActive() const
{
    for (auto& v : voices) for (auto& e : v.env) if (e.active()) return true;
    return false;
}

void FmDevice::noteOn (int note, float vel)
{
    markNote (note, true);
    const int mode = (int) get (pVoiceMode);
    const bool glideOn = get (pGlide) > 0.001f;
    Voice* v = nullptr;
    bool legato = false;
    if (mode == 0)
    {
        std::array<VoiceSlot, kVoices> slots;
        for (size_t i = 0; i < kVoices; ++i) slots[i] = voices[i].slot;
        v = &voices[(size_t) allocateVoice (slots, note, std::min ((int) get (pVoices), voiceCap (kVoices)), clock)];
    }
    else
    {
        legato = mode == 2 && held.count > 0;
        v = &voices[0];
    }
    held.push (note, vel);
    const float prev = v->pitch;
    v->target = (float) note;
    v->pitch = glideOn && (mode != 0 || v->slot.active) ? prev : (float) note;
    v->vel = vel;
    const bool wasActive = v->env[0].active();
    if (! legato)
    {
        if (! wasActive) { v->fadeIn = 0.0f; for (auto& o : v->op) o.phase = 0.0f; }
        for (auto& e : v->env) e.gateOn();
        if (! v->pitchEnv.active()) v->pitchEnv.value = 0.5f;   // the pitch EG rests at its centre (0 would be an octave down)
        v->pitchEnv.gateOn();
    }
    v->slot.note = note; v->slot.held = true; v->slot.active = true; v->slot.age = ++clock;
    if (lfo.fadeInc < 1.0f) lfo.retrigger();
}

void FmDevice::noteOff (int note)
{
    markNote (note, false);
    held.remove (note);
    const int mode = (int) get (pVoiceMode);
    if (mode != 0)
    {
        auto& v = voices[0];
        if (held.count > 0) { v.target = (float) held.top(); v.slot.note = held.top(); if (mode == 1) { for (auto& e : v.env) e.gateOn(); } return; }
        for (auto& e : v.env) e.gateOff();
        v.pitchEnv.gateOff(); v.slot.held = false;
        return;
    }
    for (auto& v : voices)
        if (v.slot.note == note && v.slot.held)
        {
            for (auto& e : v.env) e.gateOff();
            v.pitchEnv.gateOff(); v.slot.held = false;
        }
}

void FmDevice::handleEvent (const NoteEvent& e)
{
    switch (e.type)
    {
        case NoteEvent::NoteOn: noteOn (e.note, e.velocity); break;
        case NoteEvent::NoteOff: noteOff (e.note); break;
        case NoteEvent::AllNotesOff:
            held.clear();
            for (auto& v : voices) { for (auto& en : v.env) en.gateOff(); v.pitchEnv.gateOff(); v.slot.held = false; }
            for (auto& w : heldNotes) w.store (0);
            break;
        case NoteEvent::Panic: reset(); break;
    }
}

void FmDevice::render (float* outL, float* outR, int n)
{
    const float xs = expressionSemis (n);   // pitch bend and mod-wheel vibrato
    const float sr = (float) sampleRate;
    const int algo = std::clamp ((int) pv (pAlgorithm), 1, 32);
    const auto& A = fm::algorithms()[(size_t) algo - 1];
    const float fb = pv (pFeedback) * 0.45f;
    const float transposeSt = pv (pTranspose);
    const float glideSecs = pv (pGlide) * 1.5f;
    const float glideCoef = glideSecs < 0.002f ? 1.0f : 1.0f - std::exp (-1.0f / (glideSecs * 0.35f * sr));
    static constexpr int shapeMap[6] = { Lfo::Sine, Lfo::Tri, Lfo::SawDown, Lfo::SawUp, Lfo::Square, Lfo::SampleHold };
    const int lShape = shapeMap[std::clamp ((int) pv (pLfoShape), 0, 5)];
    lfo.setRate (pv (pLfoRate));
    lfo.setDelay (pv (pLfoDelay));
    const float lPitch = pv (pLfoPitch) * 0.6f, lAmp = pv (pLfoAmp);
    const float mAtk = pv (pMacro[0]), mDec = pv (pMacro[1]), mLvl = pv (pMacro[2]);
    const float cAtk = pv (pMacro[3]), cDec = pv (pMacro[4]), cLvl = pv (pMacro[5]);

    struct OpSnap { float ratio, freq, det, detRatio, amp, velSens, keyScale, rateScale; bool fixed, muted; int wave; float r[4], l[4]; };
    std::array<OpSnap, kOps> s;
    int numCarriers = 0;
    for (int o = 0; o < kOps; ++o)
    {
        auto& x = s[(size_t) o];
        const bool car = (A.carriers >> o) & 1u;
        x.ratio = pv (opParam (o, Ratio)); x.fixed = pv (opParam (o, Fixed)) > 0.5f; x.freq = pv (opParam (o, Freq));
        x.det = pv (opParam (o, Detune)) / 100.0f;
        x.detRatio = semisToRatio (x.det) * x.ratio;   // once per block, not per sample
        float lvl = clamp (pv (opParam (o, Level)) + (car ? cLvl : mLvl) * 0.25f, 0.0f, 1.0f);
        x.amp = lvl <= 0.0005f ? 0.0f : dbToGain ((lvl - 1.0f) * 60.0f);
        x.velSens = pv (opParam (o, VelSens)); x.keyScale = pv (opParam (o, KeyScale)); x.rateScale = pv (opParam (o, RateScale));
        x.muted = pv (opParam (o, Mute)) > 0.5f;
        x.wave = (int) pv (opParam (o, Wave));
        numCarriers += car && x.amp > 0.0f && ! x.muted ? 1 : 0;   // only carriers that sound share the output
        const float atk = car ? cAtk : mAtk, dec = car ? cDec : mDec;
        for (int k = 0; k < 4; ++k)
        {
            float r = pv (opParam (o, (OpField) (R1 + k)));
            if (k == 0) r = clamp (r - atk * 0.35f, 0.0f, 1.0f);
            if (k == 1 || k == 2) r = clamp (r - dec * 0.3f, 0.0f, 1.0f);
            x.r[k] = r;
            x.l[k] = pv (opParam (o, (OpField) (L1 + k)));
        }
    }
    const float carNorm = 1.0f / std::sqrt ((float) std::max (1, numCarriers));
    const float pegR[3] = { pv (pPeg[0]), pv (pPeg[1]), pv (pPeg[2]) };
    const float pegL[3] = { pv (pPeg[3]), pv (pPeg[4]), pv (pPeg[5]) };
    const bool pegActive = std::abs (pegL[0]) + std::abs (pegL[1]) + std::abs (pegL[2]) > 0.001f;

    std::fill (bufL.begin(), bufL.begin() + n, 0.0f);
    std::fill (bufR.begin(), bufR.begin() + n, 0.0f);
    std::vector<float>& lfoBuf = bufR;   // reuse: LFO first, then stereo copy at the end
    for (int i = 0; i < n; ++i) lfoBuf[(size_t) i] = lfo.process (lShape);
    float* mono = bufL.data();   // the voices sum here (sized in prepare: any block length)
    const float fadeStep = 1.0f / (0.002f * sr);

    for (auto& v : voices)
    {
        // the voice lasts while a carrier sounds (a modulator still releasing toward a level above 0 doesn't keep it)
        bool sounding = false;
        for (int o = 0; o < kOps; ++o) sounding = sounding || (((A.carriers >> o) & 1u) && v.env[(size_t) o].active());
        if (! sounding) { for (auto& e : v.env) e.kill(); v.pitchEnv.kill(); v.slot.active = false; continue; }
        for (int o = 0; o < kOps; ++o)
        {
            auto& e = v.env[(size_t) o];
            for (int k = 0; k < 4; ++k) { e.rates[k] = s[(size_t) o].r[k]; e.levels[k] = s[(size_t) o].l[k]; }
            if ((A.carriers >> o) & 1u) e.levels[3] = 0.0f;   // a carrier's release ends in silence: the voice frees
            e.rateScale = std::exp2 (s[(size_t) o].rateScale * (v.pitch - 60.0f) / 24.0f);
        }
        if (pegActive)
        {
            for (int k = 0; k < 3; ++k) { v.pitchEnv.rates[k] = pegR[k]; v.pitchEnv.levels[k] = 0.5f + 0.5f * pegL[k]; }
            v.pitchEnv.rates[3] = pegR[2]; v.pitchEnv.levels[3] = 0.5f;
        }
        float opAmp[kOps];
        for (int o = 0; o < kOps; ++o)
        {
            const auto& x = s[(size_t) o];
            const float ks = 1.0f - x.keyScale * clamp ((v.pitch - 48.0f) / 48.0f, 0.0f, 1.0f);
            opAmp[o] = x.muted ? 0.0f : x.amp * ks * (1.0f - x.velSens * (1.0f - v.vel));
        }
        float baseHz = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            v.pitch += (v.target - v.pitch) * glideCoef;
            const float lv = lfoBuf[(size_t) i];
            const float peg = pegActive ? (v.pitchEnv.process() - 0.5f) * 2.0f * 12.0f : 0.0f;
            // pitch at a control rate of every 8 samples (0.17 ms): the exp2 and the operators' frequencies
            const bool ctl = (i & 7) == 0;
            if (ctl)
            {
                baseHz = midiToHz (v.pitch + transposeSt + lv * lPitch + xs + peg);
                for (int o = 0; o < kOps; ++o)
                    v.op[(size_t) o].setFreq (s[(size_t) o].fixed ? s[(size_t) o].freq : baseHz * s[(size_t) o].detRatio, sr);
            }
            float out[kOps] {};
            float sum = 0.0f;
            for (int o = kOps - 1; o >= 0; --o)
            {
                const auto& x = s[(size_t) o];
                auto& op = v.op[(size_t) o];
                float pm = 0.0f;
                const uint8_t mods = A.mods[(size_t) o];
                if (mods) for (int m = 0; m < kOps; ++m) if ((mods >> m) & 1u) pm += out[m];
                if (o == A.feedbackOp) pm += fb * op.feedbackSignal();
                const float env = v.env[(size_t) o].process();
                out[o] = op.process (pm, x.wave) * env * opAmp[o] * 2.0f;   // up to ±2 cycles of modulation
                if ((A.carriers >> o) & 1u) sum += out[o] * 0.5f;
            }
            if (v.fadeIn < 1.0f) v.fadeIn = std::min (1.0f, v.fadeIn + fadeStep);
            mono[i] += sum * carNorm * v.fadeIn * (1.0f - lAmp * 0.5f * (1.0f + lv));
        }
        float lvl = 0.0f; for (auto& e : v.env) lvl = std::max (lvl, e.value);
        v.slot.level = lvl;
    }
    for (int i = 0; i < n; ++i) bufR[(size_t) i] = mono[i];
    const float lvlT = pv (pLevel) * kLevelScale;   // headroom for eight voices: a full-velocity 8-note chord peaks below 0 dBFS
    for (int i = 0; i < n; ++i) { const float g = levelSm.tick (lvlT); bufL[(size_t) i] *= g; bufR[(size_t) i] *= g; }
    fx.process (bufL.data(), bufR.data(), n, 1 + (int) pv (pChorusMode), pv (pChorusMix), 0.0f, 0.5, 0.0f,
                transport, 0.4f + 1.2f * pv (pChorusRate), 0.3f + 1.4f * pv (pChorusDepth));
    reverbCard (verb, bufL.data(), bufR.data(), bufL.data(), bufR.data(), n, pv (pReverb) * reverbScale, pv (pRevSize), pv (pRevSize + 1), pv (pRevSize + 2));
    for (int i = 0; i < n; ++i) { outL[i] += softLimit (bufL[(size_t) i]); outR[i] += softLimit (bufR[(size_t) i]); }
    trackSilence (bufL.data(), bufR.data(), n);
}

// ---------------------------------------------------------------------------------------------
std::vector<FmDevice::ImportedVoice> FmDevice::parseBank (const juce::MemoryBlock& mb)
{
    std::vector<ImportedVoice> outVoices;
    const auto* d = static_cast<const uint8_t*> (mb.getData());
    size_t size = mb.getSize();
    size_t off = 0;
    if (size >= 4104 && d[0] == 0xF0) off = 6;          // full sysex dump with header
    else if (size < 4096) return outVoices;
    auto rate = [] (int r) { return std::clamp (r, 0, 99) / 99.0f; };
    // DX7 levels are logarithmic, ~0.75 dB a step: an envelope level becomes an amplitude, the output level a
    // position on the operator's 60 dB Level scale
    auto envLvl = [] (int l) { l = std::clamp (l, 0, 99); return l == 0 ? 0.0f : std::pow (10.0f, (float) (l - 99) * 0.75f / 20.0f); };
    auto outLvl = [] (int l) { return std::clamp (1.0f - (float) (99 - std::clamp (l, 0, 99)) * 0.75f / 60.0f, 0.0f, 1.0f); };
    static constexpr float coarse[32] = { 0.5f, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                                          21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
    for (int vi = 0; vi < 32 && off + (size_t) (vi + 1) * 128 <= size; ++vi)
    {
        const uint8_t* v = d + off + (size_t) vi * 128;
        ImportedVoice iv;
        char name[11] {};
        for (int i = 0; i < 10; ++i) name[i] = (char) std::clamp ((int) v[118 + i], 32, 126);
        iv.name = juce::String (name).trim();
        if (iv.name.isEmpty()) iv.name = "Voice " + juce::String (vi + 1);
        auto add = [&] (const juce::String& id, float val) { iv.values.push_back ({ id, val }); };
        for (int k = 0; k < 6; ++k)
        {
            const uint8_t* o = v + k * 17;     // stored OP6 first
            const int op = 6 - k;
            const juce::String p = "op" + juce::String (op);
            for (int r = 0; r < 4; ++r) add (p + "R" + juce::String (r + 1), rate (o[r]));
            for (int l = 0; l < 4; ++l) add (p + "L" + juce::String (l + 1), envLvl (o[4 + l]));
            add (p + "KeyScale", std::clamp ((int) o[10], 0, 99) / 99.0f * 0.8f);   // the right depth: Minibox scales above the break point
            add (p + "RateScale", (o[12] & 7) / 7.0f);
            add (p + "Vel", ((o[13] >> 2) & 7) / 7.0f);
            add (p + "Level", outLvl (o[14]));
            const bool fixed = (o[15] & 1) != 0;
            const int fc = (o[15] >> 1) & 31;
            const int ff = o[16];
            add (p + "Fixed", fixed ? 1.0f : 0.0f);
            if (fixed) add (p + "Freq", std::pow (10.0f, (float) (fc & 3) + ff / 100.0f));
            else add (p + "Ratio", coarse[fc] * (1.0f + ff / 100.0f));
            add (p + "Detune", (float) (((o[12] >> 3) & 15) - 7));
        }
        for (int r = 0; r < 3; ++r) add ("pegR" + juce::String (r + 1), rate (v[102 + r]));
        for (int l = 0; l < 3; ++l) add ("pegL" + juce::String (l + 1), (std::clamp ((int) v[106 + l], 0, 99) - 50) / 50.0f);
        add ("algorithm", (float) ((v[110] & 31) + 1));
        add ("feedback", (v[111] & 7) / 7.0f);
        add ("lfoRate", 0.1f + v[112] / 99.0f * 30.0f);
        add ("lfoDelay", v[113] / 99.0f * 3.0f);
        add ("lfoPitch", v[114] / 99.0f);
        add ("lfoAmp", v[115] / 99.0f);
        static constexpr int shapeMap[6] = { 1, 2, 3, 4, 0, 5 };   // tri, saw dn, saw up, square, sine, S&H
        add ("lfoShape", (float) shapeMap[std::clamp ((v[116] >> 1) & 7, 0, 5)]);
        add ("transpose", (float) (std::clamp ((int) v[117], 0, 48) - 24));
        outVoices.push_back (std::move (iv));
    }
    return outVoices;
}
} // namespace mb
