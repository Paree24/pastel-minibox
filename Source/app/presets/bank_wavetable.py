"""Wavetable factory bank (2026-11-13): 64 bass presets and 64 others, 8 families of 8 each.

Bass: Sub, Reese, Growl, Neuro, Wobble, FM Bass, Pluck Bass, Digital Bass. Others: Lead, Pad, Pluck, Keys, Bell, Stab,
Texture, FX. The bank's rules as everywhere: no tuning offsets (octave, semitone, fine), no glide, no pitch modulation.
Unison detune is the exception: it spreads the stack around the played pitch (the wide Reese basses).

Tables, sources, destinations and divisions are named here and turned into the device's indices: the table names
come from Source/dsp/Wavetables.cpp, so the two never drift apart.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
_SRC = open(os.path.join(HERE, "..", "..", "dsp", "Wavetables.cpp")).read()
TABLES = re.findall(r'^\s*[TS] \("([^"]+)", "[^"]+"', _SRC, re.M)
assert len(TABLES) == 64, len(TABLES)

SRC = ["Off", "LFO 1", "LFO 2", "Amp Env", "Filter Env", "Velocity", "Note", "Mod Wheel", "Pressure", "Random"]
DST = ["Off", "Pitch", "Position A", "Position B", "Level A", "Level B", "Osc B Mod", "Pitch B", "Sub", "Noise", "Cutoff", "Reso",
       "Filter Drive", "Distortion", "Fold", "Crush", "Detune", "Width", "Pan", "Amp", "LFO 1 Rate", "LFO 2 Rate", "Squash", "Chorus", "Volume"]
DIV = ["2 bar", "1 bar", "1/2", "1/4 D", "1/4", "1/4 T", "1/8 D", "1/8", "1/8 T", "3/16", "1/16", "1/16 T", "1/32", "1/64"]
SHAPE = ["Sine", "Tri", "Saw", "Square", "S&H", "Smooth"]
MODE = ["Off", "Sync", "FM", "Ring"]
FILT = ["LP 24", "LP 12", "HP", "BP", "Notch"]
DIST = ["Soft", "Hard", "Tube"]
VOICE = ["Mono", "Legato", "Poly"]

BASS_FAMS = ["Sub", "Reese", "Growl", "Neuro", "Wobble", "FM Bass", "Pluck Bass", "Digital Bass"]
OTHER_FAMS = ["Lead", "Pad", "Pluck", "Keys", "Bell", "Stab", "Texture", "FX"]


def lfo(n, dest, depth, div="1/8", shape="Sine", sync=True, rate=2.0, fade=0.0):
    p = f"lfo{n}"
    return {p + "Dest": DST.index(dest), p + "Depth": depth, p + "Div": DIV.index(div), p + "Shape": SHAPE.index(shape),
            p + "Sync": 1 if sync else 0, p + "Rate": rate, p + "Fade": fade}


def mods(*routes):
    v = {}
    for i, (s, d, a) in enumerate(routes):
        v[f"mod{i + 1}Src"] = SRC.index(s); v[f"mod{i + 1}Dst"] = DST.index(d); v[f"mod{i + 1}Amt"] = a
    return v


def W(name, fam, a="Basic Shapes", apos=0.0, b=None, bpos=0.0, blevel=0.0, bmode="Off", bmod=0.0, **kw):
    """One preset: table names and choice names are given as text, the rest as the device's own values."""
    bass = fam in BASS_FAMS
    v = dict(oscATable=TABLES.index(a), oscAPos=apos, oscALevel=0.8,
             oscBTable=TABLES.index(b or a), oscBPos=bpos, oscBLevel=blevel, oscBMode=MODE.index(bmode), oscBMod=bmod,
             subLevel=0.55 if bass else 0.0, monoBass=120.0 if bass else 0.0,
             voiceMode=0 if bass else 2, filterMode=0, cutoff=1600.0, reso=0.15, filterEnv=0.25, filterKey=0.4,
             ampA=0.002, ampD=0.4, ampS=1.0, ampR=0.08 if bass else 0.25,
             fltA=0.002, fltD=0.3, fltS=0.3, fltR=0.2, velocity=0.25, lfo1Depth=0.0, lfo2Depth=0.0)
    for k, x in kw.items():
        if k == "lfos":
            for l in x: v.update(l)
        elif k == "m":
            v.update(mods(*x))
        elif k in ("filterMode",): v[k] = FILT.index(x) if isinstance(x, str) else x
        elif k in ("distType",): v[k] = DIST.index(x) if isinstance(x, str) else x
        elif k in ("voiceMode",): v[k] = VOICE.index(x) if isinstance(x, str) else x
        elif k in ("noiseType",): v[k] = ["White", "Pink"].index(x) if isinstance(x, str) else x
        elif k in ("subShape",): v[k] = ["Sine", "Tri"].index(x) if isinstance(x, str) else x
        else: v[k] = x
    # plucks and stabs ring (2026-11-13, third pass). Decay is the time to -80 dB (an RC curve: -20 dB comes at a quarter
    # of it), so a pluck that should ring for a second needs about 2 s; the filters close about three times as slowly
    if fam in ("Pluck", "Stab", "Pluck Bass") or name == "FM Pluck Bass":
        lo = 1.6 if fam == "Pluck Bass" or name == "FM Pluck Bass" else 2.2
        v["ampD"] = min(4.0, max(lo, v["ampD"] * 5.0)); v["fltD"] = min(2.0, max(0.7, v["fltD"] * 3.0)); v["ampR"] = max(v["ampR"], 0.4)
    # the others move too (2026-11-13): a chorus and a slow drift through table A unless the preset sets its own
    if not bass:
        v.setdefault("chorus", {"Pad": 0.35, "Texture": 0.35, "Lead": 0.2, "Bell": 0.2, "Keys": 0.15, "Pluck": 0.15, "Stab": 0.1}.get(fam, 0.0))
        v.setdefault("squash", {"Lead": 0.35, "Stab": 0.45, "Pluck": 0.2}.get(fam, 0.0))
        if v.get("lfo2Depth", 0.0) == 0.0 and fam != "FX":
            v.update(lfo(2, "Position A", {"Pad": 0.4, "Texture": 0.5}.get(fam, 0.25), "1 bar" if fam != "Pad" else "2 bar", "Smooth"))
    return (name, fam, v)


# ------------------------------------------------------------------------------------------------ bass
# (2026-11-13, second pass, measured against reference bass sounds: every bass moves, by an LFO or an envelope on its
# table, filter, FM, fold or noise; most are wide above a mono low end, bright enough to cut through (the filter
# barely follows the key), and dirtied by the distortion and Squash, with the sub under it all)
def B(name, fam, a, apos, R=None, drift=True, **kw):
    """A bass: the bank's bass defaults, then a recipe (R, e.g. reese()), then its own values. drift=False leaves out
    the slow drift through table A (a preset whose table must stay put, a classic Reese's plain saws)."""
    v = dict(filterKey=0.1, cutoff=8000.0, reso=0.2, filterEnv=0.15, subLevel=0.13, subOct=0, monoBass=130.0, uniVoices=3, uniWidth=0.9,
             uniDetune=0.35, chorus=0.25, distTone=0.9, distDrive=0.35, squash=0.5, fltD=0.35, fltS=0.4)
    # each family its own grit: the noise's colour and amount, the Squash
    v.update({"Sub": dict(noiseLevel=0.0, squash=0.3), "Reese": dict(noiseLevel=0.04, noiseType=1, noiseTone=0.6),
              "Growl": dict(noiseLevel=0.05, noiseType=0, noiseTone=0.8, squash=0.9), "Neuro": dict(noiseLevel=0.06, noiseType=0, noiseTone=0.85, squash=0.8),
              "Wobble": dict(noiseLevel=0.05, noiseType=1, noiseTone=0.4), "FM Bass": dict(noiseLevel=0.03, noiseType=0, noiseTone=0.9, squash=0.4),
              "Pluck Bass": dict(noiseLevel=0.0, squash=0.35), "Digital Bass": dict(noiseLevel=0.07, noiseType=0, noiseTone=0.95)}.get(fam, {}))
    v.update(R or {})
    v.update(kw)
    # nothing sits still: without a second LFO of its own, a slow drift through table A
    if drift and not any("lfo2Dest" in l for l in v.get("lfos", [])):
        v["lfos"] = list(v.get("lfos", [])) + [lfo(2, "Position A", 0.3 if fam not in ("Sub", "Growl") else 0.15, "1 bar", "Smooth")]
    return W(name, fam, a, apos, **v)


def reese(**kw):   # the stack: wide, chorused, the low end mono
    return dict(dict(uniVoices=9, uniDetune=0.45, uniWidth=1.0, chorus=0.3, distDrive=0.35, squash=0.55), **kw)


def growl(**kw):
    """A growl (2026-11-13, third pass): a vowel table moved by an envelope, once per note ("rawr", "yoy", "wow"), not
    by an LFO (that is a wobble). The filter stays open, so the level and the top stay put while the vowel changes;
    hard drive and heavy Squash keep it full and bright up to the top, as the reference growls are, and it has no sub
    (the vowel table carries the low end; a sub under it swamps the vowel)."""
    return dict(dict(cutoff=14000.0, filterEnv=0.0, filterKey=0.0, reso=0.1, uniVoices=3, uniDetune=0.12, uniWidth=0.6, chorus=0.12,
                     distDrive=0.6, distType="Hard", distTone=1.0, squash=0.95, subLevel=0.0, fltA=0.08, fltD=0.4, fltS=0.3, fltR=0.3), **kw)


SUB = [
    B("Clean Sub", "Sub", "Sub Sine", 0.15, uniVoices=1, oscALevel=0.35, subLevel=0.9, cutoff=1200.0, filterKey=0.4, monoBass=0.0,
      m=[("Filter Env", "Position A", 0.5)], fltD=0.25, fltS=0.0),
    B("Round Sub", "Sub", "Warm Sine", 0.35, subLevel=0.6, cutoff=2400.0, lfos=[lfo(1, "Position A", 0.55, "1/2", "Tri"), lfo(2, "Cutoff", 0.25, "1 bar", "Sine")]),
    B("808 Sub", "Sub", "Sub Sine", 0.9, uniVoices=1, subLevel=0.8, ampD=1.6, ampS=0.0, ampR=0.5, cutoff=3000.0, filterEnv=0.45, fltD=0.12, fltS=0.0,
      distDrive=0.3, distType="Tube", m=[("Filter Env", "Position A", 0.6)]),
    B("Tri Sub", "Sub", "Deep Tri", 0.5, subShape="Tri", subLevel=0.6, cutoff=3000.0, m=[("Amp Env", "Position A", 0.4)], lfos=[lfo(1, "Position A", 0.5, "1/4", "Sine")]),
    B("Fold Sub", "Sub", "Sine Fold", 0.25, subLevel=0.55, cutoff=5000.0, lfos=[lfo(1, "Position A", 0.45, "1/4", "Tri")], squash=0.3),
    B("Pulse Sub", "Sub", "Round Pulse", 0.3, subLevel=0.55, cutoff=4000.0, lfos=[lfo(1, "Position A", 0.7, "1/4", "Tri")]),
    B("Driven Sub", "Sub", "Warm Sine", 0.7, subLevel=0.65, cutoff=4000.0, distDrive=0.45, distType="Tube", squash=0.35,
      m=[("Velocity", "Distortion", 0.3)], velocity=0.4, lfos=[lfo(1, "Distortion", 0.35, "1/4", "Tri"), lfo(2, "Position A", 0.4, "1/2", "Sine")]),
    B("Growl Sub", "Sub", "Sine Fold", 0.4, subLevel=0.6, cutoff=5000.0, lfos=[lfo(1, "Fold", 0.5, "1/8", "Tri")], distFold=0.15, squash=0.3),
]
# Reese (2026-11-13, third pass): each its own movement, so they don't all sound alike. Classic is two plain detuned
# saws and nothing else: the beating of the pair is the movement.
REESE = [
    B("Classic Reese", "Reese", "Basic Shapes", 2 / 3, drift=False, uniVoices=2, uniDetune=0.5, uniWidth=0.5, chorus=0.0, cutoff=4500.0,
      filterKey=0.1, reso=0.15, filterEnv=0.0, distDrive=0.3, distType="Tube", squash=0.3, noiseLevel=0.0, subLevel=0.35, monoBass=110.0),
    B("Dark Reese", "Reese", "Reese Saw", 0.4, b="Saw Sweep", bpos=0.6, blevel=0.5, cutoff=700.0, reso=0.3, filterEnv=0.0,
      lfos=[lfo(1, "Cutoff", 0.45, "2 bar", "Sine", fade=1.0)], drift=False, R=reese(distDrive=0.4, squash=0.6)),     # a slow filter
    B("Hollow Reese", "Reese", "Reese Hollow", 0.6, b="Odd Harmonics", bpos=0.7, blevel=0.5, filterMode="Notch", cutoff=900.0, reso=0.4,
      lfos=[lfo(1, "Cutoff", 0.7, "1 bar", "Tri")], drift=False, R=reese()),                                           # a notch sweeping
    B("Phase Reese", "Reese", "Reese Phase", 0.2, b="Reese Saw", bpos=0.7, blevel=0.4, cutoff=4000.0, reso=0.2,
      lfos=[lfo(2, "Position A", 0.8, "2 bar", "Smooth")], R=reese(uniVoices=5)),                                       # phasing
    B("Square Reese", "Reese", "Reese Square", 0.5, b="Square Sweep", bpos=0.8, blevel=0.5, cutoff=900.0, distType="Tube", filterEnv=0.55,
      fltA=0.01, fltD=0.6, fltS=0.25, drift=False, R=reese(distDrive=0.45, squash=0.55)),                               # each note opens
    # wide: seven plain saws, spread wide around a mono low end; the stack's width breathes (its pitch spread stays put:
    # moving the detune made it seasick)
    B("Wide Reese", "Reese", "Reese Saw", 0.25, cutoff=3200.0, filterKey=0.1, reso=0.15, filterEnv=0.0, monoBass=150.0,
      lfos=[lfo(1, "Width", -0.35, "2 bar", "Sine")], drift=False, noiseLevel=0.0,
      R=reese(uniVoices=7, uniDetune=0.4, uniWidth=1.0, chorus=0.2, distType="Tube", distDrive=0.3, squash=0.45)),  # the width breathing
    # each note blooms: the dispersion (the harmonics' phases smeared, a chirp) sweeps in from the attack and settles
    B("Dispersed Reese", "Reese", "Dispersed Saw", 0.15, b="Reese Saw", bpos=0.3, blevel=0.4, cutoff=3500.0, filterKey=0.1, filterEnv=0.0,
      fltA=0.01, fltD=0.9, fltS=0.2, m=[("Filter Env", "Position A", 0.6)], drift=False, noiseLevel=0.0,
      R=reese(uniVoices=5, uniDetune=0.35, chorus=0.15, distType="Tube", distDrive=0.35, squash=0.5)),
    B("Rasp Reese", "Reese", "Rasp", 0.5, b="Reese Saw", bpos=0.5, blevel=0.6, cutoff=7000.0, reso=0.35, distType="Hard",
      lfos=[lfo(1, "Distortion", 0.4, "1/2", "Sine")], m=[("Mod Wheel", "Position A", 0.5)], drift=False, R=reese(distDrive=0.5, squash=0.75)),
]
GROWL = [
    B("Growler", "Growl", "Growl", 0.0, b="FM 1:1", bpos=0.5, blevel=0.35, bmode="FM", bmod=0.2,
      m=[("Filter Env", "Position A", 0.85), ("Filter Env", "Osc B Mod", 0.3)], lfos=[lfo(1, "Position A", 0.3, "1/4", "Tri", fade=0.5)], R=growl()),                            # rawr
    B("Yoy", "Growl", "Yoy", 0.0, fltA=0.12, fltD=0.28, fltS=0.0, m=[("Filter Env", "Position A", 1.0)], lfos=[lfo(1, "Position A", 0.45, "1/2", "Sine", fade=0.6)], R=growl(distType="Tube")),   # y-o-y
    B("Talk Bass", "Growl", "Talk", 0.0, fltA=0.005, fltD=1.4, fltS=0.0, m=[("Filter Env", "Position A", 1.0)], lfos=[lfo(1, "Position A", 0.35, "1 bar", "Smooth", fade=1.2)], R=growl()),          # u-o-i-e-a
    B("Vowel Bass", "Growl", "Vowel A-O", 1.0, noiseLevel=0.0, fltA=0.1, fltD=0.35, fltS=0.25, m=[("Filter Env", "Position A", -1.0)], lfos=[lfo(1, "Position A", -0.35, "1/2", "Tri", fade=0.5)], R=growl()),  # wow
    B("Ee-Ii", "Growl", "Vowel E-I", 0.6, fltA=0.04, fltD=0.25, fltS=0.5, m=[("Filter Env", "Position A", -0.6), ("Mod Wheel", "Position A", 0.4)],
      lfos=[lfo(1, "Position A", 0.25, "1/4", "Sine", fade=0.4)], R=growl(distDrive=0.5)),
    B("Throat Growl", "Growl", "Throat", 0.0, b="Growl", bpos=0.5, blevel=0.4, bmode="Ring", bmod=0.3, fltA=0.06, fltD=0.5, fltS=0.4,
      m=[("Filter Env", "Position A", 0.8), ("Filter Env", "Position B", 0.5)], lfos=[lfo(1, "Position B", 0.4, "1/4", "Tri", fade=0.5)], R=growl()),
    B("Horn Bass", "Growl", "Horn", 0.2, fltA=0.15, fltD=0.6, fltS=0.6, m=[("Filter Env", "Position A", 0.7)],
      R=growl(distType="Tube", distDrive=0.5, cutoff=3200.0, filterEnv=0.5, filterKey=0.3)),                            # brass: opens as it swells
    # foghorn: it ramps up. Each note starts dark and quiet and swells into the horn over about a second and a half, then
    # holds: a folded sine FM-ing another through a low-pass at 200 Hz, and one slow envelope opening the filter, the FM
    # and the fold, and raising the Volume (after the drive, which would otherwise hold the level flat). Measured against
    # a reference foghorn: about 6 dB louder and from ~250 Hz to ~2 kHz bright over the first 1.5 s
    B("Foghorn", "Growl", "Sine Fold", 0.6, b="Sine Fold", bpos=0.2, blevel=0.8, oscALevel=0.0, bmode="FM", bmod=0.15,
      ampA=0.25, ampD=1.0, ampS=1.0, ampR=0.6, fltA=1.1, fltD=2.0, fltS=1.0, fltR=0.6,
      m=[("Filter Env", "Position B", 0.4), ("Filter Env", "Osc B Mod", 0.4), ("Filter Env", "Volume", 0.5)], drift=False, noiseLevel=0.0,
      R=growl(filterMode="LP 24", cutoff=200.0, reso=0.25, filterEnv=0.55, filterKey=0.0, uniVoices=3, uniDetune=0.3, uniWidth=1.0,
              chorus=0.2, distType="Hard", distDrive=0.75, squash=0.9)),
]
NEURO = [
    B("Neuro Grind", "Neuro", "Neuro Grind", 0.4, cutoff=9000.0, reso=0.2, distDrive=0.45, distType="Hard", squash=0.75,
      lfos=[lfo(1, "Position A", 0.45, "1/4", "Tri")], drift=False, uniVoices=3, noiseLevel=0.04),
    B("Bite", "Neuro", "Neuro Bite", 0.4, b="FM 1:2", bpos=0.5, blevel=0.5, bmode="FM", bmod=0.4, cutoff=8000.0, distDrive=0.55, squash=0.75,
      m=[("Filter Env", "Position A", 0.6), ("Filter Env", "Osc B Mod", 0.4)], fltD=0.2, fltS=0.2, lfos=[lfo(1, "Position B", 0.6, "1/8")]),
    B("Snarl", "Neuro", "Snarl", 0.15, b="Throat", bpos=0.3, blevel=0.35, cutoff=6000.0, reso=0.25, filterMode="LP 12", filterEnv=0.0,
      fltA=0.03, fltD=0.35, fltS=0.35, m=[("Filter Env", "Position A", 0.6), ("Filter Env", "Position B", 0.5)],
      distType="Tube", distDrive=0.45, squash=0.8, uniVoices=3, noiseLevel=0.03),
    B("Fold Bass", "Neuro", "Saw Fold", 0.3, cutoff=8000.0, distFold=0.45, squash=0.7, distDrive=0.3,
      lfos=[lfo(1, "Fold", 0.6, "1/8", "Sine"), lfo(2, "Position A", 0.6, "1/4", "Tri")]),
    B("Square Folder", "Neuro", "Square Fold", 0.4, cutoff=7000.0, reso=0.25, distDrive=0.45, squash=0.7,
      lfos=[lfo(1, "Position A", 0.9, "1/4", "Saw")], m=[("LFO 1", "Fold", 0.3)], distFold=0.1),
    B("Crushed", "Neuro", "Crushed Saw", 0.5, cutoff=9000.0, distCrush=0.3, distDrive=0.35, squash=0.7,
      lfos=[lfo(1, "Crush", 0.3, "1/8", "Tri"), lfo(2, "Position A", 0.6, "1/4", "Tri")]),
    B("Shredder", "Neuro", "Shred", 0.6, b="Gnarl", bpos=0.5, blevel=0.5, cutoff=8000.0, reso=0.3, distDrive=0.55, distType="Hard", squash=0.8,
      lfos=[lfo(1, "Position A", 0.5, "1/8", "Smooth"), lfo(2, "Cutoff", 0.4, "1/8")], uniVoices=3),
    B("Gnarl", "Neuro", "Gnarl", 0.5, b="Neuro Grind", bpos=0.3, blevel=0.5, bmode="Ring", bmod=0.5, cutoff=7000.0, distDrive=0.5,
      distFold=0.2, squash=0.75, lfos=[lfo(1, "Osc B Mod", 0.5, "1/8", "Tri")], m=[("Velocity", "Distortion", 0.3), ("LFO 1", "Position A", 0.5)]),
]
WOBBLE = [
    B("Wub", "Wobble", "Saw Sweep", 0.9, subLevel=0.25, cutoff=750.0, reso=0.4, filterEnv=0.0, distDrive=0.35, squash=0.5,
      lfos=[lfo(1, "Cutoff", 0.85, "1/8")], m=[("LFO 1", "Position A", 0.4)]),
    B("Slow Wobble", "Wobble", "Square Sweep", 0.8, subLevel=0.25, cutoff=600.0, reso=0.35, filterEnv=0.0, distDrive=0.3, squash=0.5,
      lfos=[lfo(1, "Cutoff", 0.9, "1/4")], m=[("LFO 1", "Position A", 0.5)]),
    B("Triplet Wobble", "Wobble", "Reese Saw", 0.5, cutoff=750.0, reso=0.45, filterEnv=0.0, uniVoices=5, uniDetune=0.35, chorus=0.25,
      distDrive=0.4, squash=0.6, lfos=[lfo(1, "Cutoff", 0.8, "1/8 T")]),
    B("Fast Wobble", "Wobble", "Saw Sweep", 1.0, cutoff=900.0, reso=0.5, filterEnv=0.0, distDrive=0.4, squash=0.55,
      lfos=[lfo(1, "Cutoff", 0.75, "1/16", "Tri")], m=[("LFO 1", "Fold", 0.3)]),
    B("Growl Wobble", "Wobble", "Growl", 0.5, cutoff=1200.0, reso=0.3, filterEnv=0.0, distDrive=0.45, squash=0.65,
      lfos=[lfo(1, "Cutoff", 0.7, "1/8"), lfo(2, "Position A", 0.8, "1/8", "Saw")]),
    B("Ramp Wobble", "Wobble", "Squelch", 0.6, cutoff=900.0, reso=0.3, filterEnv=0.0, distDrive=0.35, squash=0.55,
      lfos=[lfo(1, "Cutoff", 0.9, "1/4", "Saw")], m=[("LFO 1", "Position A", 0.6)]),
    B("Step Wobble", "Wobble", "Saw Sweep", 0.9, cutoff=900.0, reso=0.4, filterEnv=0.0, distDrive=0.35, squash=0.55,
      lfos=[lfo(1, "Cutoff", 0.8, "1/16", "S&H")], m=[("LFO 1", "Position A", 0.4)]),
    B("Dub Wobble", "Wobble", "Sub Sine", 0.8, b="Square Sweep", bpos=0.7, blevel=0.6, cutoff=675.0, reso=0.5, filterEnv=0.0, subLevel=0.35,
      lfos=[lfo(1, "Cutoff", 0.85, "1/4 T")], distDrive=0.25, distType="Tube", squash=0.4, chorus=0.2),
]
FMBASS = [
    B("FM Thump", "FM Bass", "FM 1:1", 0.4, cutoff=5000.0, m=[("Amp Env", "Position A", 0.5)], ampD=0.5, ampS=0.6, squash=0.5, distDrive=0.2,
      lfos=[lfo(1, "Position A", 0.5, "1/4", "Tri")]),
    B("FM Growl", "FM Bass", "FM 1:2", 0.5, b="Basic Shapes", bpos=0.0, blevel=0.6, bmode="FM", bmod=0.5, cutoff=7000.0, squash=0.6,
      distDrive=0.35, lfos=[lfo(1, "Osc B Mod", 0.6, "1/8")]),
    B("Feedback Bass", "FM Bass", "Feedback FM", 1.0, cutoff=6000.0, reso=0.3, subLevel=0.4, filterEnv=0.4, fltD=0.15, fltS=0.2,
      ampD=0.6, ampS=0.5, lfos=[lfo(1, "Position A", 0.5, "1/4", "Tri")], squash=0.55),
    B("Octave FM", "FM Bass", "FM 2:1", 0.5, cutoff=5000.0, m=[("Filter Env", "Position A", -0.4)], lfos=[lfo(2, "Position A", 0.4, "1/2", "Sine")],
      squash=0.5),
    B("FM Pluck Bass", "FM Bass", "FM 1:3", 0.7, ampD=0.35, ampS=0.3, cutoff=6000.0, m=[("Amp Env", "Position A", 0.5)], squash=0.5),
    # the classic sync sweep, each note from bright down to a plain saw (the Hard Sync table: band-limited)
    B("Sync Bass", "FM Bass", "Hard Sync", 0.0, cutoff=5000.0, filterEnv=0.0, fltA=0.002, fltD=0.4, fltS=0.1, m=[("Filter Env", "Position A", 0.7)],
      uniVoices=3, uniDetune=0.2, distType="Tube", distDrive=0.35, squash=0.6, drift=False),
    B("Ring Bass", "FM Bass", "Sub Sine", 0.5, b="Metal Bell", bpos=0.4, blevel=0.5, bmode="Ring", bmod=0.6, cutoff=6000.0,
      lfos=[lfo(1, "Position B", 0.6, "1/8", "Tri")], squash=0.5),
    B("Hard FM", "FM Bass", "FM 1:1", 0.8, b="Feedback FM", bpos=0.5, blevel=0.5, bmode="FM", bmod=0.6, cutoff=8000.0, distDrive=0.45,
      squash=0.7, lfos=[lfo(1, "Osc B Mod", 0.5, "1/8", "Tri")]),
]
PLUCKBASS = [
    B("Pluck Bass", "Pluck Bass", "Saw Sweep", 0.9, ampD=0.35, ampS=0.0, ampR=0.1, cutoff=720.0, filterEnv=0.75, fltD=0.18, fltS=0.2,
      m=[("Filter Env", "Position A", 0.5)], squash=0.4, noiseLevel=0.15, noiseDecay=0.05, lfos=[lfo(2, "Position A", 0.4, "1/4", "Tri")]),
    B("Square Pluck Bass", "Pluck Bass", "Square Sweep", 1.0, ampD=0.4, ampS=0.0, cutoff=630.0, filterEnv=0.7, fltD=0.15, fltS=0.2, reso=0.3,
      squash=0.4, m=[("Filter Env", "Position A", 0.4)], lfos=[lfo(2, "Cutoff", 0.3, "1/4", "Tri")]),
    B("Rubber Bass", "Pluck Bass", "Sub Sine", 0.9, b="Saw Sweep", bpos=0.6, blevel=0.4, ampD=0.5, ampS=0.2, cutoff=540.0, filterEnv=0.65,
      fltD=0.25, fltS=0.2, reso=0.45, m=[("Filter Env", "Position B", 0.5)]),
    B("Thud", "Pluck Bass", "Warm Sine", 0.6, ampD=0.4, ampS=0.0, cutoff=1080.0, filterEnv=0.5, fltD=0.1, fltS=0.2, noiseLevel=0.25,
      noiseDecay=0.06, noiseTone=0.3, distDrive=0.3, distType="Tube",
      m=[("Filter Env", "Position A", 0.6)], lfos=[lfo(2, "Position A", 0.4, "1/4", "Tri")]),
    B("Clicky Bass", "Pluck Bass", "Hard Sync", 0.4, ampD=0.3, ampS=0.2, cutoff=1440.0, filterEnv=0.6, fltD=0.08, fltS=0.2, noiseLevel=0.3,
      noiseDecay=0.04, noiseTone=0.9, m=[("Amp Env", "Position A", 0.4)]),
    B("Funk Pluck", "Pluck Bass", "Phase Saw", 0.6, ampD=0.3, ampS=0.3, cutoff=810.0, filterEnv=0.65, fltD=0.12, fltS=0.2, reso=0.4,
      m=[("Velocity", "Cutoff", 0.3), ("Filter Env", "Position A", 0.4)], velocity=0.5),
    # an auto-wah: a band-pass swept open by every note
    B("Wah Bass", "Pluck Bass", "Saw Sweep", 0.8, filterMode="BP", cutoff=350.0, reso=0.55, filterEnv=0.7, fltA=0.01, fltD=0.3, fltS=0.15,
      ampD=0.5, ampS=0.3, subLevel=0.3, velocity=0.4, m=[("Velocity", "Cutoff", 0.25)], distDrive=0.3, distType="Tube"),
    B("Wood Bass", "Pluck Bass", "Hollow", 0.5, ampD=0.6, ampS=0.0, ampR=0.15, cutoff=1080.0, filterEnv=0.4, fltD=0.2, fltS=0.2,
      noiseLevel=0.15, noiseDecay=0.05, noiseTone=0.5, m=[("Amp Env", "Position A", 0.6)], lfos=[lfo(2, "Cutoff", 0.3, "1/4", "Tri")]),
]
DIGITAL = [
    B("Bit Bass", "Digital Bass", "Bitcrush", 0.5, cutoff=7000.0, distCrush=0.15, lfos=[lfo(1, "Position A", 0.4, "1/8", "Tri")], squash=0.55),
    B("Step Bass", "Digital Bass", "Digital Steps", 0.6, cutoff=3000.0, reso=0.35, distCrush=0.1, squash=0.5,
      lfos=[lfo(1, "Position A", 0.6, "1/8", "S&H"), lfo(2, "Cutoff", 0.4, "1/4", "Square")]),
    B("Pulse Train", "Digital Bass", "Pulse Train", 0.3, cutoff=5000.0, reso=0.2, m=[("Note", "Position A", 0.4)],
      lfos=[lfo(1, "Position A", 0.5, "1/4", "Tri")], squash=0.5),
    B("Squelch", "Digital Bass", "Squelch", 0.3, cutoff=6000.0, m=[("Filter Env", "Position A", 0.6)], fltD=0.25, fltS=0.1,
      lfos=[lfo(2, "Position A", 0.3, "1/8", "Tri")], squash=0.55, distDrive=0.3),
    B("Comb Bass", "Digital Bass", "Comb", 0.4, cutoff=3000.0, reso=0.3, lfos=[lfo(1, "Position A", 1.0, "1/4", "Tri"), lfo(2, "Cutoff", 0.5, "1/2", "Sine")],
      squash=0.55, uniVoices=3, chorus=0.2),
    B("Sync Sweep", "Digital Bass", "Hard Sync", 0.2, cutoff=8000.0, lfos=[lfo(1, "Position A", 0.8, "1/4", "Saw")], distDrive=0.35, squash=0.6),
    B("Reso Bass", "Digital Bass", "PD Reso", 0.4, cutoff=6000.0, m=[("Filter Env", "Position A", 0.5)], fltD=0.2, fltS=0.0,
      lfos=[lfo(1, "Position A", 0.4, "1/8", "Tri")], squash=0.5),
    B("Harmonic Bass", "Digital Bass", "Harmonic Sweep", 0.3, b="Sub Sine", bpos=0.5, blevel=0.5, cutoff=6000.0,
      lfos=[lfo(1, "Position A", 0.6, "1/8", "Tri")], squash=0.5),
]

# ------------------------------------------------------------------------------------------------ the others
def poly(**kw):
    return dict(voiceMode="Poly", subLevel=0.0, monoBass=0.0, **kw)


LEAD = [
    W("Saw Lead", "Lead", "Saw Sweep", 1.0, voiceMode="Legato", subLevel=0.0, monoBass=0.0, uniVoices=5, uniDetune=0.25, uniWidth=0.6,
      cutoff=5000.0, reso=0.15, ampR=0.2),
    W("Sync Lead", "Lead", "Hard Sync", 0.1, voiceMode="Legato", subLevel=0.0, monoBass=0.0, filterMode="LP 12", cutoff=5500.0, reso=0.1,
      filterEnv=0.0, fltA=0.002, fltD=0.7, fltS=0.3, m=[("Filter Env", "Position A", 0.45), ("Mod Wheel", "Position A", 0.3)],
      uniVoices=3, uniDetune=0.15, uniWidth=0.6, distDrive=0.25, distType="Tube", ampR=0.3, velocity=0.4,
      lfos=[lfo(2, "Position A", 0.12, "1/2", "Tri", fade=0.6)]),
    W("Square Lead", "Lead", "Square Sweep", 0.9, voiceMode="Legato", subLevel=0.0, monoBass=0.0, uniVoices=3, uniDetune=0.2, cutoff=4000.0, ampR=0.2),
    W("Growl Lead", "Lead", "Growl", 0.6, voiceMode="Legato", subLevel=0.0, monoBass=0.0, cutoff=5000.0, distDrive=0.3, ampR=0.2,
      lfos=[lfo(1, "Position A", 0.4, "1/8")]),
    W("FM Lead", "Lead", "FM 1:2", 0.5, voiceMode="Legato", subLevel=0.0, monoBass=0.0, cutoff=6000.0, ampR=0.25,
      m=[("Mod Wheel", "Position A", 0.5)]),
    W("Vowel Lead", "Lead", "Vowel A-O", 0.3, voiceMode="Legato", subLevel=0.0, monoBass=0.0, cutoff=5000.0, ampR=0.3,
      lfos=[lfo(1, "Position A", 0.5, "1/2", "Tri")]),
    W("Hollow Lead", "Lead", "Hollow", 0.7, voiceMode="Legato", subLevel=0.0, monoBass=0.0, uniVoices=3, uniDetune=0.15, cutoff=3500.0, ampR=0.25),
    W("Screamer", "Lead", "Neuro Bite", 0.5, voiceMode="Legato", subLevel=0.0, monoBass=0.0, cutoff=7000.0, reso=0.3, distDrive=0.5,
      ampR=0.2),
]
PAD = [
    W("Glass Pad", "Pad", "Sine Harmonics", 0.4, **poly(uniVoices=3, uniDetune=0.2, uniWidth=0.9, ampA=0.8, ampD=1.0, ampS=0.8, ampR=1.6,
      cutoff=3000.0, filterEnv=0.1, lfos=[lfo(1, "Position A", 0.3, "2 bar", "Tri")])),
    W("Warm Pad", "Pad", "Saw Sweep", 0.5, **poly(uniVoices=3, uniDetune=0.3, uniWidth=1.0, ampA=1.0, ampR=2.0, cutoff=1500.0, filterEnv=0.1)),
    W("Vowel Pad", "Pad", "Talk", 0.3, **poly(uniVoices=2, uniDetune=0.2, ampA=1.2, ampR=2.0, cutoff=2500.0,
      lfos=[lfo(1, "Position A", 0.5, "2 bar", "Smooth")])),
    W("Organ Pad", "Pad", "Organ", 0.6, **poly(ampA=0.4, ampR=1.0, cutoff=4000.0, filterEnv=0.0)),
    W("Moving Pad", "Pad", "Spectral", 0.3, **poly(uniVoices=3, uniDetune=0.2, uniWidth=0.8, ampA=1.5, ampR=2.5, cutoff=3000.0,
      lfos=[lfo(1, "Position A", 0.6, "2 bar", "Smooth"), lfo(2, "Cutoff", 0.2, "1 bar", "Sine")])),
    W("Hollow Pad", "Pad", "Odd Harmonics", 0.4, **poly(uniVoices=2, uniDetune=0.15, ampA=0.9, ampR=1.8, cutoff=2200.0)),
    W("Choir Pad", "Pad", "Vowel E-I", 0.4, **poly(uniVoices=3, uniDetune=0.25, uniWidth=1.0, ampA=1.4, ampR=2.2, cutoff=3500.0,
      lfos=[lfo(1, "Position A", 0.3, "1 bar", "Sine")])),
    W("Dark Pad", "Pad", "Throat", 0.3, **poly(uniVoices=3, uniDetune=0.3, ampA=1.6, ampR=2.5, cutoff=900.0, reso=0.2)),
]
PLUCK = [
    W("Glass Pluck", "Pluck", "Sine Harmonics", 0.8, **poly(ampD=0.4, ampS=0.0, ampR=0.3, cutoff=1500.0, filterEnv=0.6, fltD=0.2, fltS=0.0)),
    W("Saw Pluck", "Pluck", "Saw Sweep", 1.0, **poly(uniVoices=3, uniDetune=0.2, ampD=0.35, ampS=0.0, ampR=0.25, cutoff=800.0, filterEnv=0.7,
      fltD=0.18, fltS=0.0)),
    W("FM Pluck", "Pluck", "FM 1:3", 0.6, **poly(ampD=0.3, ampS=0.0, ampR=0.3, cutoff=5000.0, filterEnv=0.0, m=[("Amp Env", "Position A", 0.6)])),
    W("Hollow Pluck", "Pluck", "Hollow", 0.8, **poly(ampD=0.4, ampS=0.0, cutoff=1200.0, filterEnv=0.5, fltD=0.2, fltS=0.0)),
    W("Digital Pluck", "Pluck", "Bitcrush", 0.7, **poly(ampD=0.6, ampS=0.1, ampR=0.25, cutoff=6000.0, reso=0.2, filterEnv=0.3, fltD=0.15, fltS=0.0,
      distCrush=0.15, m=[("Amp Env", "Position A", 0.5)])),
    W("Vowel Pluck", "Pluck", "Vowel A-O", 0.6, **poly(ampD=0.3, ampS=0.0, cutoff=2500.0, filterEnv=0.3, m=[("Filter Env", "Position A", -0.5)])),
    W("Sync Pluck", "Pluck", "Hard Sync", 0.6, **poly(ampD=0.3, ampS=0.0, cutoff=2000.0, filterEnv=0.5, fltD=0.15, fltS=0.0,
      m=[("Filter Env", "Position A", 0.4)])),
    W("Comb Pluck", "Pluck", "Comb", 0.0, b="Sine Harmonics", bpos=0.6, blevel=0.6, **poly(oscALevel=1.0, ampD=1.2, ampS=0.3, ampR=0.3,
      cutoff=8000.0, filterEnv=0.2, fltD=0.2, fltS=0.0,
      m=[("Amp Env", "Position A", 0.5)])),
]
KEYS = [
    W("Wave Keys", "Keys", "Tri to Square", 0.3, **poly(ampD=1.2, ampS=0.4, ampR=0.4, cutoff=2600.0, filterEnv=0.3, fltD=0.5)),
    W("FM Keys", "Keys", "FM 1:1", 0.4, **poly(ampD=1.5, ampS=0.2, ampR=0.5, cutoff=5000.0, filterEnv=0.0, m=[("Amp Env", "Position A", 0.5)])),
    W("Organ Keys", "Keys", "Organ", 0.8, **poly(ampD=0.5, ampS=0.9, ampR=0.15, cutoff=6000.0, filterEnv=0.0)),
    W("Tine Keys", "Keys", "Tine", 0.7, **poly(ampD=1.8, ampS=0.1, ampR=0.6, cutoff=6000.0, filterEnv=0.0, m=[("Velocity", "Position A", 0.4)],
      velocity=0.5)),
    W("Soft Keys", "Keys", "Warm Sine", 0.4, **poly(ampD=1.0, ampS=0.5, ampR=0.4, cutoff=3000.0)),
    W("Phase Keys", "Keys", "Phase Saw", 0.5, **poly(ampD=1.0, ampS=0.3, ampR=0.4, cutoff=2500.0, filterEnv=0.3,
      m=[("Amp Env", "Position A", 0.4)])),
    W("Pulse Keys", "Keys", "PWM", 0.5, **poly(ampD=0.9, ampS=0.4, ampR=0.3, cutoff=2800.0, lfos=[lfo(1, "Position A", 0.3, "1/2", "Tri")])),
    W("Even Keys", "Keys", "Even Harmonics", 0.5, **poly(ampD=1.0, ampS=0.3, ampR=0.4, cutoff=3500.0, filterEnv=0.2)),
]
BELL = [
    W("Metal Bell", "Bell", "Metal Bell", 0.6, **poly(ampD=2.5, ampS=0.0, ampR=1.5, cutoff=8000.0, filterEnv=0.0)),
    W("Clang Bell", "Bell", "Clang", 0.7, **poly(ampD=2.0, ampS=0.0, ampR=1.2, cutoff=7000.0, filterEnv=0.0, m=[("Amp Env", "Position A", 0.4)])),
    W("Chime", "Bell", "Chime", 0.6, **poly(ampD=2.8, ampS=0.0, ampR=1.8, cutoff=9000.0, filterEnv=0.0)),
    W("Gong", "Bell", "Gong", 0.6, **poly(ampA=0.01, ampD=4.0, ampS=0.0, ampR=3.0, cutoff=6000.0, filterEnv=0.0,
      lfos=[lfo(1, "Position A", 0.3, "1 bar", "Smooth")])),
    W("Alloy", "Bell", "Alloy", 0.5, **poly(ampD=2.0, ampS=0.0, ampR=1.2, cutoff=8000.0, filterEnv=0.0)),
    W("Tin Bell", "Bell", "Tin", 0.5, **poly(ampD=1.6, ampS=0.0, ampR=1.0, cutoff=7000.0, filterEnv=0.0)),
    W("Ring Bell", "Bell", "Ring Mod", 0.4, **poly(ampD=2.2, ampS=0.0, ampR=1.4, cutoff=8000.0, filterEnv=0.0)),
    W("FM Bell", "Bell", "FM 1:3", 0.8, b="Metal Bell", bpos=0.5, blevel=0.4, **poly(ampD=2.4, ampS=0.0, ampR=1.5, cutoff=9000.0,
      filterEnv=0.0, m=[("Amp Env", "Position A", 0.5)])),
]
STAB = [
    W("Reese Stab", "Stab", "Reese Saw", 0.5, **poly(uniVoices=3, uniDetune=0.3, ampD=0.3, ampS=0.0, ampR=0.2, cutoff=1600.0, filterEnv=0.5,
      fltD=0.15, fltS=0.0, distDrive=0.3)),
    W("Growl Stab", "Stab", "Growl", 0.7, **poly(ampD=0.25, ampS=0.0, ampR=0.2, cutoff=3000.0, distDrive=0.4, m=[("Amp Env", "Position A", 0.5)])),
    W("Neuro Stab", "Stab", "Neuro Grind", 0.6, **poly(ampD=0.25, ampS=0.0, cutoff=4000.0, distDrive=0.5, distFold=0.2)),
    W("Rave Stab", "Stab", "Saw Sweep", 1.0, **poly(uniVoices=5, uniDetune=0.35, uniWidth=1.0, ampD=0.35, ampS=0.0, ampR=0.3, cutoff=2500.0,
      filterEnv=0.3, fltD=0.2, fltS=0.0)),
    W("Organ Stab", "Stab", "Organ", 1.0, **poly(ampD=0.4, ampS=0.0, ampR=0.2, cutoff=5000.0, distDrive=0.25, distType="Tube")),
    W("Square Stab", "Stab", "Square Sweep", 1.0, **poly(ampD=0.3, ampS=0.0, cutoff=1400.0, filterEnv=0.6, fltD=0.12, fltS=0.0, reso=0.3)),
    W("FM Stab", "Stab", "FM 1:2", 0.7, **poly(ampD=0.3, ampS=0.0, cutoff=5000.0, filterEnv=0.0, m=[("Amp Env", "Position A", 0.4)])),
    W("Crush Stab", "Stab", "Crushed Saw", 0.6, **poly(ampD=0.3, ampS=0.0, cutoff=4000.0, distCrush=0.25)),
]
TEXTURE = [
    W("Noise Bed", "Texture", "Noise Hash", 0.5, **poly(ampA=1.0, ampR=2.0, cutoff=3000.0, noiseLevel=0.3, noiseType="Pink", noiseDecay=4.0,
      lfos=[lfo(1, "Position A", 0.5, "2 bar", "Smooth")])),
    W("Spectral Drift", "Texture", "Spectral", 0.2, **poly(uniVoices=3, uniDetune=0.2, ampA=1.4, ampR=2.4, cutoff=4000.0,
      lfos=[lfo(1, "Position A", 0.8, "2 bar", "Smooth")])),
    W("Shimmer", "Texture", "Harmonic Sweep", 0.5, **poly(ampA=0.8, ampR=2.0, cutoff=8000.0, lfos=[lfo(1, "Position A", 0.7, "1 bar", "Tri")])),
    W("Air", "Texture", "Sine Harmonics", 0.2, **poly(ampA=1.2, ampR=2.0, cutoff=6000.0, filterMode="HP", noiseLevel=0.4, noiseDecay=4.0,
      noiseTone=0.8)),
    W("Breath", "Texture", "Vowel E-I", 0.3, **poly(ampA=0.6, ampR=1.2, cutoff=2000.0, filterMode="BP", reso=0.3, noiseLevel=0.35,
      noiseDecay=4.0)),
    W("Grain", "Texture", "Shred", 0.4, **poly(ampA=0.5, ampR=1.5, cutoff=3500.0, lfos=[lfo(1, "Position A", 0.4, "1/8", "Smooth")])),
    W("Hollow Wind", "Texture", "Comb", 0.5, **poly(ampA=1.5, ampR=2.5, cutoff=2400.0, filterMode="Notch", noiseLevel=0.3, noiseDecay=4.0,
      lfos=[lfo(1, "Cutoff", 0.4, "2 bar", "Smooth")])),
    W("Tape Hum", "Texture", "Warm Sine", 0.6, **poly(ampA=0.8, ampR=1.5, cutoff=1200.0, distDrive=0.2, distType="Tube", noiseLevel=0.1,
      noiseDecay=4.0, noiseTone=0.2)),
]
FX = [
    W("Riser", "FX", "Saw Sweep", 0.2, **poly(ampA=3.0, ampR=1.0, cutoff=400.0, filterEnv=0.8, fltA=4.0, fltS=1.0, noiseLevel=0.3,
      noiseDecay=4.0, lfos=[lfo(1, "Position A", 0.8, "1/16", "Tri", fade=4.0)])),
    W("Downer", "FX", "Saw Sweep", 1.0, **poly(ampD=2.0, ampS=0.0, ampR=1.0, cutoff=8000.0, filterEnv=-0.6, fltD=2.0, fltS=0.0)),
    W("Laser", "FX", "Hard Sync", 1.0, **poly(ampD=0.6, ampS=0.0, cutoff=8000.0, filterEnv=0.0, m=[("Amp Env", "Position A", 0.9)])),
    W("Alarm", "FX", "Square Sweep", 1.0, **poly(cutoff=4000.0, lfos=[lfo(1, "Position A", 0.8, "1/8", "Saw")])),
    W("Zap", "FX", "FM 1:1", 1.0, **poly(ampD=0.2, ampS=0.0, cutoff=9000.0, filterEnv=0.0, m=[("Amp Env", "Position A", 1.0)])),
    W("Robot", "FX", "Talk", 0.5, **poly(cutoff=4000.0, lfos=[lfo(1, "Position A", 0.7, "1/8", "Smooth")], distCrush=0.2)),
    W("Siren", "FX", "Vowel A-O", 0.5, **poly(cutoff=4000.0, lfos=[lfo(1, "Position A", 1.0, "1/2", "Tri"), lfo(2, "Cutoff", 0.4, "1/4")])),
    W("Whoosh", "FX", "Spectral", 0.5, **poly(ampA=0.6, ampR=1.5, filterMode="BP", cutoff=1200.0, reso=0.4, filterEnv=0.0, noiseLevel=0.6,
      noiseType="Pink", lfos=[lfo(1, "Cutoff", 0.8, "1 bar", "Tri")])),
]

WAVETABLE = SUB + REESE + GROWL + NEURO + WOBBLE + FMBASS + PLUCKBASS + DIGITAL + LEAD + PAD + PLUCK + KEYS + BELL + STAB + TEXTURE + FX
assert len(WAVETABLE) == 128, len(WAVETABLE)
assert sum(1 for p in WAVETABLE if p[1] in BASS_FAMS) == 64
assert len({p[0] for p in WAVETABLE}) == 128, "unique names"
