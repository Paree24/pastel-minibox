"""Acid factory bank: 8 families × 16 hand-designed patches on the bass-line circuit model.

A(name, family, wave, cutoff, resonance, env mod, decay, accent, sweep, extras...):
wave 0 = saw, 1 = square, between = hybrid; cutoff 200–4000 Hz; decay 0.2–2 s.
"""


def A(name, fam, wave, cut, res, env, dec, acc=0.6, sweep=0.4, **x):
    v = dict(oscWave=wave, cutoff=cut, reso=res, envMod=env, decay=dec, accAmt=acc, sweep=sweep)
    v.update(x)
    return (name, fam, v)


DL = lambda mix, div=9, fb=0.45, duck=0.5, tone=0.5: dict(delayMix=mix, delayDiv=div, delayFb=fb, delayDuck=duck, delayTone=tone)
# reverb: the space grows with the send (small room at low mix, hall at high), unless set
RV = lambda mix, size=None, tone=0.55, pre=None: dict(reverb=mix, revSize=min(0.9, 0.25 + 1.1 * mix) if size is None else size,
                                                       revTone=tone, revPre=round(0.008 + 0.05 * mix, 4) if pre is None else pre)
DR = lambda kind, amt, tone=0.6, mix=1.0: dict(driveType=kind, driveAmt=amt, driveTone=tone, driveMix=mix)
OD, DS, FZ = 0, 1, 2   # the drive pedals: overdrive, op-amp distortion, four-stage fuzz
LFO = lambda shape, div, cut, sync=1: dict(lfoShape=shape, lfoDiv=div, lfoCut=cut, lfoSync=sync)
LADDER = dict(filterType=0)
PLUCK = lambda d: dict(ampMode=1, ampDecay=d)
STACK = lambda mode, mix=0.6, spread=0.5: dict(stack=1, stackMode=mode, stackMix=mix, stackSpread=spread)

ACID = [
    # ---------------------------------------------------------------- Classic
    A("Classic Line", "Classic", 0, 450, 0.75, 0.55, 0.45),
    A("Square Line", "Classic", 1, 500, 0.7, 0.6, 0.35),
    A("Warm Line", "Classic", 0, 380, 0.6, 0.45, 0.6, bassComp=0.8, **RV(0.12)),
    A("Bright Line", "Classic", 0, 900, 0.7, 0.5, 0.4, **DL(0.15)),
    A("Short Line", "Classic", 0, 520, 0.8, 0.65, 0.22, **DL(0.3, 10, 0.35, 0.6)),
    A("Long Decay Line", "Classic", 0.2, 300, 0.85, 0.7, 1.6, **RV(0.25)),
    A("Hybrid Line", "Classic", 0.5, 480, 0.72, 0.55, 0.45),
    A("Low Res Line", "Classic", 0, 650, 0.35, 0.6, 0.5, sub=0.4, **LADDER),
    A("Accent Line", "Classic", 0.3, 420, 0.75, 0.4, 0.4, acc=0.95, sweep=0.8, **DL(0.2, 6, 0.4)),
    A("Gentle Line", "Classic", 1, 600, 0.5, 0.35, 0.7, acc=0.3, **RV(0.15)),
    A("Ladder Line", "Classic", 1, 500, 0.7, 0.55, 0.45, noise=0.08, **LADDER, **RV(0.2)),
    A("Thin Pulse Line", "Classic", 1, 900, 0.55, 0.35, 0.3, pw=0.12, **DL(0.2, 10, 0.35)),
    A("Echo Line", "Classic", 0, 460, 0.75, 0.55, 0.4, **DL(0.3, 9, 0.5, 0.6)),
    A("Room Line", "Classic", 1, 520, 0.7, 0.5, 0.45, **RV(0.3)),
    A("Key Tracked Line", "Classic", 0, 350, 0.75, 0.5, 0.45, key=0.8),
    A("Tight Line", "Classic", 1, 600, 0.8, 0.45, 0.25, bassComp=0.9, **DR(DS, 0.3)),
    # ---------------------------------------------------------------- Squelch
    A("Squelch", "Squelch", 0, 300, 0.95, 0.9, 0.3, acc=0.9, sweep=0.6),
    A("Rubber", "Squelch", 0.4, 380, 0.85, 0.7, 0.6, acc=0.7, accDecay=0.3, **LADDER, **DL(0.25, 10, 0.4)),
    A("Screaming Squelch", "Squelch", 0, 260, 1.0, 1.0, 0.35, acc=1.0, sweep=1.0, **DL(0.25, 6, 0.45), **RV(0.2)),
    A("Square Squelch", "Squelch", 1, 320, 0.95, 0.85, 0.3, acc=0.8),
    A("Liquid Squelch", "Squelch", 0, 350, 0.92, 0.75, 0.8, **RV(0.25)),
    A("Chirp", "Squelch", 0, 420, 0.97, 0.95, 0.2, acc=0.8, sweep=0.3, **DL(0.3, 10, 0.4)),
    A("Bubbling Squelch", "Squelch", 1, 280, 0.9, 0.8, 0.25, **DL(0.25, 10, 0.4)),
    A("Wet Squelch", "Squelch", 0, 330, 0.93, 0.85, 0.4, **DL(0.3, 9, 0.5), **RV(0.2)),
    A("Acid Tweet", "Squelch", 0, 600, 0.98, 0.7, 0.2, acc=0.9),
    A("Sweep Charge", "Squelch", 0.3, 300, 0.9, 0.8, 0.5, acc=1.0, sweep=1.0, accDecay=0.6, **RV(0.35)),
    A("Low Squelch", "Squelch", 0, 220, 0.95, 0.9, 0.5, bassComp=0.9, **DL(0.25, 6, 0.45)),
    A("Hybrid Squelch", "Squelch", 0.6, 300, 0.92, 0.85, 0.35, **DR(FZ, 0.35)),
    A("Ladder Squelch", "Squelch", 0, 350, 0.9, 0.85, 0.3, **LADDER),
    A("Slow Squelch", "Squelch", 0.3, 260, 0.9, 0.6, 1.6, **RV(0.3)),
    A("Driven Squelch", "Squelch", 0, 300, 0.92, 0.85, 0.3, **DR(DS, 0.4)),
    A("Noisy Squelch", "Squelch", 1, 320, 0.95, 0.85, 0.3, noise=0.2),
    # ---------------------------------------------------------------- Deep
    A("Deep Bass", "Deep", 0, 220, 0.3, 0.25, 1.2, acc=0.4, sub=0.6),
    A("Dub Bass", "Deep", 1, 250, 0.4, 0.3, 0.9, acc=0.4, **DL(0.25, 6, 0.4)),
    A("Sub Acid", "Deep", 1, 280, 0.2, 0.25, 1.5, acc=0.3, sub=0.9),
    A("Round Low", "Deep", 1, 280, 0.25, 0.3, 0.8, bassComp=1.0, sub=0.6),
    A("Soft Deep", "Deep", 0, 240, 0.4, 0.35, 1.0, acc=0.2, **RV(0.15)),
    A("Rumble", "Deep", 1, 210, 0.5, 0.4, 1.8, sub=0.5, **DR(DS, 0.3, 0.3)),
    A("Thick Deep", "Deep", 0, 260, 0.35, 0.3, 0.9, **STACK(2, 0.5, 0.3), sub=0.4),
    A("Dark Pulse", "Deep", 1, 230, 0.45, 0.35, 0.7, pw=0.3),
    A("Muted Bass", "Deep", 0, 300, 0.2, 0.15, 0.4),
    A("Night Bass", "Deep", 0, 240, 0.55, 0.4, 1.4, **DL(0.2, 6, 0.5, 0.7), **RV(0.2)),
    A("Ladder Deep", "Deep", 0, 330, 0.6, 0.5, 0.6, **LADDER, **DR(OD, 0.4, 0.4)),
    A("Booming Acid", "Deep", 1, 200, 0.6, 0.5, 2.0, sub=0.6),
    A("Gentle Sub", "Deep", 0, 220, 0.1, 0.1, 1.2, acc=0.2, sub=0.8),
    A("Deep Pluck", "Deep", 0, 260, 0.5, 0.45, 0.5, sub=0.5, **PLUCK(0.35)),
    A("Velvet Deep", "Deep", 0.3, 280, 0.35, 0.3, 1.0, bassComp=0.9, **RV(0.2)),
    A("Wobbly Deep", "Deep", 1, 240, 0.65, 0.2, 1.2, sub=0.5, **LFO(0, 4, 0.6)),
    # ---------------------------------------------------------------- Lead
    A("Screamer", "Lead", 0, 1500, 0.9, 0.4, 0.8, **DL(0.3, 9, 0.45), **RV(0.2)),
    A("Hybrid Lead", "Lead", 0.5, 1100, 0.8, 0.5, 0.6, **DL(0.2, 6), **RV(0.2)),
    A("Singing Acid", "Lead", 1, 1800, 0.88, 0.3, 1.0, **DL(0.3, 6, 0.5), **RV(0.3)),
    A("Bright Acid Lead", "Lead", 0, 2500, 0.7, 0.4, 0.6, key=0.6, **DL(0.25, 9)),
    A("Whistle Acid", "Lead", 1, 1300, 0.97, 0.2, 1.2, **RV(0.35)),
    A("Spacey Acid", "Lead", 0, 1200, 0.85, 0.5, 0.8, **DL(0.45, 6, 0.6, 0.4), **RV(0.4)),
    A("Thin Lead", "Lead", 1, 2000, 0.75, 0.4, 0.5, pw=0.15, **DL(0.2, 9)),
    A("Distant Lead", "Lead", 0, 900, 0.85, 0.5, 0.9, **DL(0.35, 4, 0.55), **RV(0.45)),
    A("Fifth Lead", "Lead", 0, 1300, 0.8, 0.45, 0.7, **STACK(1, 0.5), **DL(0.25, 9)),
    A("Octave Lead", "Lead", 1, 1500, 0.8, 0.4, 0.7, **STACK(2, 0.6), **RV(0.25)),
    A("Distortion Lead", "Lead", 0, 1400, 0.85, 0.45, 0.7, **DR(DS, 0.5), **DL(0.25, 9)),
    A("Laser Acid", "Lead", 0, 3500, 0.95, 0.6, 0.3, **DL(0.3, 10, 0.4)),
    A("Talking Acid", "Lead", 0, 900, 0.9, 0.3, 1.0, **LFO(0, 7, 0.3), **RV(0.2)),
    A("Key Lead", "Lead", 0, 800, 0.85, 0.4, 0.8, key=1.0, **DL(0.2, 6)),
    A("Crying Acid", "Lead", 0.3, 1600, 0.93, 0.5, 1.4, **DL(0.3, 6, 0.55), **RV(0.35)),
    A("Pulse Lead", "Lead", 1, 2200, 0.7, 0.3, 0.6, pw=0.3, **RV(0.25)),
    # ---------------------------------------------------------------- Distorted
    # the line through a pedal: overdrive keeps the bass clean and pushes the mids, the op-amp distortion
    # clips hard behind a sweepable filter, the fuzz saturates everything and scoops the middle
    A("Overdrive Line", "Distorted", 0, 520, 0.8, 0.6, 0.45, **DR(OD, 0.5, 0.55), **DL(0.2, 9, 0.4)),
    A("Screaming Overdrive", "Distorted", 0, 420, 0.93, 0.8, 0.35, acc=0.9, sweep=0.7, **DR(OD, 0.9, 0.75)),
    A("Edge Of Breakup", "Distorted", 1, 600, 0.7, 0.5, 0.5, **DR(OD, 0.3, 0.45, 0.8), **RV(0.2)),
    A("Dark Overdrive", "Distorted", 0.3, 380, 0.75, 0.55, 0.6, **DR(OD, 0.75, 0.1)),
    A("Distortion Line", "Distorted", 0, 480, 0.82, 0.6, 0.4, **DR(DS, 0.5, 0.5)),
    A("Saturated Distortion", "Distorted", 1, 420, 0.78, 0.65, 0.45, **DR(DS, 1.0, 0.35)),
    A("Open Filter Distortion", "Distorted", 0, 650, 0.85, 0.55, 0.35, **DR(DS, 0.8, 1.0)),
    A("Muffled Distortion", "Distorted", 0, 550, 0.8, 0.7, 0.4, **DR(DS, 0.7, 0.08), **DL(0.25, 6, 0.45)),
    A("Fuzz Line", "Distorted", 0, 500, 0.8, 0.6, 0.45, **DR(FZ, 0.6, 0.5)),
    A("Thick Fuzz", "Distorted", 1, 330, 0.7, 0.5, 0.7, **DR(FZ, 1.0, 0.15)),
    A("Sharp Fuzz", "Distorted", 0, 700, 0.85, 0.5, 0.3, **DR(FZ, 0.8, 0.9)),
    A("Parallel Fuzz", "Distorted", 0.5, 450, 0.8, 0.6, 0.5, **DR(FZ, 1.0, 0.5, 0.45), **RV(0.2)),
    A("Overdriven Squelch", "Distorted", 0, 300, 0.96, 0.92, 0.3, acc=1.0, sweep=0.5, **DR(OD, 0.8, 0.6)),
    A("Distorted Echo", "Distorted", 1, 500, 0.8, 0.55, 0.4, **DR(DS, 0.6, 0.6), **DL(0.35, 9, 0.5)),
    A("Fuzz Pluck", "Distorted", 0, 600, 0.8, 0.8, 0.25, **PLUCK(0.2), **DR(FZ, 0.7, 0.6), **RV(0.25)),
    A("Overdrive Fifths", "Distorted", 0, 550, 0.75, 0.5, 0.5, **STACK(1, 0.5), **DR(OD, 0.6, 0.5)),
    # ---------------------------------------------------------------- Stack
    A("Fifth Stack", "Stack", 0, 700, 0.7, 0.5, 0.5, **STACK(1, 0.6, 0.5)),
    A("Octave Stack", "Stack", 1, 500, 0.65, 0.6, 0.45, **STACK(2, 0.7, 0.6)),
    A("Wide Fifths", "Stack", 0, 600, 0.75, 0.55, 0.5, **STACK(1, 0.8, 1.0), **RV(0.2)),
    A("Bright Octave Stack", "Stack", 1, 900, 0.4, 0.3, 0.8, **STACK(2, 0.8, 0.7), **RV(0.25)),
    A("Stacked Squelch", "Stack", 0, 350, 0.9, 0.85, 0.35, **STACK(1, 0.5)),
    A("Heavy Stack", "Stack", 0, 400, 0.7, 0.5, 0.6, sub=0.4, **STACK(2, 0.6), **DR(DS, 0.4)),
    A("Bright Stack", "Stack", 0, 1200, 0.6, 0.4, 0.5, **STACK(1, 0.6), **DL(0.2, 9)),
    A("Echo Stack", "Stack", 1, 600, 0.75, 0.5, 0.5, **STACK(2, 0.6), **DL(0.35, 6, 0.5)),
    A("Spread Octaves", "Stack", 0, 550, 0.7, 0.55, 0.6, **STACK(2, 0.7, 1.0), **RV(0.25)),
    A("Thin Stack", "Stack", 1, 800, 0.7, 0.45, 0.4, pw=0.2, **STACK(1, 0.4)),
    A("Pluck Stack", "Stack", 0, 600, 0.8, 0.7, 0.3, **STACK(1, 0.6), **PLUCK(0.25), **RV(0.2)),
    A("Dark Stack", "Stack", 0, 300, 0.6, 0.4, 0.9, **STACK(2, 0.5)),
    A("Chord Acid", "Stack", 0.5, 700, 0.75, 0.5, 0.5, **STACK(1, 0.9, 0.8), **DL(0.25, 9), **RV(0.2)),
    A("Wobble Stack", "Stack", 0, 400, 0.8, 0.3, 1.0, **STACK(2, 0.6), **LFO(0, 7, 0.5)),
    A("Fuzz Fifths Stack", "Stack", 0, 500, 0.75, 0.5, 0.5, **STACK(1, 0.6), **DR(FZ, 0.7)),
    A("Lead Stack", "Stack", 0, 1400, 0.85, 0.4, 0.8, **STACK(2, 0.5), **DL(0.3, 9, 0.5), **RV(0.25)),
    # ---------------------------------------------------------------- Wobble
    # LFO shapes: 0 Tri, 1 Saw, 2 Sq, 3 S&H; divisions 1 = 1 bar, 2 = 1/2, 3 = 1/4 D, 4 = 1/4, 6 = 1/8 D,
    # 7 = 1/8, 8 = 1/8 T, 9 = 3/16, 10 = 1/16, 11 = 1/16 T
    A("Eighth Wobble", "Wobble", 0, 350, 0.8, 0.15, 1.5, **LFO(0, 7, 0.7)),
    A("Saw Ramp Wobble", "Wobble", 1, 320, 0.78, 0.1, 2.0, **LFO(1, 4, 0.75)),
    A("Half Bar Wobble", "Wobble", 0, 280, 0.86, 0.1, 2.0, sub=0.3, **LFO(0, 2, 0.8)),
    A("Triplet Wobble", "Wobble", 0.5, 380, 0.84, 0.15, 1.5, **LFO(0, 8, 0.6)),
    A("Gate Wobble", "Wobble", 1, 420, 0.7, 0.1, 1.5, **LFO(2, 10, 0.55)),
    A("Random Steps", "Wobble", 0, 460, 0.88, 0.2, 1.0, **LFO(3, 10, 0.55), **DL(0.2, 9)),
    A("Slow Breathing", "Wobble", 0, 300, 0.75, 0.2, 2.0, **LFO(0, 1, 0.6), **RV(0.3)),
    A("Overdriven Wobble", "Wobble", 0, 300, 0.85, 0.15, 1.5, **LFO(0, 9, 0.7), **DR(OD, 0.7, 0.55)),
    A("Dub Wobble", "Wobble", 1, 260, 0.8, 0.1, 2.0, sub=0.5, **LFO(0, 3, 0.75), **DL(0.3, 6, 0.5)),
    A("Drifting Wobble", "Wobble", 0, 330, 0.82, 0.2, 1.5, lfoShape=0, lfoSync=0, lfoRate=0.7, lfoCut=0.8),
    A("Fast Trill Filter", "Wobble", 1, 520, 0.75, 0.2, 1.5, lfoShape=0, lfoSync=0, lfoRate=11.0, lfoCut=0.4),
    A("Wobble Lead", "Wobble", 0, 900, 0.9, 0.2, 1.5, **LFO(0, 6, 0.45), **DL(0.25, 9), **RV(0.2)),
    A("Octave Stack Wobble", "Wobble", 0, 350, 0.8, 0.15, 1.5, **STACK(2, 0.5), **LFO(1, 7, 0.6)),
    A("Fuzz Wobble", "Wobble", 1, 380, 0.75, 0.15, 1.5, **LFO(2, 8, 0.6), **DR(FZ, 0.7, 0.4)),
    A("Distorted Steps", "Wobble", 0, 400, 0.85, 0.2, 1.2, **LFO(3, 7, 0.6), **DR(DS, 0.6, 0.45)),
    A("Fading Wobble", "Wobble", 0.3, 340, 0.82, 0.15, 1.5, **LFO(0, 11, 0.5), lfoFade=2.0),
    # ---------------------------------------------------------------- Pluck
    A("Pluck", "Pluck", 0, 600, 0.8, 0.8, 0.25, **PLUCK(0.2)),
    A("Noise Pluck", "Pluck", 0, 900, 0.7, 0.7, 0.2, noise=0.15, **PLUCK(0.15)),
    A("Square Pluck", "Pluck", 1, 700, 0.75, 0.75, 0.25, **PLUCK(0.2), **RV(0.2)),
    A("Echo Pluck", "Pluck", 0, 650, 0.8, 0.8, 0.22, **PLUCK(0.15), **DL(0.4, 9, 0.55)),
    A("Resonant Blip", "Pluck", 0, 500, 0.95, 0.85, 0.2, **PLUCK(0.16)),
    A("Long Pluck", "Pluck", 0, 500, 0.75, 0.7, 0.5, **PLUCK(0.6), **RV(0.25)),
    A("Dull Pluck", "Pluck", 1, 400, 0.4, 0.5, 0.3, **PLUCK(0.25)),
    A("Bright Pluck", "Pluck", 0, 1500, 0.65, 0.6, 0.25, **PLUCK(0.2), **DL(0.25, 10)),
    A("Wet Pluck", "Pluck", 0.4, 700, 0.8, 0.7, 0.3, **PLUCK(0.25), **DL(0.35, 6, 0.5), **RV(0.35)),
    A("Thin Pluck", "Pluck", 1, 900, 0.75, 0.7, 0.2, pw=0.15, **PLUCK(0.15)),
    A("Overdrive Pluck", "Pluck", 0, 600, 0.8, 0.75, 0.25, **PLUCK(0.2), **DR(OD, 0.6)),
    A("Sub Pluck", "Pluck", 1, 350, 0.6, 0.6, 0.3, sub=0.7, **PLUCK(0.3)),
    A("Ladder Pluck", "Pluck", 0, 550, 0.8, 0.8, 0.25, **PLUCK(0.2), **LADDER),
    A("Fifth Pluck", "Pluck", 0, 700, 0.75, 0.7, 0.25, **STACK(1, 0.6), **PLUCK(0.2), **RV(0.2)),
    A("Accent Pluck", "Pluck", 0, 450, 0.85, 0.6, 0.3, acc=1.0, sweep=0.9, **PLUCK(0.2)),
    A("Spacey Pluck", "Pluck", 0, 800, 0.85, 0.7, 0.3, **PLUCK(0.25), **DL(0.4, 4, 0.6, 0.6), **RV(0.45)),
]
