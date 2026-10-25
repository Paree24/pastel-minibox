"""Mono factory bank: 8 families × 16 hand-designed patches (single-VCO monosynth model).

Each line: name, oscillator recipe, filter (cutoff Hz, resonance, env amount, key follow),
envelope (attack, decay, sustain, release), then extras (LFO, drive, PWM, effects, voicing).
Oscillator recipe tokens: saw, sq, pul:<width>, sub<type>:<level> (type 0 = −1 oct, 1 = −2 oct,
2 = −2 oct 25 %), noise:<level>, pwm:<depth>[:env] (LFO unless :env).
"""


def osc(spec):
    d = dict(mixSaw=0.0, mixPulse=0.0, mixSub=0.0, mixNoise=0.0)
    for tok in spec.split("+"):
        k, *a = tok.split(":")
        if k == "saw": d["mixSaw"] = float(a[0]) if a else 1.0
        elif k == "sq": d.update(mixPulse=float(a[0]) if a else 1.0, pw=0.5)
        elif k == "pul": d.update(mixPulse=1.0, pw=float(a[0]))
        elif k.startswith("sub"): d.update(mixSub=float(a[0]), subType=int(k[3:] or 0))
        elif k == "noise": d["mixNoise"] = float(a[0])
        elif k == "pwm": d.update(pwm=float(a[0]), pwmSrc=1 if len(a) > 1 else 0)
    return d


# The envelope's Decay / Release are times to silence (−80 dB); the recipes give the audible ring (≈ −30 dB),
# so percussive shapes are converted. Plucks, keys, stabs and muted sounds ring at least ≈ 0.45 s.
PERCUSSIVE_WORDS = ("Pluck", "Plucked", "Pizzicato", "Muted", "Staccato", "Stab", "Keys", "Piano", "Harp", "Kalimba", "Marimba", "Koto")


def ring(name, fam, decay, sustain):
    if sustain > 0.3:
        return decay
    d = decay * 2.7
    if fam in ("Pluck", "Keys") or any(w in name.split() for w in PERCUSSIVE_WORDS):
        d = max(d, 1.8)
    return round(min(d, 10.0), 3)


def M(name, fam, o, cut, res, env, key, adsr, **x):
    v = osc(o)
    v.update(cutoff=cut, reso=res, envAmt=env, key=key, attack=adsr[0], decay=ring(name, fam, adsr[1], adsr[2]), sustain=adsr[2],
             release=round(adsr[3] * 1.5, 3))
    v.update(x)
    return (name, fam, v)


# effect shorthands
DL = lambda mix, t=7, fb=0.4: dict(fxDelay=mix, fxTime=t, fxFeedback=fb)
# reverb: the space grows with the send (small room at low mix, hall at high), unless set
RV = lambda mix, size=None, tone=0.55, pre=None: dict(fxReverb=mix, revSize=min(0.9, 0.25 + 1.1 * mix) if size is None else size,
                                                       revTone=tone, revPre=round(0.008 + 0.05 * mix, 4) if pre is None else pre)
CH = lambda mix: dict(fxChorus=mix)
LF = lambda shape, rate, cut=0.0, pwm=0.0, fade=0.0: dict(lfoShape=shape, lfoRate=rate, lfoCut=cut, lfoPwm=pwm, lfoFade=fade)
# tempo-synced LFO: division index (1 = 1 bar, 2 = 1/2, 4 = 1/4, 5 = 1/4 T, 6 = 1/8 D, 7 = 1/8, 8 = 1/8 T,
# 9 = 3/16, 10 = 1/16, 11 = 1/16 T); shapes 0 Tri, 1 Sq, 2 Rnd, 3 Noise
SY = lambda shape, div, cut=0.0, pwm=0.0, fade=0.0: dict(lfoShape=shape, lfoSync=1, lfoDiv=div, lfoCut=cut, lfoPwm=pwm, lfoFade=fade)

MONO = [
    # ---------------------------------------------------------------- Bass
    M("Round Bass", "Bass", "saw+sub0:0.6", 320, 0.25, 0.55, 0.4, (0.002, 0.35, 0.3, 0.08)),
    M("Square Thump", "Bass", "sq+sub1:0.8", 180, 0.1, 0.35, 0.3, (0.002, 0.5, 0.6, 0.1)),
    M("Rubber Bass", "Bass", "saw", 240, 0.62, 0.7, 0.5, (0.002, 0.22, 0.25, 0.06), drive=0.15),
    M("Hollow Pulse Bass", "Bass", "pul:0.18+sub0:0.5", 420, 0.3, 0.45, 0.4, (0.002, 0.4, 0.5, 0.08), **CH(0.25)),
    M("Funk Bass", "Bass", "saw+pul:0.3", 280, 0.45, 0.8, 0.6, (0.001, 0.18, 0.15, 0.05)),
    M("Deep Pluck Bass", "Bass", "sq+sub2:0.7", 150, 0.35, 0.65, 0.3, (0.001, 0.3, 0.0, 0.12)),
    M("Warm Fingered Bass", "Bass", "saw+sub0:0.4", 600, 0.1, 0.25, 0.5, (0.004, 0.6, 0.55, 0.12), **RV(0.08)),
    M("Gated Square Bass", "Bass", "sq+sub0:0.5", 900, 0.15, 0.1, 0.4, (0.002, 0.1, 1.0, 0.03), ampMode=1),
    M("PWM Bass", "Bass", "pul:0.35+pwm:0.45+sub0:0.4", 500, 0.2, 0.4, 0.4, (0.002, 0.4, 0.45, 0.1), **LF(0, 0.7)),
    M("Snappy Bass", "Bass", "saw", 160, 0.55, 0.95, 0.5, (0.001, 0.12, 0.1, 0.05)),
    M("Dub Bass", "Bass", "sq+sub1:0.6", 260, 0.2, 0.3, 0.2, (0.003, 0.8, 0.7, 0.2), **DL(0.18, 6, 0.45)),
    M("Envelope PWM Bass", "Bass", "pul:0.12+pwm:0.6:env+sub0:0.5", 700, 0.25, 0.35, 0.4, (0.002, 0.45, 0.35, 0.08)),
    M("Noisy Bass", "Bass", "saw+noise:0.25+sub0:0.4", 380, 0.4, 0.6, 0.5, (0.001, 0.25, 0.3, 0.07)),
    M("Driven Saw Bass", "Bass", "saw+sub0:0.5", 450, 0.3, 0.5, 0.4, (0.002, 0.3, 0.4, 0.08), drive=0.5),
    M("Soft Low Bass", "Bass", "sq", 220, 0.0, 0.15, 0.2, (0.02, 0.9, 0.8, 0.25)),
    M("Resonant Click Bass", "Bass", "pul:0.25", 300, 0.75, 0.75, 0.6, (0.001, 0.15, 0.2, 0.05)),
    # ---------------------------------------------------------------- Sub
    M("Pure Sub", "Sub", "sub0:1", 140, 0.0, 0.1, 0.2, (0.003, 1.0, 1.0, 0.12)),
    M("Two Octave Sub", "Sub", "sub1:1", 120, 0.0, 0.05, 0.2, (0.003, 1.0, 1.0, 0.15)),
    M("Pulse Sub", "Sub", "sub2:1", 180, 0.0, 0.2, 0.2, (0.003, 1.0, 1.0, 0.12)),
    M("Sub and Click", "Sub", "sub0:1+saw:0.2", 160, 0.2, 0.9, 0.3, (0.001, 0.08, 0.8, 0.1)),
    M("Round Sub Swell", "Sub", "sub0:1+sq:0.3", 110, 0.0, 0.3, 0.2, (0.35, 1.0, 1.0, 0.4)),
    M("Sub Boom", "Sub", "sub1:1+sq:0.4", 200, 0.3, 0.5, 0.2, (0.001, 0.6, 0.0, 0.5)),
    M("Warm Sub", "Sub", "sub0:0.9+saw:0.3", 220, 0.1, 0.2, 0.3, (0.004, 1.0, 0.9, 0.15), drive=0.25),
    M("Gated Sub", "Sub", "sub0:1", 160, 0.0, 0.0, 0.2, (0.002, 0.1, 1.0, 0.02), ampMode=1),
    M("Sub Pulse Pluck", "Sub", "sub2:0.9+pul:0.2", 260, 0.3, 0.6, 0.4, (0.001, 0.25, 0.1, 0.1)),
    M("Wobbly Sub", "Sub", "sub0:1+sq:0.4", 150, 0.4, 0.0, 0.2, (0.003, 1.0, 1.0, 0.12), **LF(0, 2.0, 0.35)),
    M("Saturated Sub", "Sub", "sub0:1+sq:0.5", 260, 0.2, 0.2, 0.3, (0.003, 1.0, 1.0, 0.12), drive=0.7),
    M("Sub Thud", "Sub", "sub1:1", 300, 0.0, 0.8, 0.2, (0.001, 0.2, 0.0, 0.2)),
    M("Breathing Sub", "Sub", "sub0:1+noise:0.15", 170, 0.15, 0.1, 0.2, (0.05, 1.0, 1.0, 0.3), **LF(0, 0.4, 0.2)),
    M("Sub Organ", "Sub", "sub0:0.8+sq:0.5+sub1:0.6", 700, 0.0, 0.0, 0.3, (0.004, 0.2, 1.0, 0.08), **CH(0.3)),
    M("Plucked Sub", "Sub", "sub0:1+saw:0.4", 400, 0.5, 0.7, 0.4, (0.001, 0.35, 0.0, 0.3), **RV(0.12)),
    M("Sub Drone", "Sub", "sub1:1+sq:0.5", 240, 0.3, 0.0, 0.2, (0.6, 1.0, 1.0, 1.5), **LF(2, 0.6, 0.2), **RV(0.25)),
    # ---------------------------------------------------------------- Lead
    M("Bright Lead", "Lead", "saw+pul:0.3", 2400, 0.35, 0.3, 0.8, (0.01, 0.6, 0.8, 0.25), **DL(0.3, 7, 0.4), **RV(0.15)),
    M("Hollow Lead", "Lead", "pul:0.14+pwm:0.35", 1600, 0.2, 0.15, 0.7, (0.02, 0.5, 0.9, 0.3), **LF(0, 0.8), **RV(0.25)),
    M("Talking Lead", "Lead", "saw", 700, 0.8, 0.4, 0.6, (0.01, 0.6, 0.8, 0.2), **LF(0, 5.0, 0.3), **DL(0.2, 6, 0.35)),
    M("Singing Square", "Lead", "sq", 1800, 0.5, 0.2, 0.8, (0.03, 0.5, 0.85, 0.35), **DL(0.25, 9, 0.45), **RV(0.3)),
    M("Screaming Lead", "Lead", "saw+sub0:0.3", 1200, 0.85, 0.5, 0.9, (0.005, 0.4, 0.7, 0.2), drive=0.6, **DL(0.25, 7, 0.4)),
    M("Soft Flute Lead", "Lead", "sq+noise:0.12", 1100, 0.1, 0.15, 0.7, (0.08, 0.5, 0.8, 0.35), **LF(0, 4.5, 0.08, fade=1.0), **RV(0.35)),
    M("Thin Pulse Lead", "Lead", "pul:0.08", 3000, 0.25, 0.2, 0.8, (0.005, 0.4, 0.8, 0.2), **DL(0.35, 6, 0.5)),
    M("Wah Lead", "Lead", "saw", 600, 0.7, 0.8, 0.7, (0.01, 0.35, 0.5, 0.2), **DL(0.2, 7, 0.35), **RV(0.2)),
    M("Buzz Lead", "Lead", "saw+pul:0.5", 3800, 0.15, 0.1, 0.9, (0.004, 0.3, 0.9, 0.15), drive=0.35, **CH(0.2)),
    M("Glassy Lead", "Lead", "pul:0.25", 4000, 0.6, 0.35, 1.0, (0.002, 0.25, 0.6, 0.4), **DL(0.3, 9, 0.5), **RV(0.3)),
    M("Pulse Width Lead", "Lead", "pul:0.3+pwm:0.5", 2200, 0.3, 0.2, 0.8, (0.01, 0.5, 0.8, 0.3), **LF(0, 2.5), **DL(0.2, 7)),
    M("Dark Lead", "Lead", "saw+sq:0.5", 800, 0.4, 0.3, 0.7, (0.02, 0.7, 0.8, 0.3), **RV(0.3)),
    M("Fat Unison Lead", "Lead", "saw+sub0:0.5", 1500, 0.3, 0.3, 0.7, (0.01, 0.5, 0.85, 0.25), voiceMode=1, **CH(0.3), **DL(0.2)),
    M("Stepped Filter Lead", "Lead", "saw", 900, 0.6, 0.2, 0.7, (0.01, 0.5, 0.9, 0.2), **LF(2, 8.0, 0.35), **DL(0.25, 10, 0.4)),
    M("Envelope Sweep Lead", "Lead", "saw+pul:0.4", 400, 0.55, 0.7, 0.7, (0.01, 1.2, 0.4, 0.3), **RV(0.25)),
    M("Whistle Lead", "Lead", "sq:0.2", 1000, 0.92, 0.1, 1.0, (0.04, 0.5, 0.9, 0.4), **DL(0.3, 6, 0.45), **RV(0.35)),
    # ---------------------------------------------------------------- Pluck
    M("Resonant Pluck", "Pluck", "saw", 260, 0.7, 0.85, 1.0, (0.001, 0.25, 0.0, 0.25), **DL(0.25, 9, 0.45), **RV(0.2)),
    M("Glass Pluck", "Pluck", "pul:0.3", 600, 0.55, 0.9, 1.0, (0.001, 0.15, 0.0, 0.3), **RV(0.3)),
    M("Wooden Pluck", "Pluck", "sq", 450, 0.3, 0.7, 0.8, (0.001, 0.12, 0.0, 0.1), **RV(0.2)),
    M("Echo Pluck", "Pluck", "saw+sq:0.4", 700, 0.45, 0.8, 1.0, (0.001, 0.2, 0.0, 0.2), **DL(0.4, 6, 0.55)),
    M("Pizzicato", "Pluck", "saw", 1200, 0.15, 0.6, 0.9, (0.002, 0.18, 0.0, 0.15), **RV(0.3)),
    M("Plinky Square", "Pluck", "sq", 1500, 0.4, 0.5, 1.0, (0.001, 0.08, 0.0, 0.12), **DL(0.3, 10, 0.4)),
    M("Muted Pluck", "Pluck", "saw+sub0:0.4", 350, 0.2, 0.5, 0.7, (0.001, 0.15, 0.0, 0.08)),
    M("Long Pluck", "Pluck", "pul:0.35", 500, 0.5, 0.7, 0.9, (0.001, 0.8, 0.0, 0.6), **CH(0.3), **RV(0.35)),
    M("Noise Pluck", "Pluck", "noise:0.6+saw:0.5", 900, 0.65, 0.8, 1.0, (0.001, 0.1, 0.0, 0.15), **RV(0.25)),
    M("Kalimba Pluck", "Pluck", "sq:0.6+sub0:0.3", 1800, 0.25, 0.3, 1.0, (0.001, 0.3, 0.0, 0.4), **RV(0.3), **DL(0.2, 7, 0.3)),
    M("Snap Pluck", "Pluck", "saw", 180, 0.8, 1.0, 1.0, (0.001, 0.09, 0.0, 0.08), **DL(0.2, 6, 0.4)),
    M("Pulse Harp", "Pluck", "pul:0.15", 2200, 0.2, 0.4, 1.0, (0.001, 0.6, 0.0, 0.8), **CH(0.4), **RV(0.4)),
    M("Driven Pluck", "Pluck", "saw+sub0:0.3", 400, 0.5, 0.8, 0.8, (0.001, 0.2, 0.0, 0.15), drive=0.6, **DL(0.2, 9)),
    M("Bubble Pluck", "Pluck", "sq", 300, 0.85, 0.6, 1.0, (0.001, 0.15, 0.0, 0.2), **RV(0.2), **DL(0.25, 10, 0.35)),
    M("Soft Bell Pluck", "Pluck", "sq:0.5+pul:0.2", 2600, 0.35, 0.2, 1.0, (0.002, 0.9, 0.0, 1.2), **CH(0.3), **RV(0.45)),
    M("Staccato Saw", "Pluck", "saw", 2000, 0.1, 0.4, 0.8, (0.001, 0.07, 0.0, 0.06), **DL(0.3, 7, 0.4)),
    # ---------------------------------------------------------------- Wobble
    # every wobble differs in what moves (filter, pulse width, gate), how (shape, division) and the voice
    # under it (dark sub, open saw, screaming resonance, pluck, swell)
    M("Eighth Note Wobble", "Wobble", "saw+sub0:0.6", 220, 0.6, 0.0, 0.3, (0.003, 0.4, 1.0, 0.1), drive=0.3, **SY(0, 7, 0.7)),
    M("Half-Time Wobble", "Wobble", "sq+sub1:0.8", 150, 0.4, 0.0, 0.2, (0.003, 0.4, 1.0, 0.12), **SY(0, 4, 0.9)),
    M("Triplet Wobble", "Wobble", "saw", 380, 0.75, 0.0, 0.4, (0.003, 0.4, 1.0, 0.1), drive=0.5, **SY(0, 8, 0.5)),
    M("Chopped Pulse Wobble", "Wobble", "pul:0.35+sub0:0.5", 260, 0.5, 0.0, 0.3, (0.003, 0.4, 1.0, 0.08), **SY(1, 10, 0.65)),
    M("Random Step Bass", "Wobble", "saw+sub0:0.4", 420, 0.82, 0.0, 0.4, (0.003, 0.4, 1.0, 0.1), **SY(2, 10, 0.55)),
    M("Noise Modulated Bass", "Wobble", "sq", 520, 0.6, 0.0, 0.4, (0.003, 0.4, 1.0, 0.1), drive=0.4, **LF(3, 18.0, 0.35)),
    M("Screaming Wobble", "Wobble", "saw", 650, 0.93, 0.0, 0.5, (0.003, 0.4, 1.0, 0.1), **SY(0, 6, 0.6), **DL(0.2, 6, 0.35)),
    M("Pulse Width Wobble", "Wobble", "pul:0.25+pwm:0.75+sub0:0.4", 950, 0.3, 0.0, 0.4, (0.003, 0.4, 1.0, 0.1), **SY(0, 5, 0.2)),
    M("Sub Wobble", "Wobble", "sub1:1.0+sq:0.3", 120, 0.2, 0.0, 0.1, (0.003, 0.4, 1.0, 0.15), **SY(0, 2, 0.9)),
    M("Fast Yoy Bass", "Wobble", "saw+pul:0.5", 300, 0.7, 0.0, 0.3, (0.003, 0.4, 1.0, 0.1), drive=0.6, **SY(0, 11, 0.55)),
    M("Swelling Wobble", "Wobble", "saw+sub0:0.5", 240, 0.65, 0.0, 0.3, (0.3, 0.4, 1.0, 0.3), **SY(0, 7, 0.7, fade=2.0), **RV(0.2)),
    M("Plucked Wobble", "Wobble", "saw", 450, 0.7, 0.6, 0.5, (0.001, 0.25, 0.15, 0.15), **SY(0, 10, 0.4), **DL(0.2, 9, 0.35)),
    M("Square LFO Wobble", "Wobble", "sq+sub0:0.7", 200, 0.72, 0.0, 0.3, (0.003, 0.4, 1.0, 0.1), **SY(1, 7, 0.7)),
    M("Dub Bar Wobble", "Wobble", "sq+sub0:0.6", 300, 0.4, 0.0, 0.3, (0.01, 0.4, 1.0, 0.25), **SY(0, 1, 0.6), **DL(0.3, 6, 0.5), **RV(0.2)),
    M("Talking Bass", "Wobble", "saw+noise:0.1", 720, 0.88, 0.0, 0.6, (0.003, 0.4, 1.0, 0.1), **SY(0, 3, 0.4)),
    M("Overdriven Wobble", "Wobble", "saw+sub0:0.5", 280, 0.6, 0.0, 0.3, (0.003, 0.4, 1.0, 0.1), drive=1.0, **SY(0, 9, 0.6), **CH(0.3)),
    # ---------------------------------------------------------------- Driven
    M("Hard Driven Saw", "Driven", "saw+pul:0.3", 500, 0.75, 0.7, 0.4, (0.002, 0.3, 0.5, 0.1), drive=0.85),
    M("Dirty Pulse", "Driven", "pul:0.2+pwm:0.4:env", 800, 0.5, 0.5, 0.4, (0.003, 0.5, 0.7, 0.12), drive=0.7),
    M("Snarling Saw", "Driven", "saw", 650, 0.8, 0.6, 0.5, (0.001, 0.25, 0.6, 0.1), drive=1.0),
    M("Fuzz Square", "Driven", "sq+sub0:0.6", 900, 0.4, 0.3, 0.4, (0.002, 0.4, 0.8, 0.1), drive=0.95),
    M("Rasp", "Driven", "saw+noise:0.3", 1200, 0.6, 0.4, 0.5, (0.002, 0.3, 0.7, 0.1), drive=0.8),
    M("Barking Bass", "Driven", "saw+sub0:0.5", 300, 0.85, 0.9, 0.4, (0.001, 0.18, 0.3, 0.08), drive=0.75),
    M("Angry Lead", "Driven", "saw+pul:0.5", 1800, 0.7, 0.4, 0.8, (0.005, 0.4, 0.8, 0.2), drive=0.9, **DL(0.2, 7)),
    M("Sputter", "Driven", "pul:0.1", 700, 0.6, 0.5, 0.5, (0.002, 0.3, 0.6, 0.1), drive=0.8, **LF(2, 14.0, 0.3)),
    M("Gravel Bass", "Driven", "saw+sub1:0.6", 400, 0.7, 0.6, 0.3, (0.002, 0.35, 0.6, 0.1), drive=0.9, **LF(3, 18.0, 0.15)),
    M("Saturated Square", "Driven", "sq", 1500, 0.3, 0.2, 0.6, (0.003, 0.5, 0.9, 0.15), drive=1.0, **CH(0.2)),
    M("Screech", "Driven", "saw", 2000, 0.9, 0.3, 0.9, (0.005, 0.3, 0.8, 0.15), drive=0.7, **DL(0.25, 9, 0.4)),
    M("Low Roar", "Driven", "sq+saw+sub0:0.8", 250, 0.6, 0.5, 0.2, (0.01, 0.6, 0.8, 0.2), drive=0.85),
    M("Distorted Pluck", "Driven", "saw", 500, 0.7, 0.9, 0.7, (0.001, 0.2, 0.0, 0.15), drive=0.9, **RV(0.15)),
    M("Buzzsaw", "Driven", "saw+pul:0.15", 3000, 0.2, 0.1, 0.7, (0.002, 0.3, 0.9, 0.1), drive=0.8),
    M("Chewy Bass", "Driven", "pul:0.3+sub0:0.6", 350, 0.75, 0.8, 0.4, (0.002, 0.4, 0.4, 0.1), drive=0.6, **LF(0, 6.0, 0.2)),
    M("Tearing Lead", "Driven", "saw+noise:0.2", 1500, 0.8, 0.5, 0.8, (0.003, 0.4, 0.7, 0.2), drive=1.0, **DL(0.2, 6), **RV(0.15)),
    # ---------------------------------------------------------------- Stab (chord memory)
    M("Minor Stab", "Stab", "saw", 1400, 0.2, 0.5, 0.5, (0.002, 0.4, 0.2, 0.3), voiceMode=3, chord=1, **CH(0.4), **DL(0.25, 7, 0.35)),
    M("Fifths Stab", "Stab", "pul:0.35", 900, 0.4, 0.6, 0.5, (0.002, 0.3, 0.0, 0.25), voiceMode=3, chord=3, **RV(0.3)),
    M("Major Organ Stab", "Stab", "sq+sub0:0.4", 2500, 0.1, 0.1, 0.5, (0.002, 0.2, 1.0, 0.1), voiceMode=3, chord=0, **CH(0.4)),
    M("House Stab", "Stab", "saw+sq:0.5", 1100, 0.35, 0.55, 0.5, (0.001, 0.25, 0.0, 0.2), voiceMode=3, chord=4, **RV(0.3), **DL(0.2, 9, 0.3)),
    M("Sus Stab", "Stab", "saw", 1800, 0.3, 0.4, 0.5, (0.003, 0.5, 0.3, 0.4), voiceMode=3, chord=2, **CH(0.3), **RV(0.3)),
    M("Dub Chord", "Stab", "sq", 700, 0.4, 0.5, 0.5, (0.002, 0.2, 0.0, 0.2), voiceMode=3, chord=1, **DL(0.45, 6, 0.6), **RV(0.3)),
    M("Seventh Pad Stab", "Stab", "saw+pul:0.3", 1200, 0.2, 0.3, 0.5, (0.08, 0.8, 0.6, 0.8), voiceMode=3, chord=4, **CH(0.5), **RV(0.4)),
    M("Major Seventh", "Stab", "pul:0.25", 2000, 0.15, 0.3, 0.5, (0.01, 0.6, 0.4, 0.5), voiceMode=3, chord=5, **CH(0.4), **RV(0.35)),
    M("Ninth Chord", "Stab", "saw", 1600, 0.25, 0.4, 0.5, (0.004, 0.5, 0.3, 0.5), voiceMode=3, chord=6, **RV(0.4)),
    M("Fourths Stack", "Stab", "sq", 1300, 0.3, 0.45, 0.5, (0.002, 0.35, 0.1, 0.3), voiceMode=3, chord=7, **DL(0.3, 7, 0.4)),
    M("Resonant Chord", "Stab", "saw", 600, 0.75, 0.7, 0.5, (0.001, 0.25, 0.0, 0.25), voiceMode=3, chord=1, **RV(0.25)),
    M("Wobble Chord", "Stab", "saw+sub0:0.3", 500, 0.5, 0.2, 0.5, (0.003, 0.4, 1.0, 0.2), voiceMode=3, chord=4, **LF(0, 4.0, 0.4)),
    M("Driven Power Chord", "Stab", "saw", 1500, 0.3, 0.3, 0.5, (0.002, 0.5, 0.6, 0.2), voiceMode=3, chord=3, drive=0.7),
    M("Swelling Chord", "Stab", "pul:0.3+pwm:0.3", 900, 0.2, 0.4, 0.5, (0.5, 1.0, 0.8, 1.0), voiceMode=3, chord=0, **LF(0, 0.5), **CH(0.4), **RV(0.4)),
    M("Plucked Chord", "Stab", "saw", 2400, 0.2, 0.5, 0.5, (0.001, 0.3, 0.0, 0.4), voiceMode=3, chord=5, **DL(0.3, 9, 0.45), **RV(0.3)),
    M("Gated Chord", "Stab", "sq+saw:0.5", 1800, 0.2, 0.0, 0.5, (0.002, 0.1, 1.0, 0.04), voiceMode=3, chord=1, ampMode=1, **CH(0.3)),
    # ---------------------------------------------------------------- FX
    M("Falling Zap", "FX", "saw", 6000, 0.9, -0.9, 1.0, (0.001, 0.15, 0.0, 0.1)),
    M("Noise Sweep", "FX", "noise:1", 300, 0.8, 0.9, 0.0, (0.6, 1.5, 0.0, 1.0), **RV(0.4)),
    M("Laser", "FX", "sq", 9000, 0.95, -1.0, 1.0, (0.001, 0.08, 0.0, 0.1), **DL(0.3, 10, 0.5)),
    M("Wind", "FX", "noise:1", 800, 0.7, 0.0, 0.0, (1.0, 1.0, 1.0, 1.5), **LF(3, 0.3, 0.5), **RV(0.5)),
    M("Siren Filter", "FX", "saw", 800, 0.9, 0.0, 0.5, (0.01, 0.5, 1.0, 0.4), **LF(0, 0.4, 0.8), **RV(0.3)),
    M("Computer Bleeps", "FX", "sq", 2000, 0.6, 0.0, 1.0, (0.001, 0.4, 1.0, 0.2), **LF(2, 12.0, 0.6), **DL(0.3, 10, 0.5)),
    M("Rising Whoosh", "FX", "noise:1", 200, 0.75, 0.8, 0.0, (1.5, 2.0, 1.0, 0.8), **RV(0.45)),
    M("Ping", "FX", "sq:0.1", 3000, 0.97, 0.2, 1.0, (0.001, 0.3, 0.0, 0.6), **DL(0.4, 6, 0.6), **RV(0.4)),
    M("Static Burst", "FX", "noise:1", 2500, 0.4, 0.3, 0.0, (0.001, 0.25, 0.0, 0.2), **LF(3, 25.0, 0.4)),
    M("Underwater Bubbles", "FX", "sq", 600, 0.9, 0.0, 1.0, (0.002, 0.3, 1.0, 0.3), **LF(2, 9.0, 0.7), **RV(0.35)),
    M("Clicky Drop", "FX", "saw+noise:0.3", 4000, 0.8, -1.0, 0.8, (0.001, 0.4, 0.0, 0.2), drive=0.5),
    M("Thunder Rumble", "FX", "noise:1+sub1:0.5", 150, 0.5, 0.3, 0.0, (0.01, 2.5, 0.0, 2.0), **RV(0.5)),
    M("Robot Voice", "FX", "pul:0.1", 900, 0.85, 0.0, 0.8, (0.01, 0.5, 1.0, 0.2), **LF(1, 6.0, 0.5)),
    M("Alarm", "FX", "sq", 1500, 0.3, 0.0, 1.0, (0.002, 0.5, 1.0, 0.1), **LF(1, 3.0, 0.5), **RV(0.2)),
    M("Dark Drone", "FX", "saw+sub1:0.7+noise:0.1", 250, 0.6, 0.0, 0.3, (2.0, 2.0, 1.0, 3.0), **LF(0, 0.15, 0.4), **CH(0.4), **RV(0.5)),
    M("Zipper", "FX", "pul:0.2", 2500, 0.8, -0.8, 1.0, (0.001, 0.05, 0.0, 0.05), **DL(0.35, 12, 0.5)),
]
