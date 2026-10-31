"""Poly factory bank: 8 families × 16 hand-designed patches, DCO engine (1982 DCO polysynth model:
DCO, 4-position HPF, 4-pole OTA filter, BBD chorus) and VA engine (compact paraphonic VCO synth:
voicing modes, 12 dB peak filter, tape echo).

Oscillator tokens: dco:saw | dco:pulse:<pw> | dco:both:<pw>, va:saw | va:pulse:<pw> | va:tri,
optional +sub:<level> +noise:<level> +pwm:<depth> (LFO) +hpf:<0..3>.
"""


def osc(spec):
    d = {}
    for tok in spec.split("+"):
        k, *a = tok.split(":")
        if k == "dco":
            d.update(engine=0, dcoWave={"pulse": 0, "saw": 1, "both": 2}[a[0]])
            if len(a) > 1: d["dcoPw"] = float(a[1])
        elif k == "va":
            d.update(engine=1, vaWave={"saw": 0, "pulse": 1, "tri": 2}[a[0]], filterMode=1)
            if len(a) > 1: d["vaPw"] = float(a[1])
        elif k == "sub": d["sub"] = float(a[0])
        elif k == "noise": d["noise"] = float(a[0])
        elif k == "pwm":
            d["lfoPwm"] = float(a[0])
            d["dcoPwm" if d.get("engine", 0) == 0 else "vaPwm"] = float(a[0])
        elif k == "hpf": d["hpf"] = int(a[0])
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


def P(name, fam, o, cut, res, env, key, fenv, aenv, **x):
    v = osc(o)
    pf = aenv[2] <= 0.3   # percussive: the filter sweep stretches less than the amp ring
    if fam == "Brass" and aenv[0] < 0.03:   # brass always speaks with a short swell, never an instant click
        aenv = (0.035,) + tuple(aenv[1:]); fenv = (max(fenv[0], 0.06),) + tuple(fenv[1:])
    v.update(cutoff=cut, reso=res, envAmt=env, key=key,
             fAttack=fenv[0], fDecay=round(fenv[1] * 1.6, 3) if pf else fenv[1], fSustain=fenv[2], fRelease=round(fenv[3] * 1.5, 3),
             aAttack=aenv[0], aDecay=ring(name, fam, aenv[1], aenv[2]), aSustain=aenv[2], aRelease=round(aenv[3] * 1.5, 3))
    v.update(x)
    return (name, fam, v)


CH = lambda mode, mix=0.5: dict(chorusMode=mode, chorusMix=mix)
DL = lambda mix, div=6, fb=0.4, tone=0.5: dict(delayMix=mix, delayDiv=div, delayFb=fb, delayTone=tone)
# reverb: the space grows with the send (small room at low mix, hall at high), unless set
RV = lambda mix, size=None, tone=0.55, pre=None: dict(reverb=mix, revSize=min(0.9, 0.25 + 1.1 * mix) if size is None else size,
                                                       revTone=tone, revPre=round(0.008 + 0.05 * mix, 4) if pre is None else pre)
LF = lambda shape, rate, cut=0.0, pwm=None, delay=0.0: dict(lfoShape=shape, lfoRate=rate, lfoCut=cut, lfoDelay=delay, **({} if pwm is None else dict(lfoPwm=pwm)))
ARP = lambda mode, rng, rate, gate=0.5: dict(arpMode=mode, arpRange=rng, arpRate=rate, arpGate=gate)
DR = lambda amt, kind=0, tone=0.6: dict(driveAmt=amt, driveType=kind, driveTone=tone)
SHARED = dict(filterMode=0)
PAD_A = (0.9, 1.0, 1.0, 1.8)

POLY = [
    # ---------------------------------------------------------------- Pad
    P("Warm Chorus Pad", "Pad", "dco:both:0.4+pwm:0.3+sub:0.3", 1400, 0.1, 0.2, 0.4, (1.2, 2.0, 0.7, 1.5), PAD_A, **LF(0, 0.6), **CH(2, 0.6), **RV(0.35)),
    P("Silk Pad", "Pad", "dco:saw+hpf:2", 2600, 0.05, 0.1, 0.4, (0.8, 2.0, 0.9, 2.0), (1.2, 1.0, 1.0, 2.5), **CH(3, 0.7), **RV(0.45)),
    P("Dark Analog Pad", "Pad", "dco:saw+sub:0.5", 500, 0.35, 0.35, 0.3, (1.5, 2.5, 0.5, 2.0), (0.8, 1.0, 1.0, 2.0), **CH(1, 0.6), **RV(0.4)),
    P("Breathing Pad", "Pad", "dco:pulse:0.35+pwm:0.4", 900, 0.3, 0.2, 0.4, (0.5, 2.0, 0.8, 1.5), PAD_A, **LF(0, 0.3, 0.3), **CH(2, 0.6), **RV(0.4)),
    P("Glass Pad", "Pad", "va:tri", 4000, 0.2, 0.1, 0.5, (0.5, 1.0, 1.0, 1.5), (1.5, 1.0, 1.0, 2.5), **DL(0.25, 4, 0.5), **RV(0.5)),
    P("Paraphonic Pad", "Pad", "va:saw", 600, 0.45, 0.4, 0.3, (1.5, 2.0, 0.5, 1.5), (1.0, 1.0, 1.0, 2.0), **SHARED, **DL(0.3, 4, 0.5), **RV(0.35)),
    P("Swelling Strings Pad", "Pad", "dco:saw+hpf:2", 1800, 0.1, 0.4, 0.4, (2.5, 3.0, 0.6, 2.0), (2.0, 1.0, 1.0, 2.5), **CH(3, 0.7), **RV(0.5)),
    P("Hollow Pulse Pad", "Pad", "va:pulse:0.2+pwm:0.35", 1600, 0.25, 0.15, 0.4, (0.8, 2.0, 0.8, 1.5), PAD_A, **LF(0, 0.5), **DL(0.2, 4, 0.45), **RV(0.4)),
    P("Octave Pad", "Pad", "va:saw", 1200, 0.2, 0.3, 0.4, (1.0, 2.0, 0.7, 1.5), PAD_A, voiceMode=3, **DL(0.2, 3, 0.5), **RV(0.45)),
    P("Fifths Pad", "Pad", "va:pulse:0.4", 1000, 0.3, 0.3, 0.4, (1.2, 2.0, 0.6, 1.8), PAD_A, voiceMode=4, **RV(0.5)),
    P("Sweeping Pad", "Pad", "dco:both:0.3+sub:0.2", 700, 0.55, 0.5, 0.4, (3.0, 3.0, 0.3, 2.0), (1.0, 1.0, 1.0, 2.0), **CH(2, 0.6), **RV(0.45)),
    P("Moving Filter Pad", "Pad", "dco:saw", 900, 0.4, 0.1, 0.4, (0.5, 1.0, 1.0, 1.5), PAD_A, **LF(0, 0.15, 0.35), **CH(1, 0.6), **RV(0.4)),
    P("Noise Air Pad", "Pad", "dco:saw+noise:0.25+hpf:3", 3000, 0.1, 0.1, 0.3, (1.0, 2.0, 0.8, 2.0), (1.5, 1.0, 1.0, 3.0), **CH(3, 0.7), **RV(0.55)),
    P("Ring Pad", "Pad", "va:saw", 2200, 0.15, 0.1, 0.4, (1.0, 1.0, 1.0, 2.0), (1.2, 1.0, 1.0, 2.5), voiceMode=5, **DL(0.25, 3, 0.5), **RV(0.5)),
    P("Warm Sub Pad", "Pad", "dco:pulse:0.5+sub:0.6+hpf:0", 800, 0.15, 0.2, 0.3, (1.0, 2.0, 0.8, 1.5), PAD_A, **CH(1, 0.5), **RV(0.35)),
    P("Distant Pad", "Pad", "va:tri+noise:0.1", 1500, 0.1, 0.0, 0.4, (1.0, 1.0, 1.0, 2.0), (2.5, 1.0, 1.0, 4.0), **DL(0.35, 4, 0.6, 0.3), **RV(0.6)),
    # ---------------------------------------------------------------- Strings
    P("String Ensemble", "Strings", "dco:saw+hpf:2", 3200, 0.0, 0.1, 0.5, (0.3, 1.0, 1.0, 0.9), (0.35, 1.0, 0.9, 0.9), **CH(3, 0.7), **RV(0.35)),
    P("Bowed Strings", "Strings", "dco:saw+hpf:1", 2200, 0.1, 0.3, 0.5, (0.4, 0.8, 0.8, 0.8), (0.25, 0.8, 0.9, 0.7), **CH(2, 0.6), **RV(0.35)),
    P("Chamber Strings", "Strings", "dco:saw+hpf:2", 1800, 0.05, 0.25, 0.6, (0.15, 0.6, 0.7, 0.5), (0.12, 0.6, 0.85, 0.5), **CH(1, 0.5), **RV(0.4)),
    P("Tremolo Strings", "Strings", "dco:saw+hpf:2", 2600, 0.05, 0.1, 0.5, (0.2, 1.0, 1.0, 0.8), (0.2, 1.0, 0.9, 0.8), **LF(0, 6.0, 0.25), **CH(3, 0.6), **RV(0.4)),
    P("Pizzicato Strings", "Strings", "dco:saw+hpf:2", 1600, 0.1, 0.6, 0.7, (0.001, 0.25, 0.0, 0.25), (0.001, 0.3, 0.0, 0.3), **CH(3, 0.6), **RV(0.45)),
    P("String Machine Wash", "Strings", "dco:both:0.45+hpf:3", 4000, 0.0, 0.0, 0.5, (0.3, 1.0, 1.0, 1.0), (0.5, 1.0, 1.0, 1.2), **CH(3, 0.8), **RV(0.5)),
    P("Dark Cellos", "Strings", "dco:saw+sub:0.3+hpf:1", 800, 0.15, 0.3, 0.4, (0.3, 1.0, 0.7, 0.8), (0.3, 1.0, 0.9, 0.8), **CH(2, 0.5), **RV(0.35)),
    P("Slow Strings", "Strings", "va:saw", 2000, 0.1, 0.2, 0.5, (1.5, 2.0, 0.8, 1.5), (1.5, 1.0, 1.0, 1.8), voiceMode=3, **DL(0.15, 4, 0.4), **RV(0.5)),
    P("Bright Violins", "Strings", "dco:saw+hpf:3", 5000, 0.1, 0.1, 0.6, (0.15, 1.0, 1.0, 0.6), (0.18, 1.0, 0.9, 0.6), **LF(0, 5.5, 0.08, delay=0.8), **CH(2, 0.6), **RV(0.4)),
    P("Synth Strings", "Strings", "va:saw", 2400, 0.25, 0.3, 0.5, (0.2, 1.0, 0.8, 0.8), (0.2, 1.0, 0.9, 0.8), **DL(0.2, 6, 0.4), **RV(0.4)),
    P("Pulse Strings", "Strings", "dco:pulse:0.4+pwm:0.3+hpf:2", 3000, 0.05, 0.1, 0.5, (0.3, 1.0, 1.0, 0.9), (0.3, 1.0, 0.9, 0.9), **LF(0, 4.0), **CH(2, 0.6), **RV(0.4)),
    P("Swelling Section", "Strings", "dco:saw+hpf:2", 1200, 0.1, 0.5, 0.5, (1.2, 1.5, 0.6, 1.0), (0.9, 1.0, 1.0, 1.0), **CH(3, 0.7), **RV(0.45)),
    P("Fifth Strings", "Strings", "va:saw", 1800, 0.1, 0.2, 0.5, (0.4, 1.0, 0.8, 1.0), (0.4, 1.0, 0.9, 1.0), voiceMode=4, **RV(0.45)),
    P("Thin Strings", "Strings", "dco:saw+hpf:3", 6000, 0.0, 0.0, 0.6, (0.1, 1.0, 1.0, 0.6), (0.25, 1.0, 0.9, 0.7), **CH(1, 0.6), **RV(0.35)),
    P("Stacked Strings", "Strings", "dco:both:0.3+sub:0.2+hpf:1", 2000, 0.05, 0.2, 0.5, (0.25, 1.0, 0.9, 0.9), (0.3, 1.0, 0.9, 1.0), **CH(3, 0.7), **RV(0.4)),
    P("Haunted Strings", "Strings", "va:tri+noise:0.15", 1400, 0.3, 0.2, 0.5, (0.8, 2.0, 0.6, 1.5), (0.7, 1.0, 1.0, 1.8), **LF(0, 5.0, 0.15, delay=1.0), **DL(0.3, 4, 0.55, 0.35), **RV(0.55)),
    # ---------------------------------------------------------------- Brass
    P("Brass Section", "Brass", "dco:saw+hpf:1", 520, 0.15, 0.55, 0.5, (0.06, 0.6, 0.45, 0.3), (0.03, 0.8, 0.85, 0.3), **CH(1, 0.5), **RV(0.2)),
    P("Fifth Brass", "Brass", "va:saw", 900, 0.3, 0.5, 0.5, (0.05, 0.5, 0.4, 0.3), (0.02, 0.6, 0.9, 0.3), voiceMode=4, **RV(0.25)),
    P("Bright Stab Brass", "Brass", "dco:saw", 900, 0.2, 0.7, 0.6, (0.045, 0.35, 0.3, 0.2), (0.02, 0.5, 0.7, 0.2), **CH(1, 0.4), **RV(0.2)),
    P("Soft Horns", "Brass", "dco:both:0.45+hpf:1", 450, 0.1, 0.45, 0.5, (0.15, 0.8, 0.6, 0.4), (0.1, 0.8, 0.9, 0.4), **CH(2, 0.5), **RV(0.3)),
    P("Synth Brass Swell", "Brass", "dco:saw+sub:0.2", 400, 0.25, 0.7, 0.5, (0.4, 1.0, 0.5, 0.5), (0.25, 1.0, 0.9, 0.5), **CH(1, 0.5), **RV(0.3)),
    P("Punchy Brass", "Brass", "va:saw", 700, 0.35, 0.8, 0.6, (0.03, 0.25, 0.35, 0.15), (0.015, 0.4, 0.8, 0.15), **DR(0.3, 1), **RV(0.15)),
    P("Pulse Brass", "Brass", "dco:pulse:0.3", 600, 0.2, 0.6, 0.5, (0.05, 0.5, 0.4, 0.3), (0.03, 0.6, 0.85, 0.3), **CH(1, 0.4), **RV(0.2)),
    P("Wide Brass", "Brass", "dco:saw", 650, 0.15, 0.6, 0.5, (0.07, 0.6, 0.5, 0.35), (0.04, 0.7, 0.9, 0.4), **CH(3, 0.7), **DL(0.15, 6, 0.3), **RV(0.25)),
    P("Dark Tuba", "Brass", "dco:saw+sub:0.5+hpf:0", 300, 0.2, 0.5, 0.4, (0.06, 0.5, 0.4, 0.2), (0.04, 0.6, 0.9, 0.2), **RV(0.15)),
    P("Octave Brass", "Brass", "va:saw", 800, 0.25, 0.55, 0.5, (0.05, 0.5, 0.45, 0.3), (0.03, 0.6, 0.9, 0.3), voiceMode=3, **RV(0.2)),
    P("Driven Brass", "Brass", "dco:saw", 800, 0.3, 0.6, 0.5, (0.04, 0.5, 0.5, 0.3), (0.02, 0.6, 0.9, 0.3), **DR(0.55, 1), **CH(1, 0.4)),
    P("Fanfare", "Brass", "dco:both:0.4", 1200, 0.15, 0.5, 0.6, (0.03, 0.4, 0.6, 0.3), (0.02, 0.5, 0.9, 0.3), **CH(2, 0.5), **RV(0.35)),
    P("Muted Trumpet", "Brass", "va:pulse:0.3", 1100, 0.4, 0.4, 0.7, (0.02, 0.3, 0.4, 0.2), (0.01, 0.4, 0.8, 0.2), **LF(0, 5.0, 0.06, delay=0.6), **RV(0.25)),
    P("Ring Brass", "Brass", "va:saw", 1500, 0.2, 0.5, 0.5, (0.04, 0.5, 0.5, 0.3), (0.02, 0.6, 0.85, 0.3), voiceMode=5, **RV(0.3)),
    P("Brass Pad", "Brass", "dco:saw+hpf:1", 700, 0.2, 0.4, 0.5, (0.6, 1.2, 0.6, 0.8), (0.5, 1.0, 1.0, 1.0), **CH(2, 0.6), **RV(0.4)),
    P("Paraphonic Brass", "Brass", "va:saw", 500, 0.4, 0.6, 0.4, (0.08, 0.6, 0.4, 0.3), (0.05, 0.6, 0.9, 0.3), **SHARED, **DL(0.15, 6, 0.3), **RV(0.2)),
    # ---------------------------------------------------------------- Keys
    P("Hollow Keys", "Keys", "dco:pulse:0.3+pwm:0.25", 2600, 0.1, 0.35, 0.7, (0.002, 0.8, 0.3, 0.4), (0.002, 1.2, 0.2, 0.4), **LF(0, 4.0), **CH(1, 0.5), **RV(0.25)),
    P("Ring Bell Keys", "Keys", "va:saw", 3000, 0.2, 0.2, 0.6, (0.001, 1.0, 0.5, 1.0), (0.001, 1.5, 0.0, 1.2), voiceMode=5, **DL(0.25, 6, 0.4), **RV(0.35)),
    P("Triangle Keys", "Keys", "va:tri", 5000, 0.0, 0.0, 0.6, (0.002, 1.0, 1.0, 0.5), (0.002, 1.0, 0.3, 0.5), **DL(0.2, 6, 0.35), **RV(0.3)),
    P("Electric Piano", "Keys", "dco:pulse:0.45+sub:0.2", 1500, 0.05, 0.5, 0.8, (0.001, 0.9, 0.2, 0.5), (0.001, 1.6, 0.25, 0.5), **CH(1, 0.6), **RV(0.3)),
    P("Funky Pulse Keys", "Keys", "dco:pulse:0.15+hpf:2", 2200, 0.35, 0.6, 0.8, (0.001, 0.2, 0.1, 0.1), (0.001, 0.4, 0.2, 0.1), **CH(1, 0.3)),
    P("Soft Keys", "Keys", "dco:saw+hpf:1", 1200, 0.1, 0.4, 0.7, (0.004, 1.0, 0.3, 0.6), (0.003, 1.5, 0.35, 0.7), **CH(2, 0.6), **RV(0.35)),
    P("Bright Poly Keys", "Keys", "dco:both:0.35", 3500, 0.2, 0.3, 0.8, (0.001, 0.6, 0.4, 0.4), (0.001, 1.0, 0.45, 0.4), **CH(1, 0.5), **DL(0.15, 6, 0.3), **RV(0.25)),
    P("Mallet Keys", "Keys", "va:tri", 2200, 0.3, 0.6, 0.8, (0.001, 0.2, 0.0, 0.3), (0.001, 0.6, 0.0, 0.5), **DL(0.2, 9, 0.35), **RV(0.35)),
    P("Octave Keys", "Keys", "va:pulse:0.35", 2800, 0.15, 0.3, 0.7, (0.002, 0.7, 0.3, 0.5), (0.002, 1.0, 0.3, 0.5), voiceMode=3, **RV(0.3)),
    P("Vintage Piano", "Keys", "dco:saw+sub:0.3+hpf:1", 2000, 0.0, 0.6, 0.8, (0.001, 1.2, 0.1, 0.4), (0.001, 2.0, 0.0, 0.5), **CH(2, 0.4), **RV(0.35)),
    P("Toy Keys", "Keys", "va:pulse:0.12", 4000, 0.3, 0.3, 0.9, (0.001, 0.3, 0.0, 0.3), (0.001, 0.5, 0.0, 0.4), **DL(0.3, 10, 0.4), **RV(0.3)),
    P("Dreamy Keys", "Keys", "dco:pulse:0.4+pwm:0.3", 1800, 0.15, 0.3, 0.6, (0.01, 1.2, 0.5, 1.0), (0.01, 2.0, 0.4, 1.2), **LF(0, 0.8), **CH(3, 0.7), **DL(0.25, 4, 0.5), **RV(0.45)),
    P("Wah Keys", "Keys", "dco:saw", 600, 0.6, 0.7, 0.7, (0.003, 0.3, 0.2, 0.2), (0.002, 0.6, 0.4, 0.2), **CH(1, 0.4), **RV(0.2)),
    P("Fifth Keys", "Keys", "va:tri", 3500, 0.1, 0.1, 0.7, (0.002, 0.8, 0.5, 0.6), (0.002, 1.2, 0.2, 0.6), voiceMode=4, **DL(0.25, 6, 0.4), **RV(0.35)),
    P("Tine Keys", "Keys", "dco:pulse:0.5", 4500, 0.4, 0.4, 0.9, (0.001, 0.15, 0.1, 0.3), (0.001, 1.4, 0.1, 0.6), **CH(1, 0.5), **RV(0.3)),
    P("Organ Keys", "Keys", "va:pulse:0.5", 2000, 0.0, 0.0, 0.5, (0.002, 0.2, 1.0, 0.1), (0.002, 0.3, 0.6, 0.15), voiceMode=3, **DL(0.15, 6, 0.3), **RV(0.25)),
    # ---------------------------------------------------------------- Pluck
    P("Chorus Pluck", "Pluck", "dco:saw+hpf:1", 420, 0.55, 0.8, 0.8, (0.001, 0.28, 0.0, 0.3), (0.001, 0.9, 0.0, 0.5), **DL(0.25, 6, 0.4), **CH(1, 0.5), **RV(0.25)),
    P("Tape Echo Pluck", "Pluck", "va:pulse:0.3", 500, 0.6, 0.9, 0.8, (0.001, 0.2, 0.0, 0.2), (0.001, 0.5, 0.0, 0.4), **DL(0.4, 7, 0.55), **RV(0.2)),
    P("Glass Pluck", "Pluck", "va:tri", 3000, 0.4, 0.4, 1.0, (0.001, 0.15, 0.0, 0.3), (0.001, 0.6, 0.0, 0.6), **DL(0.3, 9, 0.45), **RV(0.4)),
    P("Harp Pluck", "Pluck", "dco:pulse:0.25+hpf:2", 2500, 0.15, 0.4, 0.9, (0.001, 0.5, 0.0, 0.8), (0.001, 1.5, 0.0, 1.2), **CH(2, 0.5), **RV(0.45)),
    P("Short Saw Pluck", "Pluck", "dco:saw", 300, 0.5, 0.9, 0.8, (0.001, 0.15, 0.0, 0.15), (0.001, 0.3, 0.0, 0.25), **DL(0.3, 10, 0.4), **RV(0.2)),
    P("Marimba Pluck", "Pluck", "va:tri", 1500, 0.45, 0.7, 0.9, (0.001, 0.12, 0.0, 0.2), (0.001, 0.4, 0.0, 0.4), **RV(0.3)),
    P("Bell Pluck", "Pluck", "va:saw", 4000, 0.1, 0.2, 0.9, (0.001, 0.8, 0.0, 1.0), (0.001, 1.2, 0.0, 1.5), voiceMode=5, **DL(0.3, 6, 0.45), **RV(0.45)),
    P("Soft Pluck", "Pluck", "dco:both:0.45", 800, 0.2, 0.6, 0.7, (0.002, 0.4, 0.0, 0.5), (0.002, 0.8, 0.0, 0.6), **CH(3, 0.6), **RV(0.35)),
    P("Resonant Pluck", "Pluck", "va:saw", 350, 0.8, 0.85, 0.9, (0.001, 0.22, 0.0, 0.2), (0.001, 0.4, 0.0, 0.3), **DL(0.3, 9, 0.45), **RV(0.25)),
    P("Kalimba", "Pluck", "va:tri", 2200, 0.3, 0.5, 1.0, (0.001, 0.1, 0.0, 0.3), (0.001, 0.7, 0.0, 0.7), voiceMode=3, **RV(0.35)),
    P("Pulse Pluck", "Pluck", "dco:pulse:0.15", 1200, 0.35, 0.7, 0.9, (0.001, 0.2, 0.0, 0.3), (0.001, 0.5, 0.0, 0.4), **CH(1, 0.5), **DL(0.25, 7, 0.4), **RV(0.3)),
    P("Fifth Pluck", "Pluck", "va:pulse:0.4", 1000, 0.4, 0.7, 0.8, (0.001, 0.2, 0.0, 0.3), (0.001, 0.5, 0.0, 0.4), voiceMode=4, **DL(0.3, 6, 0.45), **RV(0.3)),
    P("Wet Pluck", "Pluck", "dco:saw+hpf:2", 900, 0.4, 0.6, 0.8, (0.001, 0.3, 0.0, 0.4), (0.001, 0.6, 0.0, 0.5), **CH(2, 0.6), **DL(0.4, 4, 0.6, 0.35), **RV(0.5)),
    P("Koto Pluck", "Pluck", "dco:pulse:0.1+hpf:2", 3500, 0.5, 0.4, 1.0, (0.001, 0.08, 0.0, 0.3), (0.001, 0.9, 0.0, 0.8), **RV(0.35)),
    P("Noise Pluck", "Pluck", "va:saw+noise:0.3", 1400, 0.6, 0.7, 0.9, (0.001, 0.1, 0.0, 0.2), (0.001, 0.25, 0.0, 0.3), **DL(0.25, 10, 0.4), **RV(0.3)),
    P("Dark Pluck", "Pluck", "dco:saw+sub:0.4", 220, 0.4, 0.7, 0.6, (0.001, 0.35, 0.0, 0.3), (0.001, 0.8, 0.0, 0.5), **CH(1, 0.5), **RV(0.3)),
    # ---------------------------------------------------------------- Bass
    P("Sub Pulse Bass", "Bass", "dco:pulse:0.5+sub:0.8+hpf:0", 380, 0.3, 0.6, 0.3, (0.002, 0.35, 0.1, 0.1), (0.002, 0.5, 0.6, 0.1)),
    P("Squelch Bass", "Bass", "va:saw", 220, 0.75, 0.7, 0.3, (0.001, 0.25, 0.0, 0.1), (0.001, 0.4, 0.5, 0.08), filterDrive=0.4),
    P("Poly Saw Bass", "Bass", "dco:saw+sub:0.5+hpf:0", 500, 0.2, 0.5, 0.4, (0.002, 0.3, 0.3, 0.1), (0.002, 0.6, 0.7, 0.1), **CH(1, 0.3)),
    P("Funky Pulse Bass", "Bass", "dco:pulse:0.25+hpf:0", 350, 0.45, 0.8, 0.5, (0.001, 0.18, 0.1, 0.08), (0.001, 0.3, 0.4, 0.08)),
    P("Round Bass", "Bass", "va:tri", 600, 0.1, 0.3, 0.3, (0.002, 0.5, 0.5, 0.1), (0.002, 0.8, 0.8, 0.1)),
    P("Octave Bass", "Bass", "va:saw", 450, 0.3, 0.5, 0.3, (0.002, 0.3, 0.3, 0.1), (0.002, 0.5, 0.7, 0.1), voiceMode=3),
    P("Driven Bass", "Bass", "dco:saw+sub:0.4", 600, 0.35, 0.5, 0.4, (0.002, 0.3, 0.3, 0.1), (0.002, 0.5, 0.7, 0.1), **DR(0.55, 1)),
    P("Pumping Bass", "Bass", "dco:saw+sub:0.6+hpf:0", 300, 0.4, 0.2, 0.3, (0.002, 0.5, 1.0, 0.1), (0.002, 0.5, 1.0, 0.1), lfoSync=1, lfoDiv=7, lfoShape=1, lfoCut=0.6),
    P("Ring Bass", "Bass", "va:saw", 800, 0.3, 0.4, 0.3, (0.002, 0.3, 0.3, 0.1), (0.002, 0.5, 0.6, 0.1), voiceMode=5),
    P("Paraphonic Bass", "Bass", "va:pulse:0.4", 260, 0.6, 0.6, 0.3, (0.002, 0.3, 0.2, 0.1), (0.002, 0.5, 0.7, 0.1), **SHARED, filterDrive=0.3),
    P("Soft Sub Bass", "Bass", "dco:pulse:0.5+sub:1.0+hpf:0", 200, 0.0, 0.2, 0.2, (0.005, 0.6, 0.7, 0.15), (0.005, 0.8, 0.9, 0.15)),
    P("Wobble Bass", "Bass", "dco:saw+sub:0.5", 250, 0.6, 0.0, 0.3, (0.002, 0.5, 1.0, 0.1), (0.002, 0.5, 1.0, 0.1), lfoSync=1, lfoDiv=10, lfoShape=0, lfoCut=0.7),
    P("Fifth Bass", "Bass", "va:saw", 400, 0.35, 0.6, 0.3, (0.001, 0.25, 0.2, 0.1), (0.001, 0.4, 0.6, 0.1), voiceMode=4),
    P("Plucked Bass", "Bass", "dco:both:0.4+sub:0.4", 300, 0.5, 0.8, 0.4, (0.001, 0.2, 0.0, 0.1), (0.001, 0.45, 0.0, 0.15), **CH(1, 0.3)),
    P("Echo Bass", "Bass", "va:pulse:0.3", 350, 0.5, 0.6, 0.3, (0.001, 0.25, 0.1, 0.1), (0.001, 0.4, 0.4, 0.1), **DL(0.2, 6, 0.45, 0.3)),
    P("Velvet Bass", "Bass", "dco:saw+sub:0.6+hpf:0", 700, 0.05, 0.25, 0.3, (0.01, 0.8, 0.6, 0.2), (0.01, 1.0, 0.85, 0.2), **CH(2, 0.4)),
    # ---------------------------------------------------------------- Organ
    P("Square Organ", "Organ", "dco:pulse:0.5+sub:0.5+hpf:2", 4000, 0.0, 0.0, 0.5, (0.003, 0.1, 1.0, 0.08), (0.003, 0.1, 1.0, 0.08), **CH(3, 0.5), **RV(0.25)),
    P("Octave Organ", "Organ", "va:pulse:0.5", 2500, 0.1, 0.0, 0.5, (0.002, 0.2, 1.0, 0.1), (0.002, 0.2, 1.0, 0.1), voiceMode=3, **DL(0.15, 6), **RV(0.3)),
    P("Church Organ", "Organ", "dco:both:0.5+sub:0.6+hpf:1", 3000, 0.0, 0.0, 0.5, (0.05, 0.2, 1.0, 0.6), (0.05, 0.2, 1.0, 0.8), **CH(3, 0.6), **RV(0.6)),
    P("Percussive Organ", "Organ", "dco:pulse:0.5+hpf:2", 3500, 0.1, 0.5, 0.6, (0.001, 0.15, 0.5, 0.08), (0.002, 0.2, 1.0, 0.08), **CH(2, 0.5), **RV(0.25)),
    P("Rotary Organ", "Organ", "dco:pulse:0.5+sub:0.4+hpf:2", 3500, 0.0, 0.0, 0.5, (0.003, 0.2, 1.0, 0.1), (0.003, 0.2, 1.0, 0.1), **LF(0, 6.5, 0.12), **CH(3, 0.7), **RV(0.3)),
    P("Fifth Organ", "Organ", "va:pulse:0.5", 2000, 0.0, 0.0, 0.5, (0.003, 0.2, 1.0, 0.1), (0.003, 0.2, 1.0, 0.1), voiceMode=4, **RV(0.3)),
    P("Reed Organ", "Organ", "dco:pulse:0.2+hpf:2", 1800, 0.15, 0.0, 0.5, (0.02, 0.2, 1.0, 0.15), (0.02, 0.2, 1.0, 0.15), **CH(1, 0.5), **RV(0.35)),
    P("Driven Organ", "Organ", "dco:both:0.5+sub:0.5", 3000, 0.05, 0.0, 0.5, (0.003, 0.2, 1.0, 0.1), (0.003, 0.2, 1.0, 0.1), **DR(0.6, 1, 0.5), **CH(3, 0.5), **RV(0.2)),
    P("Soft Flute Organ", "Organ", "va:tri", 2500, 0.0, 0.0, 0.5, (0.05, 0.2, 1.0, 0.2), (0.05, 0.2, 1.0, 0.2), voiceMode=3, **LF(0, 5.5, 0.05), **RV(0.4)),
    P("Thin Organ", "Organ", "dco:pulse:0.5+hpf:3", 6000, 0.0, 0.0, 0.5, (0.002, 0.1, 1.0, 0.06), (0.002, 0.1, 1.0, 0.06), **CH(2, 0.6), **RV(0.25)),
    P("Ring Organ", "Organ", "va:pulse:0.5", 3000, 0.0, 0.0, 0.5, (0.003, 0.2, 1.0, 0.1), (0.003, 0.2, 1.0, 0.1), voiceMode=5, **DL(0.15, 6), **RV(0.3)),
    P("Cheap Combo Organ", "Organ", "dco:pulse:0.35+hpf:2", 2500, 0.25, 0.0, 0.5, (0.002, 0.1, 1.0, 0.05), (0.002, 0.1, 1.0, 0.05), **LF(0, 7.0, 0.1), **RV(0.2)),
    P("Gospel Organ", "Organ", "dco:both:0.5+sub:0.7+hpf:1", 3500, 0.05, 0.3, 0.6, (0.002, 0.3, 0.7, 0.15), (0.003, 0.3, 1.0, 0.15), **CH(3, 0.7), **RV(0.4)),
    P("Dark Organ", "Organ", "dco:pulse:0.5+sub:0.8+hpf:0", 1500, 0.1, 0.0, 0.4, (0.01, 0.2, 1.0, 0.2), (0.01, 0.2, 1.0, 0.2), **CH(1, 0.5), **RV(0.4)),
    P("Swell Organ", "Organ", "va:pulse:0.5", 1500, 0.1, 0.4, 0.5, (0.6, 1.0, 1.0, 0.5), (0.5, 1.0, 1.0, 0.5), voiceMode=3, **RV(0.45)),
    P("Bright Drawbars", "Organ", "dco:both:0.5+sub:0.3+hpf:2", 8000, 0.0, 0.0, 0.5, (0.002, 0.1, 1.0, 0.06), (0.002, 0.1, 1.0, 0.06), **CH(3, 0.6), **RV(0.25)),
    # ---------------------------------------------------------------- Arp
    P("Pulse Arp", "Arp", "dco:pulse:0.25+hpf:1", 800, 0.5, 0.6, 0.6, (0.001, 0.2, 0.0, 0.2), (0.001, 0.3, 0.0, 0.2), **ARP(1, 2, 10), **DL(0.3, 6, 0.45), **CH(1, 0.4), **RV(0.2)),
    P("Random Arp", "Arp", "va:pulse:0.4", 1200, 0.4, 0.3, 0.6, (0.001, 0.2, 0.3, 0.2), (0.001, 0.2, 0.0, 0.15), **ARP(4, 2, 11, 0.6), **LF(3, 8.0, 0.4), **DL(0.25, 7, 0.4), **RV(0.25)),
    P("Up Down Saw Arp", "Arp", "dco:saw", 600, 0.55, 0.7, 0.7, (0.001, 0.15, 0.0, 0.15), (0.001, 0.25, 0.0, 0.15), **ARP(3, 2, 10, 0.5), **DL(0.3, 9, 0.4), **RV(0.25)),
    P("Glass Arp", "Arp", "va:tri", 3500, 0.2, 0.3, 0.8, (0.001, 0.3, 0.0, 0.3), (0.001, 0.4, 0.0, 0.4), **ARP(1, 3, 10, 0.4), **DL(0.35, 6, 0.5), **RV(0.4)),
    P("Bubbling Arp", "Arp", "dco:pulse:0.4", 400, 0.7, 0.8, 0.7, (0.001, 0.12, 0.0, 0.1), (0.001, 0.2, 0.0, 0.1), **ARP(2, 1, 11, 0.4), **DL(0.25, 10, 0.4), **RV(0.2)),
    P("Octave Arp", "Arp", "va:saw", 1500, 0.3, 0.5, 0.6, (0.001, 0.2, 0.2, 0.2), (0.001, 0.25, 0.2, 0.2), **ARP(1, 3, 7, 0.5), voiceMode=3, **DL(0.3, 6, 0.45), **RV(0.3)),
    P("Slow Dreamy Arp", "Arp", "dco:both:0.35", 1200, 0.2, 0.4, 0.6, (0.01, 0.6, 0.2, 0.8), (0.01, 0.8, 0.2, 1.0), **ARP(3, 2, 7, 0.8), **CH(3, 0.6), **DL(0.35, 4, 0.55), **RV(0.45)),
    P("Ring Arp", "Arp", "va:saw", 2500, 0.2, 0.3, 0.7, (0.001, 0.2, 0.0, 0.2), (0.001, 0.3, 0.0, 0.3), **ARP(1, 2, 10, 0.5), voiceMode=5, **DL(0.3, 6, 0.45), **RV(0.3)),
    P("Funky Arp", "Arp", "dco:pulse:0.15+hpf:2", 1800, 0.4, 0.6, 0.7, (0.001, 0.1, 0.1, 0.05), (0.001, 0.15, 0.2, 0.05), **ARP(4, 1, 10, 0.3), **CH(1, 0.4)),
    P("Wide Pad Arp", "Arp", "dco:saw+hpf:2", 2000, 0.1, 0.3, 0.6, (0.05, 0.6, 0.6, 0.6), (0.05, 0.8, 0.6, 0.8), **ARP(1, 2, 8, 0.9), **CH(3, 0.7), **DL(0.3, 6, 0.5), **RV(0.45)),
    P("Fast Triplet Arp", "Arp", "va:pulse:0.3", 1000, 0.5, 0.6, 0.7, (0.001, 0.1, 0.0, 0.1), (0.001, 0.15, 0.0, 0.1), **ARP(1, 2, 11, 0.4), **DL(0.25, 8, 0.4), **RV(0.2)),
    P("Resonant Arp", "Arp", "dco:saw", 400, 0.8, 0.7, 0.8, (0.001, 0.18, 0.0, 0.15), (0.001, 0.25, 0.0, 0.15), **ARP(2, 2, 10, 0.5), **DL(0.3, 9, 0.45), **RV(0.25)),
    P("Fifth Arp", "Arp", "va:tri", 2500, 0.2, 0.4, 0.7, (0.001, 0.2, 0.0, 0.2), (0.001, 0.3, 0.0, 0.3), **ARP(3, 2, 10, 0.5), voiceMode=4, **DL(0.3, 6, 0.45), **RV(0.35)),
    P("Pumping Arp", "Arp", "dco:pulse:0.4+sub:0.3", 900, 0.4, 0.4, 0.6, (0.001, 0.2, 0.3, 0.2), (0.001, 0.25, 0.5, 0.2), **ARP(1, 1, 10, 0.7), lfoSync=1, lfoDiv=4, lfoShape=1, lfoCut=0.35, **RV(0.2)),
    P("Plucked Harp Arp", "Arp", "dco:pulse:0.2+hpf:2", 3000, 0.1, 0.4, 0.9, (0.001, 0.4, 0.0, 0.7), (0.001, 1.0, 0.0, 1.0), **ARP(3, 3, 10, 0.6), **CH(2, 0.5), **RV(0.45)),
    P("Dark Echo Arp", "Arp", "va:saw", 500, 0.6, 0.6, 0.6, (0.001, 0.2, 0.0, 0.2), (0.001, 0.3, 0.0, 0.2), **ARP(2, 2, 10, 0.5), **DL(0.45, 6, 0.6, 0.3), **RV(0.3)),
]
