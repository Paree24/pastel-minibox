#include "DrumsDevice.h"

namespace mb
{
using namespace dsp;

namespace drums
{
const char* engineName (Engine e)
{
    static const char* n[] = { "Classic", "Punch", "Box", "Ring", "Layer", "Bell" };
    return n[e];
}

const std::vector<Engine>& enginesForPart (int part)
{
    static const std::vector<Engine> kick { Classic, Punch, Box, Ring, Layer }, perc { Classic, Bell, Punch, Box, Layer },
        other { Classic, Punch, Box, Layer };
    return part == 0 ? kick : (part == 7 ? perc : other);
}
} // namespace drums

namespace
{
const char* kPartNames[8] = { "Kick", "Snare", "Clap", "Tom", "C Hat", "O Hat", "Cymbal", "Perc" };
const int kPartNotes[8] = { 36, 38, 39, 45, 42, 46, 49, 37 };
//                                 kick  snare clap  tom   ch    oh    cym   perc
const float kDefPitch[8] = { 50, 180, 1150, 120, 420, 420, 420, 640 };
const float kDefDecay[8] = { 0.45f, 0.22f, 0.3f, 0.4f, 0.1f, 0.8f, 2.8f, 0.18f };
const float kDefBend[8] = { 0.45f, 0.15f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.1f };
const float kDefTone[8] = { 0.4f, 0.5f, 0.5f, 0.4f, 0.6f, 0.6f, 0.5f, 0.5f };
const float kDefClick[8] = { 0.4f, 0.6f, 0.5f, 0.2f, 0.3f, 0.3f, 0.3f, 0.3f };
const float kDefPan[8] = { 0, 0.05f, -0.1f, -0.25f, 0.2f, 0.2f, -0.3f, 0.3f };
const float kDefBendTime[8] = { 0.012f, 0.004f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f };
} // namespace

DrumsDevice::DrumsDevice() : DeviceProcessor (DeviceType::Drums)
{
    auto& L = layout;
    for (int p = 0; p < kParts; ++p)
    {
        const juce::String id = "p" + juce::String (p + 1);
        juce::StringArray engs;
        for (auto e : drums::enginesForPart (p)) engs.add (drums::engineName (e));
        L.choice (id + "Eng", "Engine", engs, 0);
        L.cont (id + "Pitch", "Pitch", 20, 12000, kDefPitch[p], Unit::Hz, true);
        L.cont (id + "Bend", "Bend", 0, 1, kDefBend[p], Unit::Percent);
        L.cont (id + "BendTime", "Bend Time", 0.002f, 0.6f, kDefBendTime[p], Unit::Seconds, true);
        L.cont (id + "Decay", "Decay", 0.01f, 4, kDefDecay[p], Unit::Seconds, true);
        L.cont (id + "Tone", "Tone", 0, 1, kDefTone[p], Unit::Percent);
        L.cont (id + "Click", "Click", 0, 1, kDefClick[p], Unit::Percent);
        L.cont (id + "Accent", "Accent", 0, 1, 0.5f, Unit::Percent);
        L.cont (id + "Drive", "Drive", 0, 1, 0, Unit::Percent);
        L.choice (id + "DriveType", "Drive Type", { "Soft", "Tube", "Fold" }, 0);
        L.cont (id + "Level", "Level", 0, 1, 0.8f, Unit::Percent);
        L.cont (id + "Pan", "Pan", -1, 1, kDefPan[p], Unit::Pan, false, true);
        L.cont (id + "SendDelay", "Delay", 0, 1, 0, Unit::Percent);
        L.cont (id + "SendReverb", "Reverb", 0, 1, 0, Unit::Percent);
        L.cont (id + "VelSens", "Vel Sens", 0, 1, 0.6f, Unit::Percent);
        L.integer (id + "Choke", "Choke", 0, 2, (p == 4 || p == 5) ? 1 : 0);
        L.cont (id + "ResSend", "Send", 0, 1, 0, Unit::Percent);
        L.choice (id + "LayA", "Source A", { "Sine", "Saw", "HP Noise", "LP Noise", "BP Noise" }, p >= 4 ? 2 : 0);
        L.choice (id + "LayB", "Source B", { "Sine", "Saw", "HP Noise", "LP Noise", "BP Noise" }, p == 0 ? 3 : 4);
        L.choice (id + "LayMod", "Mod", { "Decay", "Rise", "LFO" }, 0);
        L.choice (id + "LayEnv", "Envelope", { "AD", "Exp", "Multi" }, 1);
        L.cont (id + "LayMix", "Mix", 0, 1, 0.3f, Unit::Percent);
        L.cont (id + "LayRatio", "B Pitch", 0.25f, 8, 2, Unit::Ratio, true);
    }
    kitBase = L.size();
    pResType = L.choice ("resType", "Resonator", { "Tube", "String" }, 0);
    pResTune = L.cont ("resTune", "Tune", 40, 800, 110, Unit::Hz, true);
    pResDecay = L.cont ("resDecay", "Decay", 0, 1, 0.5f, Unit::Percent);
    pResDamp = L.cont ("resDamp", "Damp", 0, 1, 0.4f, Unit::Percent);
    pResMix = L.cont ("resMix", "Mix", 0, 1, 0, Unit::Percent);
    pComp = L.cont ("kitComp", "Comp", 0, 1, 0, Unit::Percent);
    pKitDrive = L.cont ("kitDrive", "Drive", 0, 1, 0, Unit::Percent);
    pWidth = L.cont ("kitWidth", "Width", 0, 1, 0.6f, Unit::Percent);
    pKitLevel = L.cont ("kitLevel", "Level", 0, 2, 0.8f, Unit::Percent);
    pRevMix = L.cont ("kitReverb", "Mix", 0, 1, 0.6f, Unit::Percent);   // the reverb card's return; parts set their sends
    pRevSize = L.size();
    addReverbParams (L);
    finishLayout();

    macro (0, "Kick Tune", { { "p1Pitch", 0.05f, 0.22f } });
    macro (1, "Kick Decay", { { "p1Decay", 0.2f, 0.75f } });
    macro (2, "Snare Tone", { { "p2Tone", 0.0f, 1.0f } });
    macro (3, "Snappy", { { "p2Click", 0.0f, 1.0f } });
    macro (4, "Hat Decay", { { "p5Decay", 0.05f, 0.5f }, { "p6Decay", 0.3f, 0.85f } });
    macro (5, "Metal", { { "p5Tone", 0.0f, 1.0f }, { "p6Tone", 0.0f, 1.0f }, { "p7Tone", 0.0f, 1.0f } });
    macro (6, "Resonator", { { "resMix", 0.0f, 1.0f } });
    macro (7, "Drive", { { "kitDrive", 0.0f, 1.0f } });
}

juce::String DrumsDevice::laneName (int lane) const { return kPartNames[std::clamp (lane, 0, 7)]; }
int DrumsDevice::laneNote (int lane) const { return kPartNotes[std::clamp (lane, 0, 7)]; }

int DrumsDevice::partForNote (int note) const
{
    for (int p = 0; p < kParts; ++p) if (kPartNotes[p] == note) return p;
    if (const int r = gmDrumRole (note); r >= 0) return r;              // parts are already in role order
    return ((note % 12) + 12) % 12 < 8 ? ((note % 12) + 12) % 12 : -1;   // C..G of any other octave map to parts 1–8
}

drums::Engine DrumsDevice::partEngine (int part) const
{
    const auto& e = drums::enginesForPart (part);
    return e[(size_t) std::clamp ((int) get (partParam (part, Eng)), 0, (int) e.size() - 1)];
}

namespace
{
enum Model { K808, K909, KBox, KRing, MLayer, S808, S909, SBox, Clap808, Clap909, Pcm, TomPing, Tom909, TomBox,
             Metal808, HatBox, PercPing, Rim909, Cowbell };

Model modelFor (int part, drums::Engine e)
{
    using namespace drums;
    if (e == Layer) return MLayer;
    switch (part)
    {
        case 0: return e == Classic ? K808 : e == Punch ? K909 : e == Box ? KBox : KRing;
        case 1: return e == Classic ? S808 : e == Punch ? S909 : SBox;
        case 2: return e == Classic ? Clap808 : e == Punch ? Clap909 : Pcm;
        case 3: return e == Classic ? TomPing : e == Punch ? Tom909 : TomBox;
        case 6: return e == Classic ? Metal808 : Pcm;
        case 7: return e == Classic ? PercPing : e == Bell ? Cowbell : e == Punch ? Rim909 : Pcm;
        default: return e == Classic ? Metal808 : e == Punch ? Pcm : HatBox;
    }
}

/** Ringing circuits: a new hit excites the same state instead of fading the old one out. */
bool rings (int m) { return m == K808 || m == S808 || m == TomPing || m == PercPing || m == Rim909 || m == KBox || m == SBox || m == TomBox; }

void lowpassInPlace (std::vector<float>& b, float hz, float sr)
{
    dsp::drum::Tpt1 a, c;
    for (auto& v : b) v = c.lp (a.lp (v, hz, sr), hz, sr);
}
} // namespace

void DrumsDevice::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    const float r = (float) sr;
    for (auto* set : { &voices, &tails })
        for (int p = 0; p < kParts; ++p)
        {
            auto& v = (*set)[(size_t) p];
            v.k808.prepare (r); v.k909.prepare (r); v.kres.prepare (r); v.s808.prepare (r); v.s909.prepare (r);
            v.clap.prepare (r); v.t909.prepare (r); v.ping.prepare (r); v.bell.prepare (r); v.metal.prepare (r, rng);
            v.metal.cymbal = p == 6;
            v.lay.prepare (r);
            v.bkick.prepare (r); v.bsnare.prepare (r); v.btom.prepare (r); v.bhat.prepare (r, rng); v.pcm.prepare (r);
            v.drive.prepare (r);
            v.hpOut.setCutoff (18.0f, r);
        }
    // stored recordings: built here, once, never on the audio thread
    {
        dsp::drum::Clap clap; clap.prepare (r);
        dsp::drum::Ctl c; c.pitch = 1150.0f; c.decay = 0.3f; c.tone = 0.5f; c.click = 0.5f;
        clap.trigger (c);
        pcmClap.assign ((size_t) (0.6f * r), 0.0f);
        for (auto& v : pcmClap) v = clap.process (c);
        lowpassInPlace (pcmClap, 9000.0f, r);
        dsp::drum::pcm::quantise (pcmClap, 10);
    }
    pcmHat = dsp::drum::pcm::metal (r, 1.2f, 3500.0f, 15000.0f, 90, 0.05f, 0.9f, 0.25f, 0x909u);
    lowpassInPlace (pcmHat, 14000.0f, r);
    dsp::drum::pcm::quantise (pcmHat, 6);   // the 1983 machine stored its metal at 6 bits
    pcmRide = dsp::drum::pcm::metal (r, 3.0f, 2500.0f, 14000.0f, 120, 0.4f, 3.0f, 0.15f, 0x1983u);
    lowpassInPlace (pcmRide, 13000.0f, r);
    dsp::drum::pcm::quantise (pcmRide, 6);
    pcmCrash = dsp::drum::pcm::metal (r, 2.5f, 1500.0f, 12000.0f, 100, 0.3f, 2.5f, 0.35f, 0x2013u);
    lowpassInPlace (pcmCrash, 10000.0f, r);
    dsp::drum::pcm::quantise (pcmCrash, 10);
    {
        pcmAgogo.assign ((size_t) (0.5f * r), 0.0f);
        const float f = 880.0f, ratio[3] = { 1.0f, 2.41f, 4.17f }, t60[3] = { 0.35f, 0.15f, 0.08f }, amp[3] = { 1.0f, 0.45f, 0.25f };
        for (int k = 0; k < 3; ++k)
        {
            const float dec = dsp::drum::t60Coef (t60[k], r);
            float g = amp[k];
            for (size_t i = 0; i < pcmAgogo.size(); ++i) { pcmAgogo[i] += std::sin (kTwoPi * f * ratio[k] * (float) i / r) * g; g *= dec; }
        }
        lowpassInPlace (pcmAgogo, 9000.0f, r);
        dsp::drum::pcm::quantise (pcmAgogo, 10);
    }
    resonator.prepare (r);
    comp.prepare (r);
    kitDrive.prepare (r);
    kitDrive.setTone (0.75f);
    bufL.assign ((size_t) maxBlock, 0.0f); bufR.assign ((size_t) maxBlock, 0.0f);
    levelSm.setTime (15.0f, r); levelSm.reset (get (pKitLevel));
    sendL.assign ((size_t) maxBlock, 0.0f); sendR.assign ((size_t) maxBlock, 0.0f);
    verb.prepare (r);
}

void DrumsDevice::reset()
{
    for (auto* set : { &voices, &tails })
        for (auto& v : *set)
        {
            v.active = false;
            v.k808.reset(); v.k909.reset(); v.kres.reset(); v.s808.reset(); v.s909.reset(); v.clap.reset();
            v.t909.reset(); v.ping.reset(); v.bell.reset(); v.metal.reset(); v.lay.reset();
            v.bkick.reset(); v.bsnare.reset(); v.btom.reset(); v.bhat.reset(); v.pcm.reset();
        }
    resonator.reset();
}

bool DrumsDevice::anyVoiceActive() const
{
    for (int p = 0; p < kParts; ++p) if (voices[(size_t) p].active || tails[(size_t) p].active) return true;
    return false;
}

dsp::drum::Ctl DrumsDevice::ctlFor (int part, const Voice& v) const
{
    const float* P = snap[part].p;
    dsp::drum::Ctl c;
    c.pitch = P[Pitch]; c.bend = P[Bend]; c.bendTime = P[BendTime]; c.decay = P[Decay]; c.tone = P[Tone]; c.click = P[Click];
    c.accent = 0.0f;
    c.layA = (int) P[LayA]; c.layB = (int) P[LayB]; c.layMod = (int) P[LayMod]; c.layEnv = (int) P[LayEnv];
    c.layMix = P[LayMix]; c.layRatio = P[LayRatio];
    juce::ignoreUnused (v);
    return c;
}

void DrumsDevice::trigger (int part, float vel, bool accent)
{
    const int group = (int) pv (partParam (part, Choke));   // a step lock counts from the trigger on
    if (group > 0)
        for (int p = 0; p < kParts; ++p)
            if (p != part && (int) pv (partParam (p, Choke)) == group)
                for (auto* v : { &voices[(size_t) p], &tails[(size_t) p] })
                    if (v->active) v->fadeStep = 1.0f / (0.006f * (float) sampleRate);   // choked: 6 ms fade
    auto& v = voices[(size_t) part];
    const auto eng = partEngine (part);
    const int model = modelFor (part, eng);
    if (v.active && ! (rings (model) && v.model == model))
    {
        tails[(size_t) part] = v;   // the previous hit fades out under the new one
        tails[(size_t) part].fadeStep = 1.0f / (0.004f * (float) sampleRate);
    }
    v.active = true; v.eng = eng; v.model = model; v.vel = vel; v.gain = 1.0f; v.fadeStep = 0.0f;
    for (int f = 0; f < kFields; ++f) snap[part].p[f] = pv (partParam (part, (Field) f));
    auto c = ctlFor (part, v);
    c.accent = accent ? pv (partParam (part, Accent)) : 0.0f;
    switch (model)
    {
        case K808: v.k808.trigger (c); break;
        case K909: v.k909.trigger (c); break;
        case KBox: v.bkick.trigger (c); break;
        case KRing: v.kres.trigger (c); break;
        case MLayer: v.lay.trigger (c); break;
        case S808: v.s808.trigger (c); break;
        case S909: v.s909.trigger (c); break;
        case SBox: v.bsnare.trigger (c); break;
        case Clap808: v.clap.digital = false; v.clap.trigger (c); break;
        case Clap909: v.clap.digital = true; v.clap.trigger (c); break;
        case TomPing: case PercPing: case Rim909: v.ping.trigger (c); break;
        case Tom909: v.t909.trigger (c); break;
        case TomBox: v.btom.trigger (c); break;
        case Metal808: v.metal.trigger (c); break;
        case HatBox: v.bhat.trigger (c); break;
        case Cowbell: v.bell.trigger (c); break;
        case Pcm:
            v.pcm.buf = part == 2 ? &pcmClap : part == 6 ? (eng == drums::Punch ? &pcmRide : &pcmCrash) : part == 7 ? &pcmAgogo : &pcmHat;
            v.pcm.trigger (c);
            break;
        default: break;
    }
    hits[(size_t) part].fetch_add (1);
}

void DrumsDevice::handleEvent (const NoteEvent& e)
{
    if (e.type == NoteEvent::NoteOn)
    {
        const int p = partForNote (e.note);
        if (p >= 0) trigger (p, e.velocity, (e.flags & kAccent) != 0 || e.velocity > 0.94f);
        markNote (e.note, true);
    }
    else if (e.type == NoteEvent::NoteOff) markNote (e.note, false);
    else if (e.type == NoteEvent::Panic) reset();
    else if (e.type == NoteEvent::AllNotesOff) for (auto& w : heldNotes) w.store (0);
}

float DrumsDevice::processPart (int part, Voice& v, const dsp::drum::Ctl& c)
{
    const float* P = snap[part].p;
    const float secs = 1.0f / (float) sampleRate;
    float out = 0.0f;
    bool done = false;
    switch (v.model)
    {
        case K808: out = v.k808.process (c); done = v.k808.done(); break;
        case K909: out = v.k909.process (c); done = (float) v.k909.t * secs > c.decay * 1.4f + 0.12f; break;
        case KBox: out = v.bkick.process (c); done = v.bkick.done(); break;
        case KRing: out = v.kres.process (c); done = v.kres.done(); break;
        case MLayer: out = v.lay.process (c); done = v.lay.done(); break;
        case S808: out = v.s808.process (c); done = v.s808.done(); break;
        case S909: out = v.s909.process (c); done = v.s909.done(); break;
        case SBox: out = v.bsnare.process (c); done = v.bsnare.done(); break;
        case Clap808: case Clap909: out = v.clap.process (c); done = (float) v.clap.t * secs > c.decay * 2.0f + 0.1f; break;
        case TomPing: out = v.ping.process (c, 1.59f); done = v.ping.done(); break;
        case PercPing: out = v.ping.process (c, 3.6f); done = v.ping.done(); break;
        case Rim909: out = v.ping.process (c, 2.63f); done = v.ping.done(); break;
        case Tom909: out = v.t909.process (c); done = (float) v.t909.t * secs > c.decay * 1.6f + 0.05f; break;
        case TomBox: out = v.btom.process (c); done = v.btom.done(); break;
        case Metal808: out = v.metal.process (c); done = v.metal.done(); break;
        case HatBox: out = v.bhat.process (c); done = v.bhat.done(); break;
        case Cowbell: out = v.bell.process (c); done = v.bell.done(); break;
        case Pcm: out = v.pcm.process (c, kDefPitch[part]); done = v.pcm.done(); break;
        default: done = true; break;
    }
    // makeup per model, measured at default settings (tool_drum_lab): a part keeps its level when its
    // engine changes (drums and toms peak ≈ 0.5, metal ≈ 0.35)
    static constexpr float kMakeup[] = { /*K808*/ 1.4f, /*K909*/ 1.26f, /*KBox*/ 3.6f, /*KRing*/ 0.84f, /*MLayer*/ 1.6f,
                                         /*S808*/ 1.05f, /*S909*/ 1.32f, /*SBox*/ 1.5f, /*Clap808*/ 1.28f, /*Clap909*/ 0.9f,
                                         /*Pcm*/ 1.0f, /*TomPing*/ 1.45f, /*Tom909*/ 1.4f, /*TomBox*/ 2.65f, /*Metal808*/ 2.4f,
                                         /*HatBox*/ 0.85f, /*PercPing*/ 2.7f, /*Rim909*/ 4.0f, /*Cowbell*/ 1.15f };
    out *= kMakeup[v.model];
    if (v.model == Pcm) out *= part == 2 ? 2.4f : part == 7 ? 2.1f : part == 6 ? 1.1f : 1.5f;
    if (P[Drive] > 0.001f) out = v.drive.process (out, (int) P[DriveType], P[Drive], 1.0f);
    out = v.hpOut.hp (out);
    if (v.fadeStep > 0.0f) { v.gain -= v.fadeStep; if (v.gain <= 0.0f) { v.gain = 0.0f; done = true; } }
    if (done) v.active = false;
    return out * v.gain;
}

void DrumsDevice::render (float* outL, float* outR, int n)
{
    for (int p = 0; p < kParts; ++p)
        for (int f = 0; f < kFields; ++f) snap[p].p[f] = pv (partParam (p, (Field) f));
    std::array<dsp::drum::Ctl, kParts> ctl;
    for (int p = 0; p < kParts; ++p) ctl[(size_t) p] = ctlFor (p, voices[(size_t) p]);
    const float width = pv (pWidth);
    const int resType = (int) pv (pResType);
    const float resHz = pv (pResTune), resDecay = pv (pResDecay), resDamp = pv (pResDamp), resMix = pv (pResMix);
    const bool resOn = resMix > 0.001f;
    if (resOn && ! resWasOn) resonator.reset();   // back on: from silence, not what rang when it stopped
    resWasOn = resOn;
    const float compAmt = pv (pComp), kDrive = pv (pKitDrive), lvl = pv (pKitLevel);
    std::fill (bufL.begin(), bufL.begin() + n, 0.0f);
    std::fill (bufR.begin(), bufR.begin() + n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        float l = 0.0f, r = 0.0f, resIn = 0.0f, dl = 0.0f, dr = 0.0f, rl = 0.0f, rr = 0.0f;
        for (int p = 0; p < kParts; ++p)
        {
            auto& v = voices[(size_t) p];
            auto& tv = tails[(size_t) p];
            if (! v.active && ! tv.active) continue;
            const float* P = snap[p].p;
            const auto c = ctl[(size_t) p];
            float raw = 0.0f;
            if (v.active) raw += processPart (p, v, c) * (1.0f - P[VelSens] * (1.0f - v.vel));
            if (tv.active) raw += processPart (p, tv, c) * (1.0f - P[VelSens] * (1.0f - tv.vel));
            const float s = raw * P[Level];
            const float pan = clamp (P[Pan] * width * 1.6f, -1.0f, 1.0f);
            const float gl = std::sqrt (0.5f * (1.0f - pan)), gr = std::sqrt (0.5f * (1.0f + pan));
            l += s * gl; r += s * gr;
            resIn += s * P[ResSend];
            dl += s * gl * P[SendDelay]; dr += s * gr * P[SendDelay];
            rl += s * gl * P[SendReverb]; rr += s * gr * P[SendReverb];
        }
        if (resOn)
        {
            const float res = resonator.process (resIn * 0.5f, resHz, resDecay, resDamp, resType) * resMix * 1.5f;
            l += res; r += res;
        }
        if (kDrive > 0.001f) kitDrive.process (l, r, 1, kDrive, 1.0f);
        comp.process (l, r, compAmt);
        const float g = levelSm.tick (lvl);
        bufL[(size_t) i] = l * g; bufR[(size_t) i] = r * g;
        sendTo (0, i, dl * g, dr * g);
        sendL[(size_t) i] = rl * g; sendR[(size_t) i] = rr * g;
    }
    reverbCard (verb, sendL.data(), sendR.data(), bufL.data(), bufR.data(), n, pv (pRevMix) * 1.4f * reverbScale, pv (pRevSize), pv (pRevSize + 1), pv (pRevSize + 2));
    for (int i = 0; i < n; ++i) { outL[i] += bufL[(size_t) i]; outR[i] += bufR[(size_t) i]; }
    trackSilence (bufL.data(), bufR.data(), n);
}
} // namespace mb
