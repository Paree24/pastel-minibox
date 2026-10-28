#include "MonoDevice.h"

namespace mb
{
using namespace dsp;

MonoDevice::MonoDevice() : DeviceProcessor (DeviceType::Mono)
{
    auto& L = layout;
    L.integer ("range", "Range", 0, 3, 1, Unit::Footage);
    L.cont ("tune", "Tune", -12, 12, 0, Unit::Semis, false, true);
    L.cont ("pw", "PW", 0.05f, 0.95f, 0.5f, Unit::Percent);
    L.cont ("pwm", "PWM", 0, 1, 0, Unit::Percent);
    L.choice ("pwmSrc", "PWM Source", { "LFO", "Env", "Manual" }, 0);
    L.cont ("mixPulse", "Pulse", 0, 1, 0, Unit::Percent);
    L.cont ("mixSaw", "Saw", 0, 1, 1, Unit::Percent);
    L.cont ("mixSub", "Sub", 0, 1, 0, Unit::Percent);
    L.choice ("subType", "Sub Type", { "-1 Sq", "-2 Sq", "-2 25%" }, 0);
    L.cont ("mixNoise", "Noise", 0, 1, 0, Unit::Percent);
    L.cont ("cutoff", "Cutoff", 30, 18000, 18000, Unit::Hz, true);
    L.cont ("reso", "Reso", 0, 1, 0, Unit::Percent);
    L.cont ("envAmt", "Env", -1, 1, 0, Unit::Plain, false, true);
    L.cont ("lfoCut", "LFO", 0, 1, 0, Unit::Percent);
    L.cont ("key", "Key", 0, 1, 0.5f, Unit::Percent);
    L.cont ("drive", "Drive", 0, 1, 0, Unit::Percent);
    L.cont ("attack", "Attack", 0.001f, 8, 0.003f, Unit::Seconds, true);
    L.cont ("decay", "Decay", 0.005f, 10, 0.4f, Unit::Seconds, true);
    L.cont ("sustain", "Sustain", 0, 1, 1, Unit::Percent);
    L.cont ("release", "Release", 0.005f, 10, 0.12f, Unit::Seconds, true);
    L.choice ("ampMode", "Amp", { "Env", "Gate" }, 0);
    L.choice ("lfoShape", "LFO Shape", { "Tri", "Sq", "Rnd", "Noise" }, 0);
    L.cont ("lfoRate", "Rate", 0.05f, 30, 4, Unit::Hz, true);
    L.cont ("lfoFade", "Fade", 0, 4, 0, Unit::Seconds);
    L.cont ("lfoPitch", "Pitch", 0, 1, 0, Unit::Percent);
    L.cont ("lfoPwm", "PWM", 0, 1, 0, Unit::Percent);
    L.choice ("glideMode", "Glide", { "Off", "On", "Auto" }, 0);
    L.cont ("glideTime", "Time", 0.005f, 2, 0.08f, Unit::Seconds, true);
    L.choice ("voiceMode", "Voice", { "Mono", "Unison", "Poly", "Chord" }, 0);
    L.cont ("detune", "Detune", 0, 1, 0, Unit::Percent);
    L.choice ("chord", "Chord", chordShapeNames(), 0);
    L.cont ("fxChorus", "Chorus", 0, 1, 0, Unit::Percent);
    L.cont ("fxDelay", "Delay", 0, 1, 0, Unit::Percent);
    L.integer ("fxTime", "Time", 0, kNumDivisions - 1, 6, Unit::Division);
    L.cont ("fxFeedback", "Feedback", 0, 0.9f, 0.35f, Unit::Percent);
    L.cont ("fxReverb", "Reverb", 0, 1, 0, Unit::Percent);
    L.cont ("level", "Level", 0, 2, 0.7f, Unit::Percent);
    addReverbParams (L);
    L.choice ("lfoSync", "Sync", { "Free", "Sync" }, 0);
    L.integer ("lfoDiv", "Rate", 0, kNumDivisions - 1, 7, Unit::Division);
    jassert (layout.size() == numParams);
    finishLayout();

    macro (0, "Cutoff", { { "cutoff", 0.15f, 1.0f } });
    macro (1, "Reso", { { "reso", 0.0f, 0.95f } });
    macro (2, "Env", { { "envAmt", 0.5f, 1.0f } });
    macro (3, "Sub", { { "mixSub", 0.0f, 1.0f } });
    macro (4, "PWM", { { "pwm", 0.0f, 1.0f } });
    macro (5, "Decay", { { "decay", 0.1f, 0.9f } });
    macro (6, "Glide", { { "glideTime", 0.0f, 0.8f } });
    macro (7, "LFO", { { "lfoCut", 0.0f, 1.0f } });
}

void MonoDevice::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    const float r = (float) sr;
    for (auto& v : voices)
    {
        v.env.prepare (r); v.sub.prepare (2.0f * r);
        v.osc.randomise (rng); v.noise.rng.seed (rng.next());
        v.tol.roll (rng, true);
    }
    lfo.prepare (r);
    fx.prepare (r); verb.prepare (r);
    bufL.assign ((size_t) maxBlock, 0.0f); bufR.assign ((size_t) maxBlock, 0.0f); lfoBuf.assign ((size_t) maxBlock, 0.0f); cutBuf.assign ((size_t) maxBlock, 0.0f);
    cutSm.setTime (8.0f, r); cutSm.reset (std::log (get (cutoff)));
    levelSm.setTime (15.0f, r); levelSm.reset (get (level));
}

void MonoDevice::reset()
{
    for (auto& v : voices) { v.env.kill(); v.filter.reset(); v.os.reset(); v.slot = {}; }
    held.clear();
    fx.reset(); verb.reset();
}

int MonoDevice::numActiveVoices() const
{
    const int mode = (int) get (voiceMode);
    return mode == 0 ? 1 : 4;
}

bool MonoDevice::anyVoiceActive() const
{
    for (auto& v : voices) if (v.env.active()) return true;
    return false;
}

void MonoDevice::startVoice (Voice& v, int note, float vel, bool legato, bool glide)
{
    v.targetPitch = (float) note;
    if (! glide || ! v.env.active()) v.pitch = (float) note;
    v.vel = vel;
    if (! v.env.active()) { v.fadeIn = 0.0f; v.velGain = 0.55f + 0.45f * vel; }
    v.env.gateOn (legato);   // legato = returning to a still-held key on release: no re-attack
    // the shared LFO restarts with the first key, not with every voice of a chord
    bool othersHeld = false;
    for (auto& o : voices) othersHeld = othersHeld || (&o != &v && o.slot.held);
    if (! legato && v.slot.note < 0 && ! othersHeld) lfo.retrigger();
    v.slot.note = note; v.slot.held = true; v.slot.active = true; v.slot.age = ++clock;
}

void MonoDevice::noteOn (int note, float vel)
{
    const int mode = (int) get (voiceMode);
    const int gmode = (int) get (glideMode);
    markNote (note, true);
    if (mode == 0 || mode == 1)   // Mono / Unison: one note, last-note priority
    {
        const bool legato = held.count > 0;
        held.push (note, vel);
        const bool glide = gmode == 1 || (gmode == 2 && legato);
        if (! legato) lfo.retrigger();
        const int nv = mode == 0 ? 1 : 4;
        for (int i = 0; i < nv; ++i)
        {
            auto& v = voices[(size_t) i];
            v.detuneSemis = mode == 1 ? ((float) i - 1.5f) / 1.5f : 0.0f;
            v.pan = mode == 1 ? ((float) i - 1.5f) / 1.5f * 0.6f : 0.0f;
            startVoice (v, note, vel, false, glide);   // a new key always re-attacks (from the current level)
        }
        return;
    }
    if (mode == 3)   // Chord: the stored shape on up to four voices
    {
        int cnt = 0;
        auto shape = chordShape ((int) get (chord), cnt);
        held.push (note, vel);
        for (int i = 0; i < cnt; ++i)
        {
            auto& v = voices[(size_t) i];
            v.detuneSemis = 0.0f; v.pan = ((float) i - 1.5f) * 0.2f;
            startVoice (v, note + shape[(size_t) i], vel, false, gmode == 1);
            v.slot.note = note;   // released together by the played key
        }
        return;
    }
    // Poly: four voices
    std::array<VoiceSlot, 4> slots;
    for (size_t i = 0; i < 4; ++i) slots[i] = voices[i].slot;
    const int idx = allocateVoice (slots, note, 4, clock);
    auto& v = voices[(size_t) idx];
    v.detuneSemis = 0.0f; v.pan = ((float) idx - 1.5f) * 0.15f;
    startVoice (v, note, vel, false, gmode == 1);
}

void MonoDevice::noteOff (int note)
{
    markNote (note, false);
    const int mode = (int) get (voiceMode);
    if (mode == 0 || mode == 1)
    {
        held.remove (note);
        const int nv = mode == 0 ? 1 : 4;
        if (held.count > 0)
        {
            const int gm = (int) get (glideMode);
            for (int i = 0; i < nv; ++i) startVoice (voices[(size_t) i], held.top(), held.topVel(), true, gm != 0);
        }
        else
            for (int i = 0; i < nv; ++i) { voices[(size_t) i].env.gateOff(); voices[(size_t) i].slot.held = false; voices[(size_t) i].slot.note = -1; }
        return;
    }
    held.remove (note);
    for (auto& v : voices)
        if (v.slot.note == note && v.slot.held) { v.env.gateOff(); v.slot.held = false; }
}

void MonoDevice::handleEvent (const NoteEvent& e)
{
    switch (e.type)
    {
        case NoteEvent::NoteOn: noteOn (e.note, e.velocity); break;
        case NoteEvent::NoteOff: noteOff (e.note); break;
        case NoteEvent::AllNotesOff:
            held.clear();
            for (auto& v : voices) { v.env.gateOff(); v.slot.held = false; v.slot.note = -1; }
            for (auto& w : heldNotes) w.store (0);
            break;
        case NoteEvent::Panic: reset(); break;
    }
}

void MonoDevice::render (float* outL, float* outR, int n)
{
    const float xs = expressionSemis (n);   // pitch bend and mod-wheel vibrato
    const float sr = (float) sampleRate;
    // snapshot every parameter once per block (locks included)
    const float rangeOct = (float) ((int) pv (range) - 1) * 12.0f;   // 16' an octave down … 2' two up
    const float tuneSt = pv (tune), pwv = pv (pw), pwmAmt = pv (pwm);
    const int pwmSource = (int) pv (pwmSrc), sType = (int) pv (subType);
    const float mP = pv (mixPulse), mS = pv (mixSaw), mSub = pv (mixSub), mN = pv (mixNoise);
    const float cut = pv (cutoff), res = pv (reso), envA = pv (envAmt), lfoC = pv (lfoCut), keyT = pv (key), drv = pv (drive);
    const int amode = (int) pv (ampMode);
    const float gateCoef = 1.0f - std::exp (-1.0f / (0.0015f * (float) sampleRate));
    const float lRate = pv (lfoRate), lPitch = pv (lfoPitch), lPwm = pv (lfoPwm);
    const int lShape = (int) pv (lfoShape);
    static constexpr int shapeMap[4] = { Lfo::Tri, Lfo::Square, Lfo::SampleHold, Lfo::SmoothNoise };
    const float detuneSemis = pv (detune) * 0.25f;
    const float glideSecs = pv (glideTime);
    const float glideCoef = 1.0f - std::exp (-1.0f / (glideSecs * 0.4f * sr));
    if (pv (lfoSync) > 0.5f)   // tempo-synced: the rate follows the project / host tempo, the phase its bar
    {
        const double beats = divisionBeats (pv (lfoDiv)), bpm = std::max (20.0, transport.bpm);
        lfo.setRate ((float) (bpm / 60.0 / beats));
        if (transport.playing) lfo.syncTo (transport.beatPos, beats);
    }
    else lfo.setRate (lRate);
    lfo.setDelay (pv (lfoFade));
    const float a = pv (attack), d = pv (decay), s = pv (sustain), r = pv (release);
    const float mixNorm = 1.0f / std::max (1.0f, mP + mS + mSub * 0.8f + mN * 0.6f);

    std::fill (bufL.begin(), bufL.begin() + n, 0.0f);
    std::fill (bufR.begin(), bufR.begin() + n, 0.0f);
    const float fadeStep = 1.0f / (0.002f * sr);
    const float velGlide = 1.0f - std::exp (-1.0f / (0.003f * sr));
    const int vmode = (int) pv (voiceMode);
    const float voiceScale = vmode >= 2 ? 0.5f : (vmode == 1 ? 0.6f : 1.0f);   // several voices sum: keep the headroom
    for (int i = 0; i < n; ++i) lfoBuf[(size_t) i] = lfo.process (shapeMap[lShape]);
    // the cutoff knob glides (8 ms) instead of stepping at block edges
    for (int i = 0; i < n; ++i) cutBuf[(size_t) i] = cutSm.tick (std::log (cut));

    for (auto& v : voices)
    {
        if (! v.env.active()) continue;
        const float et = v.tol.envTime;
        v.env.set (a * et, d * et, s, r * et);
        for (int i = 0; i < n; ++i)
        {
            const float lv = lfoBuf[(size_t) i];
            v.pitch += (v.targetPitch - v.pitch) * glideCoef;
            if (std::abs (v.targetPitch - v.pitch) < 0.002f) v.pitch = v.targetPitch;
            const float note = v.pitch + rangeOct + tuneSt + v.detuneSemis * detuneSemis + lv * lPitch * 2.0f + v.tol.vcoSemis (sr) + xs;
            const float env = v.env.process();
            float pwNow = pwv;
            const float pwmSig = pwmSource == 0 ? lv : (pwmSource == 1 ? env * 2.0f - 1.0f : 0.0f);
            pwNow += 0.42f * (pwmAmt * pwmSig + lPwm * lv);
            const float cutNote = cutBuf[(size_t) i] + std::log (v.tol.cutoff) + (note - 60.0f) * (keyT * 0.0578f)
                                  + envA * env * 4.6f + lfoC * lv * 2.2f;
            const float fc = clamp (std::exp (cutNote), 15.0f, 20000.0f);
            v.filter.set (fc, res, 2.0f * sr);
            // oscillators and filter run at 2×: the oscillators' residual aliasing lands above the audio
            // band and is removed by the decimator; the mixer drives the filter's input OTA (Drive)
            v.osc.setFreq (midiToHz (note), 2.0f * sr);
            auto tick = [&]
            {
                v.osc.advance();
                const float sig = (mP * v.osc.pulse (pwNow) + mS * v.osc.saw()
                                   + mSub * 0.8f * v.sub.process (v.osc.wrapped, sType) + mN * 0.6f * v.noise.white()) * mixNorm;
                return v.filter.process (sig * 0.8f * (1.0f + drv * 3.0f));
            };
            const float t0 = tick(), t1 = tick();
            float y = v.os.downsample (t0, t1) * 1.25f;
            if (drv > 0.0f) y /= (1.0f + drv * 1.2f);
            float amp = env;
            if (amode == 1)
            {
                // Gate: on while the key is down, off at its release (slewed, so neither edge clicks)
                const float gateTarget = v.env.stage == Adsr::Release || v.env.stage == Adsr::Idle ? 0.0f : 1.0f;
                v.gateAmp += (gateTarget - v.gateAmp) * gateCoef;
                amp = v.gateAmp;
            }
            if (v.fadeIn < 1.0f) v.fadeIn = std::min (1.0f, v.fadeIn + fadeStep);
            v.velGain += ((0.55f + 0.45f * v.vel) - v.velGain) * velGlide;
            const float out = y * amp * v.fadeIn * v.velGain * v.tol.gain * voiceScale;
            bufL[(size_t) i] += out * (1.0f - std::max (0.0f, v.pan));
            bufR[(size_t) i] += out * (1.0f + std::min (0.0f, v.pan));
        }
        v.slot.level = v.env.value;
        if (! v.env.active()) v.slot.active = false;
    }
    const float lvlT = pv (level);
    for (int i = 0; i < n; ++i) { const float g = levelSm.tick (lvlT); bufL[(size_t) i] *= g; bufR[(size_t) i] *= g; }
    fx.process (bufL.data(), bufR.data(), n, 1, pv (fxChorus), pv (fxDelay) * 0.6f, divisionBeats (pv (fxTime)),
                pv (fxFeedback), transport);
    verb.process (bufL.data(), bufR.data(), bufL.data(), bufR.data(), n, pv (fxReverb) * reverbScale, pv (revSize), pv (revSize + 1), pv (revSize + 2));
    for (int i = 0; i < n; ++i) { outL[i] += softLimit (bufL[(size_t) i]); outR[i] += softLimit (bufR[(size_t) i]); }
    trackSilence (bufL.data(), bufR.data(), n);
}
} // namespace mb
