#include "AcidDevice.h"

namespace mb
{
using namespace dsp;

AcidDevice::AcidDevice() : DeviceProcessor (DeviceType::Acid)
{
    auto& L = layout;
    L.cont ("oscWave", "Wave", 0, 1, 0, Unit::Percent);
    L.cont ("pw", "PW", 0.1f, 0.9f, 0.5f, Unit::Percent);
    L.cont ("sub", "Sub", 0, 1, 0, Unit::Percent);
    L.cont ("noise", "Noise", 0, 1, 0, Unit::Percent);
    L.cont ("tune", "Tune", -12, 12, 0, Unit::Semis, false, true);
    L.cont ("fine", "Fine", -50, 50, 0, Unit::Cents, false, true);
    L.choice ("stack", "Stack", { "Single", "Stack" }, 0);
    L.choice ("stackMode", "Stack Mode", { "Detune", "Fifth", "Oct" }, 1);
    L.cont ("stackDetune", "Detune", 0, 1, 0, Unit::Percent);
    L.cont ("stackSpread", "Spread", 0, 1, 0.5f, Unit::Percent);
    L.cont ("stackMix", "Mix", 0, 1, 0.6f, Unit::Percent);
    L.choice ("filterType", "Filter", { "Ladder", "Diode" }, 1);
    L.cont ("cutoff", "Cutoff", 200, 4000, 1000, Unit::Hz, true);          // 303 nominal range 314–2394 Hz, a little wider
    L.cont ("reso", "Reso", 0, 1, 0.3f, Unit::Percent);
    L.cont ("envMod", "Env Mod", 0, 1, 0, Unit::Percent);
    L.cont ("decay", "Decay", 0.2f, 2.0f, 1.0f, Unit::Seconds, true);       // 303 main envelope decay
    L.cont ("key", "Key", 0, 1, 0, Unit::Percent);
    L.cont ("bassComp", "Bass Comp", 0, 1, 0.25f, Unit::Percent);           // feedback high-pass 8–150 Hz (the circuit: ≈ 10 Hz)
    L.cont ("accAmt", "Amount", 0, 1, 0.5f, Unit::Percent);
    L.cont ("sweep", "Sweep", 0, 1, 0.17f, Unit::Percent);                  // accent RC 2–80 ms
    L.cont ("accDecay", "Acc Decay", 0.05f, 2, 0.2f, Unit::Seconds, true);
    L.choice ("slideMode", "Slide", { "Fixed", "Legato" }, 0);
    L.cont ("slideTime", "Time", 0.01f, 0.5f, 0.06f, Unit::Seconds, true);
    L.choice ("ampMode", "Amp", { "Gate", "Decay" }, 0);
    L.cont ("level", "Level", 0, 2, 0.7f, Unit::Percent);
    L.cont ("velocity", "Velocity", 0, 1, 0.3f, Unit::Percent);
    L.cont ("ampDecay", "Decay", 0.05f, 4, 0.6f, Unit::Seconds, true);
    L.choice ("lfoShape", "LFO Shape", { "Tri", "Saw", "Sq", "S&H" }, 0);
    L.choice ("lfoSync", "Sync", { "Free", "Sync" }, 0);
    L.cont ("lfoRate", "Rate", 0.05f, 30, 2, Unit::Hz, true);
    L.integer ("lfoDiv", "Rate", 0, kNumDivisions - 1, 7, Unit::Division);
    L.cont ("lfoCut", "Cutoff", 0, 1, 0, Unit::Percent);
    L.cont ("lfoPitch", "Pitch", 0, 1, 0, Unit::Percent);
    L.cont ("lfoFade", "Fade", 0, 4, 0, Unit::Seconds);
    L.choice ("driveType", "Drive", { "Overdrive", "Distortion", "Fuzz" }, 0);
    L.cont ("driveAmt", "Amount", 0, 1, 0, Unit::Percent);
    L.cont ("driveTone", "Tone", 0, 1, 0.7f, Unit::Percent);
    L.cont ("driveMix", "Mix", 0, 1, 1, Unit::Percent);
    L.choice ("delaySync", "Delay", { "Sync", "Free" }, 0);
    L.integer ("delayDiv", "Time", 0, kNumDivisions - 1, 9, Unit::Division);
    L.cont ("delayFree", "Time", 0.02f, 2, 0.3f, Unit::Seconds, true);
    L.cont ("delayFb", "Feedback", 0, 0.92f, 0.35f, Unit::Percent);
    L.cont ("delayDuck", "Duck", 0, 1, 0.5f, Unit::Percent);
    L.cont ("delayTone", "Tone", 0, 1, 0.5f, Unit::Percent);
    L.cont ("delayMix", "Mix", 0, 1, 0, Unit::Percent);
    L.cont ("reverb", "Reverb", 0, 1, 0, Unit::Percent);
    L.choice ("artic", "Articulation", { "Normal", "Accent", "Slide", "Acc+Slide" }, 0);   // also shows the held C♯0–D♯0 keyswitches
    addReverbParams (L);
    jassert (layout.size() == numParams);
    finishLayout();

    macro (0, "Cutoff", { { "cutoff", 0.05f, 0.95f } });
    macro (1, "Reso", { { "reso", 0.0f, 0.97f } });
    macro (2, "Env Mod", { { "envMod", 0.0f, 1.0f } });
    macro (3, "Decay", { { "decay", 0.05f, 0.85f } });
    macro (4, "Accent", { { "accAmt", 0.0f, 1.0f }, { "sweep", 0.0f, 1.0f } });
    macro (5, "Slide", { { "slideTime", 0.0f, 0.8f } });
    macro (6, "Drive", { { "driveAmt", 0.0f, 1.0f } });
    macro (7, "Delay", { { "delayMix", 0.0f, 0.8f } });
}

void AcidDevice::prepare (double sr, int maxBlock)
{
    sampleRate = sr;
    baseRate = sr;
    const double osRate = sr * kOversampling;
    for (auto& o : osc) o.randomise (rng);
    subOsc.prepare ((float) osRate);
    filter.prepare (osRate);
    couplingHp.setCutoff (8.0f, (float) osRate);   // coupling capacitors around the core: ≈ 8 Hz
    outHp.setCutoff (12.0f, (float) sr);
    lfo.prepare ((float) sr);
    drive.prepare ((float) sr);
    delay.prepare ((float) sr);
    verb.prepare ((float) sr);
    accVcaCoef = 1.0 - std::exp (-1.0 / (sr * 0.00155));   // 47 kΩ × 0.033 µF: the accent's soft VCA attack
    ampAttack = 1.0 - std::exp (-1.0 / (sr * 0.0007));
    bufL.assign ((size_t) maxBlock, 0.0f); bufR.assign ((size_t) maxBlock, 0.0f);
    levelSm.setTime (15.0f, (float) sr); levelSm.reset (get (level) * kLevelScale);
}

void AcidDevice::reset()
{
    held.clear(); noteOn = false; currentNote = -1;
    if (keysHeld != 0) { keysHeld = 0; set (artic, (float) articBase); }
    amp = ampTarget = meg = accVca = 0.0;
    filter.reset(); couplingHp.reset(); outHp.reset(); dec1.reset(); dec2.reset();
    sweepCircuit.cap = 0.0;
    drive.reset(); delay.reset(); verb.reset();
}

void AcidDevice::trigger (int note, bool accent, float velocity_)
{
    accentNow = accent;
    accentGain = accent ? get (accAmt) : 0.0;
    vel = velocity_;
    oscFreq = dsp::midiToHz ((float) note);
    slewedFreq = oscFreq;
    pitchSemis = targetSemis = (double) note;
    megTarget = 1.0;   // the envelope capacitor charges in well under a millisecond (a step would click)
    ampTarget = 1.0;   // the volume envelope: sharp attack
    noteOn = true;
    currentNote = note;
    lfo.retrigger();
}

void AcidDevice::slideTo (int note, bool accent)
{
    accentNow = accent;
    accentGain = accent ? get (accAmt) : 0.0;
    oscFreq = dsp::midiToHz ((float) note);
    targetSemis = (double) note;
    currentNote = note;
}

void AcidDevice::keyswitch (int note, bool down)
{
    // C♯0 = accent (bit 1), D0 = slide (bit 2), D♯0 = both; several held keys combine
    const int bits = note - kKeyswitchLow + 1;
    if (down && keysHeld == 0) articBase = (int) get (artic);
    keysHeld = down ? keysHeld | (1 << bits) : keysHeld & ~(1 << bits);
    int art = 0;
    for (int b = 1; b <= 3; ++b) if (keysHeld & (1 << b)) art |= b;
    set (artic, (float) (keysHeld != 0 ? art : articBase));
}

void AcidDevice::handleEvent (const NoteEvent& e)
{
    switch (e.type)
    {
        case NoteEvent::NoteOn:
        {
            if (e.note >= kKeyswitchLow && e.note <= kKeyswitchHigh) { keyswitch (e.note, true); break; }
            markNote (e.note, true);
            const int art = (int) get (artic);
            const bool accent = (e.flags & kAccent) != 0 || (art & 1) != 0 || e.velocity > 0.94f;
            // a sequencer slide / tie step holds its note into the next one: that overlap slides, as does any
            // overlap under the Slide articulation; any other overlap (legato playing on a keyboard) is a new
            // note and re-triggers the envelopes
            if (held.count > 0 && (heldSlides || (art & 2) != 0)) slideTo (e.note, accent);
            else trigger (e.note, accent, e.velocity);
            heldSlides = (e.flags & (kSlide | kTie)) != 0;
            held.push (e.note, e.velocity);
            break;
        }
        case NoteEvent::NoteOff:
            if (e.note >= kKeyswitchLow && e.note <= kKeyswitchHigh) { keyswitch (e.note, false); break; }
            markNote (e.note, false);
            held.remove (e.note);
            if (held.count > 0) { if (heldSlides || ((int) get (artic) & 2) != 0) slideTo (held.top(), accentNow); }
            else { noteOn = false; heldSlides = false; }
            break;
        case NoteEvent::AllNotesOff:
            held.clear(); noteOn = false; for (auto& w : heldNotes) w.store (0);
            if (keysHeld != 0) { keysHeld = 0; set (artic, (float) articBase); }
            break;
        case NoteEvent::Panic: reset(); break;
    }
}

void AcidDevice::render (float* outL, float* outR, int n)
{
    const float xs = expressionSemis (n);   // pitch bend and mod-wheel vibrato
    using namespace dsp;
    const double sr = baseRate, osRate = sr * kOversampling;
    const float wave = pv (oscWave);
    const double s0 = -0.0624 + (pv (pw) - 0.5) * 1.6;         // where the 303 square's tanh crosses zero
    // the circuit's square is the saw through a 36.9 dB tanh shaper: at that drive it is a pulse whose
    // falling edge sits where the shaper crosses zero; generated band-limited so the edges never alias
    const float sqWidth = (float) std::clamp ((s0 + 1.0) * 0.5, 0.05, 0.95);
    const float subL = pv (sub), noiseL = pv (noise);
    const double tuneSt = pv (tune) + pv (fine) / 100.0;
    const bool stacked = pv (stack) > 0.5f;
    const int sMode = (int) pv (stackMode);
    const float sDet = pv (stackDetune), sSpread = pv (stackSpread), sMix = pv (stackMix);
    // Filter type: Diode = the circuit's ladder; Ladder = the same core driven harder at the input
    // (a cleaner, rounder character from the saturating input pair)
    const bool diode = pv (filterType) > 0.5f;
    const double res = pv (reso), fbHp = 8.0 * std::pow (150.0 / 8.0, (double) pv (bassComp));
    const double cut = pv (cutoff), keyT = pv (key);
    const double envOct = 4.5 * pv (envMod);   // Env Mod: how many octaves the main envelope sweeps the cutoff
    const double sweepPot = pv (sweep);
    sweepCircuit.prepare (sr, sweepPot);
    megCoef = std::exp (-1.0 / (sr * (accentNow ? pv (accDecay) : pv (decay))));
    megAttack = 1.0 - std::exp (-1.0 / (sr * 0.0004));
    const bool gateAmp = pv (ampMode) < 0.5f;
    // the volume envelope decays slowly while the gate is held (Gate) or as a pluck (Decay)
    ampDecayCoef = std::exp (-1.0 / (sr * (gateAmp ? 1.6 : (double) pv (ampDecay))));
    ampReleaseCoef = std::exp (-1.0 / (sr * (accentNow ? 0.03 : 0.006)));
    const double slideSecs = pv (slideTime);
    const bool legatoSlide = pv (slideMode) > 0.5f;
    slewCoef = std::exp (-1.0 / (sr * 0.2 * slideSecs));
    const double legatoStep = 12.0 / (std::max (0.005, slideSecs) * sr);
    const double velTarget = std::pow (10.0, (vel - 1.0) * pv (velocity) * 12.0 / 20.0);
    const double velGlide = 1.0 - std::exp (-1.0 / (sr * 0.003));   // a new note's velocity never steps the level
    static constexpr int shapeMap[4] = { Lfo::Tri, Lfo::SawDown, Lfo::Square, Lfo::SampleHold };
    const int lShape = shapeMap[std::clamp ((int) pv (lfoShape), 0, 3)];
    const double bpm = std::max (20.0, transport.bpm);
    if (pv (lfoSync) > 0.5f)
    {
        const double beats = divisionBeats (pv (lfoDiv));
        lfo.setRate ((float) (bpm / 60.0 / beats));
        if (transport.playing) lfo.syncTo (transport.beatPos, beats);
    }
    else lfo.setRate (pv (lfoRate));
    lfo.setDelay (pv (lfoFade));
    const float lCut = pv (lfoCut), lPitch = pv (lfoPitch);
    const int dType = (int) pv (driveType);
    const float dAmt = pv (driveAmt), dMix = pv (driveMix);
    const float dTone = pv (driveTone);
    static constexpr float kRestore[3] = { 0.35f, 0.6f, 0.9f };   // overdrive, distortion, fuzz
    const float restoreAmt = kRestore[std::clamp (dType, 0, 2)] * std::min (1.0f, dAmt * 1.6f);
    const float dlMix = pv (delayMix), dlFb = pv (delayFb), duck = pv (delayDuck);
    delay.setTone (pv (delayTone));
    const float dlSecs = pv (delaySync) < 0.5f ? (float) (divisionBeats (pv (delayDiv)) * 60.0 / bpm) : pv (delayFree);
    static constexpr double stackSemis[3][3] = { { 0, 0, 0 }, { 0, 7, 12 }, { 0, 12, -12 } };
    const double detSemis[3] = { 0.0, sDet * 0.22, -sDet * 0.22 };
    const float lvl = pv (level) * kLevelScale;   // the voice runs low: the accent amp boost (up to 4.45×) needs the headroom

    for (int i = 0; i < n; ++i)
    {
        const float lv = lfo.process (lShape);
        // slide: the pitch CV through an RC (Fixed: a constant time); Legato glides at a constant rate in semitones
        double freq;
        if (legatoSlide)
        {
            const double d = targetSemis - pitchSemis;
            pitchSemis += std::abs (d) <= legatoStep ? d : (d > 0 ? legatoStep : -legatoStep);
            freq = midiToHz ((float) pitchSemis);
        }
        else
        {
            slewedFreq = oscFreq + slewCoef * (slewedFreq - oscFreq);
            freq = slewedFreq;
        }
        freq *= std::exp2 ((tuneSt + lv * lPitch * 2.0 + xs) / 12.0);
        // main envelope and the accent RC circuit
        if (megTarget > 0.0) { meg += (megTarget - meg) * megAttack; if (meg > 0.98 * megTarget) megTarget = 0.0; }
        else meg *= megCoef;
        // accent: the main envelope reaches the VCA through 47 kΩ / 0.033 µF and the filter through the
        // sweep circuit, both scaled by the Accent pot
        const double accMeg = accentGain > 0.0 ? meg : 0.0;
        const double sweepCv = sweepCircuit.process (accMeg, sweepPot);
        accVca += (accentGain * accMeg - accVca) * accVcaCoef;   // the RC sits after the Accent pot: no steps
        const double note = 12.0 * std::log2 (freq / 440.0) + 69.0;
        const double instCut = cut * std::exp2 (envOct * meg + accentGain * sweepCv * 2.5 + lCut * lv * 2.0 + keyT * (note - 45.0) / 12.0);
        filter.set (instCut, res, fbHp);
        // volume envelope (sharp attack, slow decay) plus the accented main envelope
        if (ampTarget > 0.0) { amp += (ampTarget - amp) * ampAttack; if (amp > 0.99) ampTarget = 0.0; }
        else amp *= noteOn ? ampDecayCoef : ampReleaseCoef;
        const double ampOut = amp + 2.8 * accVca;

        double s = 0.0;
        const int nOsc = stacked ? 3 : 1;
        for (int k = 0; k < nOsc; ++k)
            osc[(size_t) k].setFreq ((float) (freq * std::exp2 ((stackSemis[sMode][k] + (sMode == 0 ? detSemis[k] : 0.0)) / 12.0)), (float) osRate);
        double fo[kOversampling];
        for (int o = 0; o < kOversampling; ++o)
        {
            double mix = 0.0;
            for (int k = 0; k < nOsc; ++k)
            {
                auto& os = osc[(size_t) k];
                os.advance();
                const double saw = os.saw();
                const double sq = os.pulse (sqWidth);
                // the circuit square is the inverse of the saw: blend it with flipped polarity so the
                // morph adds the two waves instead of cancelling them
                const double v = -((1.0 - wave) * saw - wave * sq);
                mix += k == 0 ? v : v * sMix * 0.7;
            }
            if (stacked) mix *= 0.6;
            mix += subL * 0.8 * subOsc.process (osc[0].wrapped, 0) + noiseL * 0.4 * noiseGen.white();
            const double f = couplingHp.hp ((float) mix);
            fo[o] = diode ? filter.process (f * 0.9) : filter.process (f * 1.8) * 0.55;
        }
        // 4× → 1× through two half-band stages (≈ −70 dB alias rejection; the old IIR let the saw's
        // upper harmonics fold back as grit)
        if constexpr (kOversampling == 4)
        {
            const float a = dec1.downsample ((float) fo[0], (float) fo[1]);
            const float b = dec1.downsample ((float) fo[2], (float) fo[kOversampling - 1]);
            s = dec2.downsample (a, b);
        }
        else s = dec2.downsample ((float) fo[0], (float) fo[kOversampling - 1]);
        s = outHp.hp ((float) s);
        velGain += (velTarget - velGain) * velGlide;
        float out = (float) (s * ampOut * velGain);
        if (dAmt > 0.0005f && dMix > 0.0005f)
        {
            // a pedal after the VCA compresses: the fuzz holds every note at full level until it dies. The voice
            // knows its own VCA envelope, so it puts that shape back (most for the fuzz, a little for the
            // overdrive) and the line keeps its decay and accents.
            const float vca = (float) (ampOut * velGain);
            const float wet = drive.process (out, dType, dAmt, dTone, 1.0f);
            const float restore = std::pow (juce::jlimit (1.0e-4f, 1.5f, vca), restoreAmt);
            out = out + (wet * restore - out) * dMix;
        }
        const float width = stacked ? 0.25f * sSpread : 0.0f;
        bufL[(size_t) i] = out * (1.0f + width * 0.3f);
        bufR[(size_t) i] = out * (1.0f - width * 0.3f);
    }
    for (int i = 0; i < n; ++i)
    {
        float l = bufL[(size_t) i], r = bufR[(size_t) i];
        if (dlMix > 0.001f) delay.process (l, r, l, r, dlSecs, dlFb, dlMix, duck);
        const float g = levelSm.tick (lvl);
        bufL[(size_t) i] = l * g; bufR[(size_t) i] = r * g;
    }
    verb.process (bufL.data(), bufR.data(), bufL.data(), bufR.data(), n, pv (reverb) * reverbScale, pv (revSize), pv (revSize + 1), pv (revSize + 2));
    for (int i = 0; i < n; ++i) { outL[i] += softLimit (bufL[(size_t) i]); outR[i] += softLimit (bufR[(size_t) i]); }   // the output stage rounds overs
    trackSilence (bufL.data(), bufR.data(), n);
}
} // namespace mb
