#include "PolyDevice.h"

namespace mb
{
using namespace dsp;

PolyDevice::PolyDevice() : DeviceProcessor (DeviceType::Poly)
{
    auto& L = layout;
    L.choice ("engine", "Engine", { "DCO", "VCO" }, 0);   // the VCO engine is the paraphonic 3-oscillator voice
    L.choice ("dcoWave", "Wave", { "Pulse", "Saw", "Both" }, 1);
    L.cont ("dcoPw", "PW", 0.05f, 0.95f, 0.5f, Unit::Percent);
    L.cont ("dcoPwm", "PWM", 0, 1, 0, Unit::Percent);
    L.choice ("vaWave", "Wave", { "Saw", "Pulse", "Tri" }, 0);
    L.integer ("vaOctave", "Octave", 0, 3, 1, Unit::Footage);
    L.cont ("vaPw", "PW", 0.05f, 0.95f, 0.5f, Unit::Percent);
    L.cont ("vaPwm", "PWM", 0, 1, 0, Unit::Percent);
    L.cont ("vaFine", "Fine", -50, 50, 0, Unit::Cents, false, true);
    L.cont ("sub", "Sub", 0, 1, 0, Unit::Percent);
    L.cont ("noise", "Noise", 0, 1, 0, Unit::Percent);
    L.cont ("drift", "Drift", 0, 1, 0, Unit::Percent);
    L.integer ("hpf", "HPF", 0, 3, 1);
    L.cont ("cutoff", "Cutoff", 30, 18000, 18000, Unit::Hz, true);
    L.cont ("reso", "Reso", 0, 1, 0, Unit::Percent);
    L.cont ("envAmt", "Env", -1, 1, 0, Unit::Plain, false, true);
    L.cont ("key", "Key", 0, 1, 0.5f, Unit::Percent);
    L.cont ("filterVel", "Velocity", 0, 1, 0, Unit::Percent);
    L.choice ("filterMode", "Filter", { "Shared", "Per voice" }, 1);
    L.cont ("filterDrive", "Drive", 0, 1, 0, Unit::Percent);
    L.cont ("fAttack", "Attack", 0.001f, 8, 0.003f, Unit::Seconds, true);
    L.cont ("fDecay", "Decay", 0.005f, 10, 0.5f, Unit::Seconds, true);
    L.cont ("fSustain", "Sustain", 0, 1, 1, Unit::Percent);
    L.cont ("fRelease", "Release", 0.005f, 10, 0.2f, Unit::Seconds, true);
    L.cont ("aAttack", "Attack", 0.001f, 8, 0.003f, Unit::Seconds, true);
    L.cont ("aDecay", "Decay", 0.005f, 10, 0.5f, Unit::Seconds, true);
    L.cont ("aSustain", "Sustain", 0, 1, 1, Unit::Percent);
    L.cont ("aRelease", "Release", 0.005f, 10, 0.2f, Unit::Seconds, true);
    L.choice ("ampSrc", "Amp", { "ADSR", "Filter Env", "Gate" }, 0);
    L.cont ("level", "Level", 0, 2, 0.7f, Unit::Percent);
    L.cont ("velocity", "Velocity", 0, 1, 0.3f, Unit::Percent);
    L.choice ("lfoShape", "LFO Shape", { "Tri", "Saw", "Sq", "S&H" }, 0);
    L.choice ("lfoSync", "Sync", { "Free", "Sync" }, 0);
    L.cont ("lfoRate", "Rate", 0.05f, 30, 3, Unit::Hz, true);
    L.integer ("lfoDiv", "Rate", 0, kNumDivisions - 1, 7, Unit::Division);
    L.cont ("lfoDelay", "Delay", 0, 4, 0, Unit::Seconds);
    L.cont ("lfoPitch", "Pitch", 0, 1, 0, Unit::Percent);
    L.cont ("lfoPwm", "PWM", 0, 1, 0, Unit::Percent);
    L.cont ("lfoCut", "Cutoff", 0, 1, 0, Unit::Percent);
    L.choice ("voiceMode", "Voice", { "Poly", "Unison", "Chord", "Octave", "Fifth", "Ring" }, 0);
    L.cont ("detune", "Detune", 0, 1, 0, Unit::Percent);
    L.cont ("glide", "Glide", 0, 1, 0, Unit::Seconds);
    L.choice ("chord", "Chord", chordShapeNames(), 0);
    L.choice ("arpMode", "Arp", { "Off", "Up", "Down", "Up-Dn", "Rand" }, 0);
    L.integer ("arpRange", "Range", 1, 3, 1);
    L.integer ("arpRate", "Rate", 0, kNumDivisions - 1, 10, Unit::Division);
    L.cont ("arpGate", "Gate", 0.1f, 1, 0.6f, Unit::Percent);
    L.choice ("driveType", "Drive", { "Soft", "Tube", "Fold" }, 0);
    L.cont ("driveAmt", "Amount", 0, 1, 0, Unit::Percent);
    L.cont ("driveTone", "Tone", 0, 1, 0.7f, Unit::Percent);
    L.choice ("chorusMode", "Chorus", { "Off", "I", "II", "I+II" }, 0);
    L.cont ("chorusMix", "Mix", 0, 1, 0.5f, Unit::Percent);
    L.choice ("delaySync", "Delay", { "Sync", "Free" }, 0);
    L.integer ("delayDiv", "Time", 0, kNumDivisions - 1, 7, Unit::Division);
    L.cont ("delayFree", "Time", 0.02f, 2, 0.35f, Unit::Seconds, true);
    L.cont ("delayFb", "Feedback", 0, 0.92f, 0.35f, Unit::Percent);
    L.cont ("delayTone", "Tone", 0, 1, 0.5f, Unit::Percent);
    L.cont ("delayMix", "Mix", 0, 1, 0, Unit::Percent);
    L.cont ("reverb", "Reverb", 0, 1, 0, Unit::Percent);
    addReverbParams (L);
    jassert (layout.size() == numParams);
    finishLayout();

    // Default macros follow the DCO engine; VA presets carry their own map (see PresetBank).
    macro (0, "Cutoff", { { "cutoff", 0.15f, 1.0f } });
    macro (1, "Reso", { { "reso", 0.0f, 0.9f } });
    macro (2, "Env", { { "envAmt", 0.5f, 1.0f } });
    macro (3, "PWM", { { "dcoPwm", 0.0f, 1.0f }, { "vaPwm", 0.0f, 1.0f } });
    macro (4, "Sub", { { "sub", 0.0f, 1.0f } });
    macro (5, "Attack", { { "aAttack", 0.0f, 0.75f }, { "fAttack", 0.0f, 0.75f } });
    macro (6, "Release", { { "aRelease", 0.1f, 0.85f }, { "fRelease", 0.1f, 0.85f } });
    macro (7, "Chorus", { { "chorusMode", 0.0f, 1.0f } });
}

void PolyDevice::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    const float r = (float) sr;
    for (auto& v : voices)
    {
        v.fenv.prepare (r); v.aenv.prepare (r);
        v.rng.seed (rng.next());
        for (auto& o : v.osc) o.randomise (rng);
        for (auto& d : v.dco) { d.prepare (2.0f * r); d.randomise (rng); }
        v.noiseGen.rng.seed (rng.next());
        v.noiseLp.setCutoff (8000.0f, 2.0f * r);   // the oscillator mixer runs at 2×
        v.ringDc.prepare (2.0f * r);
        v.subOsc.prepare (2.0f * r);
        v.tol.roll (rng, true);   // each voice card has its own tolerances
    }
    sharedEnv.prepare (r);
    lfo.prepare (r);
    drive.prepare (r);
    chorus.prepare (r);
    delay.prepare (r);
    verb.prepare (r);
    bufL.assign ((size_t) maxBlock, 0.0f); bufR.assign ((size_t) maxBlock, 0.0f); lfoBuf.assign ((size_t) maxBlock, 0.0f);
    levelSm.setTime (15.0f, r); levelSm.reset (get (level) * kLevelScale);
}

void PolyDevice::reset()
{
    for (auto& v : voices) { v.fenv.kill(); v.aenv.kill(); v.cascade.reset(); v.peak.reset(); v.os.reset(); v.slot = {}; }
    sharedEnv.kill(); sharedFilter.reset(); sharedOs.reset();
    held.clear(); arp.clear(); arp.sounding = -1;
    chorus.reset(); delay.reset(); verb.reset();
}

bool PolyDevice::anyVoiceActive() const
{
    for (auto& v : voices) if (v.aenv.active() || v.fenv.active()) return true;
    return false;
}

void PolyDevice::voiceOn (int note, float vel, int sourceKey)
{
    const int mode = (int) get (voiceMode);
    const float glideAmt = get (glide);
    auto start = [&] (int idx, int n, float stack, float pan)
    {
        auto& v = voices[(size_t) idx];
        const bool wasActive = v.aenv.active();
        v.target = (float) n;
        v.pitch = glideAmt > 0.001f ? lastPitch + stack : (float) n;
        v.stack = stack; v.pan = pan; v.vel = vel;
        if (! wasActive) { v.fadeIn = 0.0f; v.velGain = 1.0f - get (velocity) + get (velocity) * vel; }
        v.fenv.gateOn(); v.aenv.gateOn();
        v.slot.note = n; v.slot.held = true; v.slot.active = true; v.slot.age = ++clock;
        voiceKey[(size_t) idx] = sourceKey;
    };
    if (mode == 1)   // Unison: all six voices on one note, spread across the stereo field
    {
        for (int i = 0; i < kVoices; ++i)
            start (i, note, ((float) i - 2.5f) / 2.5f, ((float) i - 2.5f) / 2.5f * 0.8f);
    }
    else if (mode == 2)   // Chord memory
    {
        int cnt = 0;
        const auto shape = chordShape ((int) get (chord), cnt);
        std::array<VoiceSlot, kVoices> slots;
        for (size_t i = 0; i < kVoices; ++i) slots[i] = voices[i].slot;
        for (int k = 0; k < cnt; ++k)
        {
            const int idx = allocateVoice (slots, note + shape[(size_t) k], voiceCap (kVoices), clock);
            slots[(size_t) idx].active = true; slots[(size_t) idx].note = note + shape[(size_t) k]; slots[(size_t) idx].held = true;
            slots[(size_t) idx].age = UINT32_MAX;
            start (idx, note + shape[(size_t) k], 0.0f, ((float) k - 1.5f) * 0.25f);
        }
    }
    else
    {
        std::array<VoiceSlot, kVoices> slots;
        for (size_t i = 0; i < kVoices; ++i) slots[i] = voices[i].slot;
        const int idx = allocateVoice (slots, note, voiceCap (kVoices), clock);
        start (idx, note, 0.0f, ((float) (idx % 3) - 1.0f) * 0.2f);
    }
    lastPitch = (float) note;
    sharedEnv.gateOn();
    if (lfo.fadeInc < 1.0f) lfo.retrigger();
}

void PolyDevice::voiceOff (int sourceKey)
{
    for (int i = 0; i < kVoices; ++i)
        if (voiceKey[(size_t) i] == sourceKey && voices[(size_t) i].slot.held)
        {
            voices[(size_t) i].fenv.gateOff(); voices[(size_t) i].aenv.gateOff();
            voices[(size_t) i].slot.held = false;
        }
    bool any = false;
    for (auto& v : voices) any |= v.slot.held;
    if (! any) sharedEnv.gateOff();
}

void PolyDevice::keyOn (int note, float vel)
{
    markNote (note, true);
    held.push (note, vel);
    if (arpModeNow() != Arpeggiator::Off) { arp.addKey (note); return; }
    voiceOn (note, vel, note);
}

void PolyDevice::keyOff (int note)
{
    markNote (note, false);
    held.remove (note);
    arp.removeKey (note);
    if (arpModeNow() == Arpeggiator::Off) voiceOff (note);
}

void PolyDevice::handleEvent (const NoteEvent& e)
{
    switch (e.type)
    {
        case NoteEvent::NoteOn: keyOn (e.note, e.velocity); break;
        case NoteEvent::NoteOff: keyOff (e.note); break;
        case NoteEvent::AllNotesOff:
            held.clear(); arp.clear();
            if (arp.sounding >= 0) { voiceOff (arp.sounding + 1000); arp.sounding = -1; }
            for (int i = 0; i < kVoices; ++i) voiceOff (voiceKey[(size_t) i]);
            for (auto& w : heldNotes) w.store (0);
            break;
        case NoteEvent::Panic: reset(); break;
    }
}

void PolyDevice::render (float* outL, float* outR, int n)
{
    exprNow = expressionSemis (n);   // pitch bend and mod-wheel vibrato (renderChunk adds it)
    std::fill (bufL.begin(), bufL.begin() + n, 0.0f);
    std::fill (bufR.begin(), bufR.begin() + n, 0.0f);

    // LFO for the block
    const bool sync = pv (lfoSync) > 0.5f;
    static constexpr int shapeMap[4] = { Lfo::Tri, Lfo::SawDown, Lfo::Square, Lfo::SampleHold };
    const int shape = shapeMap[std::clamp ((int) pv (lfoShape), 0, 3)];
    const double bpm = std::max (20.0, transport.bpm);
    if (sync)
    {
        const double beats = divisionBeats (pv (lfoDiv));
        lfo.setRate ((float) (bpm / 60.0 / beats));
        if (transport.playing) lfo.syncTo (transport.beatPos, beats);
    }
    else lfo.setRate (pv (lfoRate));
    lfo.setDelay (pv (lfoDelay));
    for (int i = 0; i < n; ++i) lfoBuf[(size_t) i] = lfo.process (shape);

    // Arpeggiator events split the block so notes land sample-accurately
    const int amode = outerArp ? (int) Arpeggiator::Off : (int) pv (arpMode);
    if (amode == Arpeggiator::Off && arp.sounding >= 0) { voiceOff (arp.sounding + 1000); arp.sounding = -1; }
    if (amode != Arpeggiator::Off && arp.numKeys > 0)
    {
        const double stepBeats = divisionBeats (pv (arpRate));
        const double stepSamples = stepBeats * 60.0 / bpm * sampleRate;
        if (transport.playing)
        {
            // locked to the transport: steps count in transport beats from the
            // phrase's first key, so they survive tempo changes and dotted
            // rates start where the phrase does (a stopped transport free-runs on)
            if (arp.fresh || ! arp.wasGrid || std::abs (transport.beatPos - arp.expectBeat) > 1.0e-3)
            {
                // first key, transport start, or a loop / seek jump: step 0 is now
                if (! arp.fresh) arp.step = 0;
                arp.anchor = transport.beatPos;
                arp.slot = -1;
                arp.slotRate = pv (arpRate);
            }
            const double x = (transport.beatPos - arp.anchor) / stepBeats + 1.0e-7;
            const long long s = (long long) std::floor (x);
            if (arp.slotRate != pv (arpRate))
            {
                // rate changed (slots renumbered): rejoin without an extra step
                arp.slot = x - (double) s < 1.0e-4 ? s - 1 : s;
                arp.slotRate = pv (arpRate);
            }
            arp.samplesToNext = ((double) (arp.slot + 1) - x) * stepSamples;
            arp.expectBeat = transport.beatPos + n * bpm / 60.0 / sampleRate;
        }
        arp.wasGrid = transport.playing;
        arp.fresh = false;
        int pos = 0;
        while (pos < n)
        {
            int next = n;
            if (arp.samplesToOff >= 0.0) next = std::min (next, pos + (int) std::ceil (arp.samplesToOff));
            next = std::min (next, pos + (int) std::ceil (arp.samplesToNext));
            next = std::max (next, pos);
            if (next > pos) renderChunk (bufL.data(), bufR.data(), pos, next - pos);
            const double adv = next - pos;
            arp.samplesToNext -= adv;
            // armed BEFORE the decrement: the chunk ends on ceil(samplesToOff), so a
            // fractional gate lands just below 0 (it used to read as "no gate" and
            // every arp note held until the next step)
            const bool offArmed = arp.samplesToOff >= 0.0;
            if (offArmed) arp.samplesToOff -= adv;
            if (offArmed && arp.samplesToOff <= 0.5)
            {
                if (arp.sounding >= 0) voiceOff (arp.sounding + 1000);
                arp.sounding = -1; arp.samplesToOff = -1.0;
            }
            if (arp.samplesToNext <= 0.5)
            {
                if (arp.sounding >= 0) voiceOff (arp.sounding + 1000);
                const int note = arp.noteForStep (amode, (int) pv (arpRange));
                if (note >= 0)
                {
                    voiceOn (note, held.count > 0 ? held.topVel() : 0.8f, note + 1000);
                    arp.sounding = note;
                    arp.samplesToOff = stepSamples * pv (arpGate);
                }
                ++arp.step;
                ++arp.slot;
                arp.samplesToNext += stepSamples;
            }
            pos = next;
            if (next == n) break;
        }
    }
    else
    {
        if (arp.numKeys == 0 && arp.sounding >= 0) { voiceOff (arp.sounding + 1000); arp.sounding = -1; }
        renderChunk (bufL.data(), bufR.data(), 0, n);
    }

    // FX: drive → chorus → delay
    const int dType = (int) pv (driveType);
    const float dAmt = pv (driveAmt);
    drive.setTone (pv (driveTone));
    const int cMode = (int) pv (chorusMode);
    const float cMix = pv (chorusMix);
    const float dMix = pv (delayMix), fb = pv (delayFb);
    const bool delayOn = dMix > 0.001f;
    if (delayOn && ! delayWasOn) delay.reset();   // back on: from silence, not the echoes frozen when it stopped
    delayWasOn = delayOn;
    delay.setTone (pv (delayTone));
    const float dSecs = pv (delaySync) < 0.5f ? (float) (divisionBeats (pv (delayDiv)) * 60.0 / bpm) : pv (delayFree);
    const float lvl = pv (level) * kLevelScale;   // headroom for six voices: an 8-note full-velocity chord peaks below 0 dBFS
    for (int i = 0; i < n; ++i)
    {
        float l = bufL[(size_t) i], r = bufR[(size_t) i];
        if (dAmt > 0.001f) drive.process (l, r, dType, dAmt, 1.0f);
        chorus.process (l, r, cMode, std::min (1.0f, cMix * 2.0f));   // runs every sample: the BBD line stays filled
        float ol = l, orr = r;
        if (delayOn) delay.process (l, r, ol, orr, dSecs, fb, dMix);
        const float g = levelSm.tick (lvl);
        bufL[(size_t) i] = ol * g; bufR[(size_t) i] = orr * g;
    }
    reverbCard (verb, bufL.data(), bufR.data(), bufL.data(), bufR.data(), n, pv (reverb) * reverbScale, pv (revSize), pv (revSize + 1), pv (revSize + 2));
    for (int i = 0; i < n; ++i) { outL[i] += softLimit (bufL[(size_t) i]); outR[i] += softLimit (bufR[(size_t) i]); }
    trackSilence (bufL.data(), bufR.data(), n);
}

void PolyDevice::renderChunk (float* L, float* R, int start, int len)
{
    // the voices' pitch, cutoff and filter coefficients move at a control rate of 1/8 of the samples (0.17 ms at
    // 48 kHz): inaudible as steps, and it takes the exp / log / tan of every voice off every sample
    constexpr int kCtl = 8;
    if (len <= 0) return;
    const float sr = (float) sampleRate;
    const bool va = pv (engine) > 0.5f;
    const int mode = (int) pv (voiceMode);
    const int dWave = (int) pv (dcoWave), vWave = (int) pv (vaWave);
    const float pwBase = va ? pv (vaPw) : pv (dcoPw);
    const float pwmAmt = va ? pv (vaPwm) : pv (dcoPwm);
    const float lPwm = pv (lfoPwm), lPitch = pv (lfoPitch), lCut = pv (lfoCut);
    const float octave = va ? (float) ((int) pv (vaOctave) - 1) * 12.0f : 0.0f;   // 16' an octave down … 2' two up
    const float fine = va ? pv (vaFine) / 100.0f : 0.0f;
    const float subLvl = pv (sub), noiseLvl = pv (noise), driftAmt = pv (drift);
    const int hpPos = (int) pv (hpf);
    const float cut = pv (cutoff), res = pv (reso), envA = pv (envAmt), keyT = pv (key), fVel = pv (filterVel);
    const bool sharedFilt = va && pv (filterMode) < 0.5f;
    const float fDrv = pv (filterDrive);
    const int aSrc = (int) pv (ampSrc);
    const float gateCoef = 1.0f - std::exp (-1.0f / (0.0015f * (float) sampleRate));
    const float velSens = pv (velocity);
    const float det = pv (detune) * 0.3f;
    const float glideSecs = pv (glide) * 1.5f;
    const float glideCoef = glideSecs < 0.002f ? 1.0f : 1.0f - std::exp (-1.0f / (glideSecs * 0.35f * sr));
    const float fadeStep = 1.0f / (0.002f * sr);
    const float velGlide = 1.0f - std::exp (-1.0f / (0.003f * sr));
    const int nOsc = (mode == 3 || mode == 4 || mode == 5) ? 3 : 1;
    // Octave, Fifth, Ring: osc 2 and 3 against osc 1 (Ring multiplies osc 1 by osc 2 a fifth up: sum and
    // difference tones, the bell of a ring modulator; osc 3 an octave down mixes in as it is)
    static constexpr float stackOff[3][3] = { { 0, 12, -12 }, { 0, 7, 12 }, { 0, 7, -12 } };
    const float sr2 = 2.0f * sr;

    const float fa = pv (fAttack), fd = pv (fDecay), fs = pv (fSustain), fr = pv (fRelease);
    const float aa = pv (aAttack), ad = pv (aDecay), as = pv (aSustain), ar = pv (aRelease);
    sharedEnv.set (fa, fd, fs, fr);
    float sharedBuf[256];

    // Process in pieces of at most 256 samples so the paraphonic filter can run after the voice sum.
    for (int off = 0; off < len; off += 256)
    {
        const int m = std::min (256, len - off);
        if (sharedFilt) std::fill (sharedBuf, sharedBuf + m, 0.0f);
        for (auto& v : voices)
        {
            if (! v.aenv.active() && ! v.fenv.active()) { v.slot.active = false; continue; }
            const float et = v.tol.envTime;
            v.fenv.set (fa * et, fd * et, fs, fr * et);
            v.aenv.set (aa * et, ad * et, as, ar * et);
            double hz[3] {};   // control rate (below): pitch, cutoff and filter coefficients every kCtl samples
            for (int k = 0; k < m; ++k)
            {
                const int i = start + off + k;
                const float lv = lfoBuf[(size_t) i];
                v.pitch += (v.target - v.pitch) * glideCoef;
                const float fenv = v.fenv.process();
                float aenv = v.aenv.process();
                if (aSrc == 1) aenv = fenv;
                else if (aSrc == 2)
                {
                    // Gate: on while the key is down, off at its release (slewed, so neither edge clicks)
                    const float gateTarget = v.fenv.stage == Adsr::Release || v.fenv.stage == Adsr::Idle ? 0.0f : 1.0f;
                    v.gateAmp += (gateTarget - v.gateAmp) * gateCoef;
                    aenv = v.gateAmp;
                }
                const float pw = clamp (pwBase + 0.4f * (pwmAmt + lPwm) * lv, 0.05f, 0.95f);

                if (k % kCtl == 0)
                {
                    // DCO pitch is crystal-locked: its Drift spreads and slowly moves each voice's filter instead;
                    // VCOs wander a little in pitch (more with Drift)
                    const float drift = v.tol.vcoSemis (sr / (float) kCtl);
                    const float wander = va ? drift * (1.0f + 4.0f * driftAmt) : 0.0f;
                    const float filterDrift = va ? 0.0f : driftAmt * (40.0f * drift + 6.0f * (v.tol.cutoff - 1.0f)) * 0.0578f;
                    const float baseNote = v.pitch + octave + fine + v.stack * det + lv * lPitch + wander + exprNow;
                    const float c = std::log (cut * v.tol.cutoff) + (baseNote - 60.0f) * keyT * 0.0578f + envA * fenv * 4.6f
                                    + lCut * lv * 2.2f + fVel * (v.vel - 0.5f) * 1.6f + filterDrift;
                    const float fc = clamp (std::exp (c), 15.0f, 20000.0f);
                    for (int o = 0; o < nOsc; ++o) hz[o] = midiToHz (baseNote + (nOsc == 3 ? stackOff[mode - 3][o] : 0.0f));
                    if (! va) v.cascade.set (fc, res, sr2);
                    else if (! sharedFilt) v.peak.set (fc, res, sr2);
                }
                // one oscillator-mixer sample at the 2× rate
                auto gen = [&]
                {
                    float sig = 0.0f, ring0 = 0.0f;
                    for (int o = 0; o < nOsc; ++o)
                    {
                        float s;
                        if (! va)
                        {
                            auto& d = v.dco[(size_t) o];
                            d.setFreq (hz[o], 2.0 * sampleRate); d.advance();
                            s = dWave == 0 ? d.pulse (pw) : (dWave == 1 ? d.saw() : 0.7f * (d.saw() + d.pulse (pw)));
                            if (o == 0 && subLvl > 0.0f) sig += subLvl * 0.67f * v.dco[0].sub();
                        }
                        else
                        {
                            auto& osc = v.osc[(size_t) o];
                            osc.setFreq ((float) hz[o], sr2); osc.advance();
                            s = vWave == 0 ? osc.saw() : (vWave == 1 ? osc.pulse (pw) : osc.tri());
                            if (o == 0 && subLvl > 0.0f) sig += subLvl * 0.6f * v.subOsc.process (osc.wrapped, 0);
                        }
                        if (mode == 5 && o == 0) { ring0 = s; continue; }                         // ring: osc 1 waits for osc 2
                        if (mode == 5 && o == 1) { sig += v.ringDc.process (ring0 * s) * 1.1f; continue; }   // osc 1 × osc 2, DC removed
                        sig += nOsc == 3 ? s * 0.55f : s;
                    }
                    if (noiseLvl > 0.0f) sig += noiseLvl * 0.5f * v.noiseLp.lp (v.noiseGen.white());   // off: not generated at all
                    return sig * 0.5f;
                };
                float y;
                if (! va)
                {
                    auto tick = [&] { return v.cascade.process (v.hp.process (gen(), hpPos, sr2) * 0.8f); };
                    const float t0 = tick(), t1 = tick();
                    y = v.os.downsample (t0, t1) * 1.25f;
                }
                else if (! sharedFilt)
                {
                    auto tick = [&] { return v.peak.process (gen(), fDrv); };
                    const float t0 = tick(), t1 = tick();
                    y = v.os.downsample (t0, t1);
                }
                else { const float t0 = gen(), t1 = gen(); y = v.os.downsample (t0, t1); }

                if (v.fadeIn < 1.0f) v.fadeIn = std::min (1.0f, v.fadeIn + fadeStep);
                v.velGain += ((1.0f - velSens + velSens * v.vel) - v.velGain) * velGlide;
                const float g = aenv * v.fadeIn * v.velGain * v.tol.gain;
                const float out = y * g;
                if (sharedFilt) sharedBuf[k] += out;
                else
                {
                    L[i] += out * (1.0f - std::max (0.0f, v.pan));
                    R[i] += out * (1.0f + std::min (0.0f, v.pan));
                }
            }
            v.slot.level = v.aenv.value;
            if (! v.aenv.active()) v.slot.active = false;
        }
        if (sharedFilt)
        {
            for (int k = 0; k < m; ++k)
            {
                const int i = start + off + k;
                const float env = sharedEnv.process();
                const float c = std::log (cut) + (lastPitch - 60.0f) * keyT * 0.0578f + envA * env * 4.6f + lCut * lfoBuf[(size_t) i] * 2.2f;
                sharedFilter.set (clamp (std::exp (c), 15.0f, 20000.0f), res, sr2);
                // the voice sum is padded into the one filter's input stage (six voices would slam it)
                const float y = 2.5f * dsp::analog::run2x (sharedOs, sharedBuf[k] * 0.4f, [this, fDrv] (float x) { return sharedFilter.process (x, fDrv); });
                L[i] += y; R[i] += y;
            }
        }
    }
}
} // namespace mb
