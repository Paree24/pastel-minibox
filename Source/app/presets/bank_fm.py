"""FM factory bank: 8 families × 16 hand-designed patches on the 6-operator engine.

F(name, family, algorithm, feedback, ops, extras): ops = six (ratio, level, envelope, wave) tuples for
OP1..OP6 (None = silent). Routing used (OP a → OP b means a modulates b):
  1: 2→1, 6→5→4→3          5: 2→1, 4→3, 6→5 (three stacks)     7: 2→1, 4→3, 6→5→3
  16: 2→1, 4→3→1, 6→5→1    22: 2→1, 6→3, 6→4, 6→5               24: 6→3, 6→4, 6→5, all others carriers
  31: 6→5, carriers 1–5    32: all six carriers
Envelopes are rate/level shapes (rates 0..1, higher = faster: 0.99 ≈ 2 ms, 0.5 ≈ 0.23 s, 0.3 ≈ 1.7 s).
Waves: 0 sine, 1 half-sine, 2 square-ish, 3 saw-ish.
"""

ENV = {
    # name: (R1, R2, R3, R4, L1, L2, L3)
    "struck": (0.99, 0.42, 0.22, 0.55, 1.0, 0.35, 0.0),
    "knock": (0.99, 0.55, 0.42, 0.7, 1.0, 0.2, 0.0),   # a short hollow knock (≈ 0.15 s), never a bare click
    # electric piano carriers: a quick drop after the hammer, then the tine rings on (≈ 6 dB/s, audible for 8–12 s)
    "epbody": (0.99, 0.34, 0.08, 0.55, 1.0, 0.72, 0.0),
    "eptine": (0.99, 0.42, 0.11, 0.6, 1.0, 0.6, 0.0),
    "tine": (0.99, 0.55, 0.32, 0.6, 1.0, 0.25, 0.0),
    # rates are 96 dB times (0.6 ≈ 85 ms, 0.45 ≈ 0.38 s, 0.27 ≈ 2.3 s): a pluck drops ≈ 10 dB, then rings ≈ 0.5 s
    "pluck": (0.99, 0.45, 0.27, 0.6, 1.0, 0.3, 0.0),
    "muted": (0.99, 0.5, 0.33, 0.6, 1.0, 0.25, 0.0),
    # brass stab: carriers speak in ≈ 20 ms, modulators blare in ≈ 50 ms, then fall back to a body
    "stab": (0.75, 0.45, 0.35, 0.7, 1.0, 0.6, 0.3),
    "stabmod": (0.65, 0.5, 0.38, 0.7, 1.0, 0.55, 0.3),
    "click": (0.99, 0.8, 0.7, 0.7, 1.0, 0.0, 0.0),
    "sus": (0.99, 0.5, 0.5, 0.7, 1.0, 1.0, 1.0),
    "organ": (0.99, 0.6, 0.6, 0.8, 1.0, 1.0, 1.0),
    "pad": (0.45, 0.32, 0.3, 0.4, 1.0, 0.9, 0.85),
    "slowpad": (0.35, 0.28, 0.25, 0.35, 1.0, 0.95, 0.9),
    "brass": (0.72, 0.5, 0.4, 0.6, 1.0, 0.85, 0.8),
    "brassmod": (0.6, 0.45, 0.4, 0.6, 1.0, 0.75, 0.6),
    "fall": (0.99, 0.5, 0.42, 0.6, 1.0, 0.4, 0.2),
    "bell": (0.99, 0.28, 0.17, 0.4, 1.0, 0.55, 0.0),
    "longbell": (0.99, 0.22, 0.2, 0.35, 1.0, 0.6, 0.0),
    "bass": (0.99, 0.48, 0.4, 0.75, 1.0, 0.7, 0.6),
    "bassmod": (0.99, 0.62, 0.5, 0.7, 1.0, 0.35, 0.15),
    "swell": (0.4, 0.4, 0.35, 0.45, 1.0, 1.0, 0.95),
}


def op(r, l, e, w=0):
    return (r, l, e, w)


CARRIERS = {1: "13", 5: "135", 7: "13", 16: "1", 22: "1345", 24: "12345", 31: "12345", 32: "123456"}


def level_param(amount, carrier):
    """The engine's operator Level is logarithmic (gain = dB((level − 1) · 60)). Recipes give carriers a
    linear amplitude and modulators an amount 0..1 (0.2 subtle ≈ 1.3 rad index, 0.5 ≈ 2.6, 0.8 ≈ 5.6)."""
    import math
    if carrier:
        return round(max(0.0, 1.0 + 20.0 * math.log10(max(amount, 1e-3)) / 60.0), 4)
    return round(0.6 + 0.35 * amount, 4)


def F(name, fam, algo, fb, ops, **x):
    v = dict(algorithm=algo, feedback=fb)
    for i, o in enumerate(ops):
        n = i + 1
        if o is None:
            v[f"op{n}Level"] = 0.0
            continue
        r, l, e, w = o
        R1, R2, R3, R4, L1, L2, L3 = ENV[e]
        v.update({f"op{n}Ratio": r, f"op{n}Level": level_param(l, str(n) in CARRIERS[algo]), f"op{n}Wave": w, f"op{n}R1": R1, f"op{n}R2": R2, f"op{n}R3": R3,
                  f"op{n}R4": R4, f"op{n}L1": L1, f"op{n}L2": L2, f"op{n}L3": L3, f"op{n}L4": 0.0})
    v.update(x)
    return (name, fam, v)


CH = lambda mode=0, mix=0.45, rate=0.5, depth=0.5: dict(chorusMode=mode, chorusMix=mix, chorusRate=rate, chorusDepth=depth)
# reverb: the space grows with the send (small room at low mix, hall at high), unless set
RV = lambda mix, size=None, tone=0.55, pre=None: dict(reverb=mix, revSize=min(0.9, 0.25 + 1.1 * mix) if size is None else size,
                                                       revTone=tone, revPre=round(0.008 + 0.05 * mix, 4) if pre is None else pre)
TREM = lambda rate=4.5, amt=0.3: dict(lfoShape=0, lfoRate=rate, lfoAmp=amt)

FM = [
    # ---------------------------------------------------------------- Electric Piano (algo 5: 1←2 body, 3←4 tine, 5←6 attack)
    F("Classic Tine Piano", "Electric Piano", 5, 0.1, [op(1, 0.95, "epbody"), op(1, 0.55, "fall"), op(1, 0.7, "epbody"), op(14, 0.35, "click"), None, None], **CH(0), **RV(0.2)),
    F("Soft Tine Piano", "Electric Piano", 5, 0.0, [op(1, 0.95, "epbody"), op(1, 0.4, "fall"), op(1, 0.6, "epbody"), op(14, 0.2, "click"), None, None], **CH(1, 0.5), **RV(0.25)),
    F("Bright Tine Piano", "Electric Piano", 5, 0.2, [op(1, 0.95, "epbody"), op(1, 0.7, "fall"), op(1, 0.75, "eptine"), op(14, 0.5, "click"), op(3, 0.3, "pluck"), op(7, 0.25, "click")], **CH(0, 0.35), **RV(0.2)),
    F("Tremolo Piano", "Electric Piano", 5, 0.1, [op(1, 0.95, "epbody"), op(1, 0.5, "fall"), op(1, 0.65, "epbody"), op(14, 0.3, "click"), None, None], **TREM(5.0, 0.35), **RV(0.25)),
    F("Dark Mellow Piano", "Electric Piano", 5, 0.0, [op(1, 0.95, "epbody"), op(1, 0.3, "fall"), op(0.5, 0.55, "epbody"), op(1, 0.25, "fall"), None, None], **CH(2, 0.5), **RV(0.3)),
    F("Bell Tine Piano", "Electric Piano", 5, 0.15, [op(1, 0.9, "epbody"), op(1, 0.45, "fall"), op(3, 0.55, "bell"), op(14, 0.35, "click"), None, None], **CH(0, 0.4), **RV(0.3)),
    F("Hard Hammer Piano", "Electric Piano", 5, 0.3, [op(1, 0.95, "epbody"), op(1, 0.75, "fall"), op(1, 0.7, "epbody"), op(14, 0.6, "click"), op(1, 0.3, "click"), op(9, 0.4, "click")], **RV(0.2)),
    F("Chorus Ballad Piano", "Electric Piano", 5, 0.05, [op(1, 0.95, "epbody"), op(1, 0.45, "fall"), op(1, 0.6, "epbody"), op(13, 0.25, "click"), None, None], **CH(2, 0.6), **RV(0.4)),
    F("Reed Piano", "Electric Piano", 1, 0.35, [op(1, 0.95, "epbody"), op(1, 0.6, "fall", 1), op(1, 0.45, "epbody"), op(7, 0.2, "click"), None, None], **TREM(5.5, 0.25), **RV(0.2)),
    F("Glass Piano", "Electric Piano", 5, 0.0, [op(1, 0.9, "epbody"), op(2, 0.45, "fall"), op(4, 0.5, "bell"), op(14, 0.4, "click"), None, None], **CH(1, 0.4), **RV(0.35)),
    F("Funky Plucked Piano", "Electric Piano", 1, 0.5, [op(1, 0.95, "pluck"), op(3, 0.6, "click", 2), op(1, 0.4, "pluck"), op(6, 0.3, "click"), None, None], **CH(0, 0.25)),
    F("Punchy Tine Piano", "Electric Piano", 5, 0.2, [op(1, 0.95, "epbody"), op(1, 0.65, "fall"), op(1, 0.8, "eptine"), op(14, 0.55, "click"), None, None], **CH(0, 0.5), **TREM(3.5, 0.15), **RV(0.25)),
    F("Muted Piano", "Electric Piano", 5, 0.0, [op(1, 0.95, "muted"), op(1, 0.35, "click"), op(1, 0.5, "muted"), op(14, 0.2, "click"), None, None], **RV(0.2)),
    F("Velvet Keys", "Electric Piano", 5, 0.0, [op(1, 0.95, "epbody"), op(0.5, 0.35, "fall"), op(2, 0.4, "epbody"), op(1, 0.3, "fall"), None, None], **CH(2, 0.55), **RV(0.35)),
    F("Toy Piano", "Electric Piano", 5, 0.1, [op(1, 0.9, "pluck"), op(5, 0.5, "click"), op(4, 0.5, "bell"), op(11, 0.3, "click"), None, None], **RV(0.3)),
    F("Layered Stage Piano", "Electric Piano", 5, 0.15, [op(1, 0.95, "epbody"), op(1, 0.5, "fall"), op(1, 0.65, "epbody"), op(14, 0.4, "click"), op(2, 0.4, "epbody"), op(1, 0.3, "fall")], **CH(1, 0.45), **RV(0.3)),
    # ---------------------------------------------------------------- Bell
    F("Tubular Bell", "Bell", 5, 0.0, [op(1, 0.9, "bell"), op(3.5, 0.55, "bell"), op(2, 0.6, "bell"), op(5.4, 0.45, "bell"), None, None], **RV(0.4)),
    F("Church Bell", "Bell", 1, 0.2, [op(1, 0.9, "longbell"), op(1.41, 0.6, "longbell"), op(0.5, 0.7, "longbell"), op(2.76, 0.5, "bell"), op(5.4, 0.3, "bell"), None], **RV(0.5)),
    F("Glass Bell", "Bell", 5, 0.0, [op(1, 0.9, "bell"), op(7, 0.5, "bell"), op(3, 0.6, "bell"), op(11, 0.35, "click"), None, None], **CH(1, 0.3), **RV(0.45)),
    F("Small Bell", "Bell", 5, 0.1, [op(2, 0.85, "bell"), op(5.4, 0.5, "bell"), op(4, 0.6, "pluck"), op(9, 0.4, "click"), None, None], **RV(0.4)),
    F("Gamelan", "Bell", 7, 0.25, [op(1, 0.9, "bell"), op(2.76, 0.55, "bell"), op(1, 0.7, "bell"), op(3.11, 0.5, "bell"), op(5.4, 0.4, "bell"), op(1.41, 0.3, "bell")], **RV(0.45)),
    F("Crystal Chime", "Bell", 5, 0.0, [op(3, 0.85, "bell"), op(7, 0.45, "bell"), op(5, 0.6, "bell"), op(13, 0.3, "click"), op(8, 0.4, "bell"), op(1, 0.2, "click")], **CH(2, 0.5), **RV(0.55)),
    F("Dark Gong", "Bell", 1, 0.45, [op(0.5, 0.9, "longbell"), op(1.41, 0.7, "longbell"), op(1, 0.6, "longbell"), op(1.57, 0.6, "bell"), op(2.39, 0.5, "bell"), None], **RV(0.55)),
    F("Music Box", "Bell", 5, 0.0, [op(2, 0.85, "pluck"), op(7, 0.4, "click"), op(4, 0.55, "bell"), op(11, 0.3, "click"), None, None], **RV(0.4)),
    F("Fairy Bell", "Bell", 5, 0.0, [op(4, 0.8, "bell"), op(9, 0.45, "bell"), op(6, 0.55, "bell"), op(13, 0.35, "bell"), None, None], **CH(2, 0.6), **RV(0.6)),
    F("Bell Pad", "Bell", 5, 0.0, [op(1, 0.85, "pad"), op(3.5, 0.45, "pad"), op(2, 0.6, "bell"), op(5.4, 0.45, "bell"), None, None], **CH(2, 0.5), **RV(0.55)),
    F("Hand Bell", "Bell", 5, 0.1, [op(1, 0.9, "bell"), op(2.76, 0.5, "fall"), op(3, 0.5, "pluck"), op(7.1, 0.35, "click"), None, None], **RV(0.4)),
    F("Inharmonic Bell", "Bell", 1, 0.35, [op(1, 0.9, "bell"), op(3.73, 0.6, "bell"), op(1, 0.6, "bell"), op(4.71, 0.55, "bell"), op(7.1, 0.4, "bell"), None], **RV(0.45)),
    F("Soft Chime", "Bell", 5, 0.0, [op(2, 0.8, "bell"), op(1, 0.3, "bell"), op(3, 0.6, "bell"), op(1, 0.2, "bell"), None, None], **TREM(4.0, 0.2), **RV(0.5)),
    F("Clock Bell", "Bell", 5, 0.2, [op(1, 0.9, "struck"), op(4.71, 0.5, "click"), op(2.39, 0.6, "bell"), op(7.1, 0.45, "click"), None, None], **RV(0.35)),
    F("Wind Chime", "Bell", 32, 0.0, [op(3, 0.6, "bell"), op(4.2, 0.55, "bell"), op(5.4, 0.5, "bell"), op(7.1, 0.45, "bell"), op(8.9, 0.4, "bell"), op(11, 0.35, "bell")], **CH(2, 0.5), **RV(0.6)),
    F("Bell Tone", "Bell", 5, 0.0, [op(1, 0.9, "longbell"), op(1, 0.4, "bell"), op(1, 0.5, "longbell"), op(2, 0.35, "bell"), None, None], **RV(0.45)),
    # ---------------------------------------------------------------- Bass
    F("Solid FM Bass", "Bass", 5, 0.4, [op(1, 0.95, "bass"), op(1, 0.7, "bassmod"), op(0.5, 0.7, "bass"), op(1, 0.4, "bassmod"), None, None]),
    F("Slap Bass", "Bass", 1, 0.5, [op(1, 0.95, "fall"), op(3, 0.6, "click"), op(1, 0.6, "fall"), op(1, 0.6, "bassmod"), None, None]),
    F("Sub Sine Bass", "Bass", 5, 0.0, [op(0.5, 0.95, "bass"), op(1, 0.25, "bassmod"), op(1, 0.5, "bass"), None, None, None]),
    F("Wood Bass", "Bass", 5, 0.2, [op(1, 0.95, "fall"), op(1, 0.45, "click"), op(0.5, 0.7, "fall"), op(2, 0.3, "click"), None, None], **RV(0.1)),
    F("Synth FM Bass", "Bass", 1, 0.8, [op(1, 0.95, "bass"), op(1, 0.8, "bassmod"), op(0.5, 0.6, "bass"), op(1, 0.5, "bassmod"), None, None]),
    F("Gritty FM Bass", "Bass", 16, 0.7, [op(1, 0.95, "bass"), op(1, 0.65, "bassmod", 3), op(2, 0.5, "bassmod"), op(1, 0.4, "bassmod"), op(0.5, 0.5, "bassmod"), op(1, 0.4, "bassmod")]),
    F("Pluck FM Bass", "Bass", 5, 0.3, [op(1, 0.95, "pluck"), op(1, 0.7, "click"), op(0.5, 0.7, "pluck"), None, None, None]),
    F("Square FM Bass", "Bass", 5, 0.2, [op(1, 0.9, "bass", 2), op(1, 0.5, "bassmod"), op(0.5, 0.6, "bass"), None, None, None]),
    F("Rubber FM Bass", "Bass", 1, 0.3, [op(1, 0.95, "bass"), op(0.5, 0.75, "fall"), op(1, 0.5, "bass"), op(2, 0.4, "fall"), None, None]),
    F("Fretless", "Bass", 5, 0.1, [op(1, 0.95, "bass"), op(1, 0.4, "brassmod"), op(0.5, 0.5, "bass"), op(1, 0.2, "brassmod"), None, None], **CH(1, 0.3)),
    F("Metallic Bass", "Bass", 1, 0.9, [op(1, 0.95, "bass"), op(3, 0.6, "bassmod"), op(1, 0.6, "bass"), op(1.41, 0.5, "bassmod"), None, None]),
    F("Saw FM Bass", "Bass", 5, 0.3, [op(1, 0.9, "bass", 3), op(2, 0.5, "bassmod"), op(0.5, 0.6, "bass"), None, None, None]),
    F("Punch Bass", "Bass", 16, 0.4, [op(1, 0.95, "fall"), op(1, 0.7, "click"), op(2, 0.4, "click"), op(1, 0.3, "click"), op(0.5, 0.5, "bassmod"), op(1, 0.3, "click")]),
    F("Deep Organ Bass", "Bass", 32, 0.0, [op(0.5, 0.9, "organ"), op(1, 0.6, "organ"), op(1.5, 0.3, "organ"), None, None, None]),
    F("Wobble FM Bass", "Bass", 5, 0.5, [op(1, 0.95, "bass"), op(1, 0.7, "sus"), op(0.5, 0.6, "bass"), None, None, None], lfoShape=0, lfoRate=3.0, lfoAmp=0.4),
    F("Hollow FM Bass", "Bass", 5, 0.2, [op(1, 0.9, "bass", 1), op(2, 0.5, "bassmod"), op(0.5, 0.6, "bass", 1), None, None, None]),
    # ---------------------------------------------------------------- Brass (algo 22 / 18: modulators swell with the note)
    F("FM Brass Section", "Brass", 22, 0.6, [op(1, 0.9, "brass"), op(1, 0.7, "brassmod"), op(1, 0.7, "brass"), op(1, 0.65, "brass"), op(1, 0.6, "brass"), op(1, 0.6, "brassmod")], **CH(1, 0.3), **RV(0.25)),
    F("Soft Horns", "Brass", 22, 0.3, [op(1, 0.9, "brass"), op(1, 0.5, "brassmod"), op(1, 0.6, "brass"), op(1, 0.5, "brass"), op(0.5, 0.4, "brass"), op(1, 0.45, "brassmod")], **CH(2, 0.4), **RV(0.3)),
    F("Bright Trumpet", "Brass", 1, 0.5, [op(1, 0.95, "brass"), op(1, 0.75, "brassmod"), op(1, 0.4, "brass"), op(2, 0.4, "brassmod"), None, None], **TREM(5.5, 0.12), **RV(0.25)),
    F("Brass Stab", "Brass", 22, 0.6, [op(1, 0.9, "stab"), op(1, 0.7, "stabmod"), op(1, 0.7, "stab"), op(1, 0.6, "stab"), op(1, 0.6, "stab"), op(1, 0.6, "stabmod")], **RV(0.25)),
    F("Tuba", "Brass", 1, 0.4, [op(0.5, 0.95, "brass"), op(0.5, 0.6, "brassmod"), op(1, 0.3, "brass"), None, None, None], **RV(0.15)),
    F("French Horn", "Brass", 1, 0.2, [op(1, 0.9, "swell"), op(1, 0.45, "swell"), op(1, 0.35, "swell"), op(2, 0.2, "swell"), None, None], **CH(2, 0.35), **RV(0.4)),
    F("Synth Brass", "Brass", 22, 0.8, [op(1, 0.9, "brass", 3), op(1, 0.6, "brassmod"), op(1, 0.7, "brass"), op(2, 0.5, "brass", 3), op(1, 0.6, "brass"), op(1, 0.55, "brassmod")], **CH(1, 0.5), **RV(0.2)),
    F("Muted Trumpet", "Brass", 1, 0.3, [op(1, 0.9, "brass"), op(2, 0.5, "brassmod"), op(1, 0.35, "brass"), op(3, 0.3, "click"), None, None], **RV(0.25)),
    F("Saxophone", "Brass", 1, 0.5, [op(1, 0.95, "brass"), op(1, 0.65, "brassmod", 1), op(1, 0.3, "brass"), op(3, 0.3, "brassmod"), None, None], **TREM(5.0, 0.1), **RV(0.3)),
    F("Trombone", "Brass", 1, 0.45, [op(0.5, 0.95, "brass"), op(0.5, 0.65, "brassmod"), op(1, 0.35, "brass"), op(1, 0.3, "brassmod"), None, None], **RV(0.25)),
    F("Brass Pad", "Brass", 22, 0.4, [op(1, 0.85, "pad"), op(1, 0.5, "pad"), op(1, 0.6, "pad"), op(1, 0.5, "pad"), op(0.5, 0.4, "pad"), op(1, 0.4, "pad")], **CH(2, 0.6), **RV(0.45)),
    F("Fanfare", "Brass", 22, 0.7, [op(1, 0.9, "brass"), op(1, 0.75, "fall"), op(1, 0.7, "brass"), op(1, 0.65, "brass"), op(2, 0.5, "brass"), op(1, 0.7, "fall")], **RV(0.35)),
    F("Harmon Mute", "Brass", 1, 0.6, [op(1, 0.9, "brass", 1), op(3, 0.55, "brassmod"), op(1, 0.3, "brass"), None, None, None], **RV(0.2)),
    F("Big Band Brass", "Brass", 22, 0.5, [op(1, 0.9, "brass"), op(1, 0.7, "brassmod"), op(1, 0.7, "brass"), op(1, 0.65, "brass"), op(0.5, 0.55, "brass"), op(1, 0.6, "brassmod")], **CH(2, 0.45), **RV(0.3)),
    F("Square Brass", "Brass", 1, 0.3, [op(1, 0.9, "brass", 2), op(1, 0.4, "brassmod"), op(0.5, 0.4, "brass", 2), None, None, None], **CH(1, 0.4), **RV(0.2)),
    F("Distant Horns", "Brass", 22, 0.2, [op(1, 0.85, "slowpad"), op(1, 0.4, "slowpad"), op(1, 0.5, "slowpad"), op(1, 0.45, "slowpad"), None, op(1, 0.35, "slowpad")], **CH(2, 0.5), **RV(0.6)),
    # ---------------------------------------------------------------- Organ (all-carrier / fan algorithms: drawbar footages)
    F("Full Drawbars", "Organ", 32, 0.0, [op(0.5, 0.8, "organ"), op(1, 0.8, "organ"), op(1.5, 0.6, "organ"), op(2, 0.6, "organ"), op(3, 0.5, "organ"), op(4, 0.5, "organ")], **CH(2, 0.5), **RV(0.25)),
    F("Jazz Organ", "Organ", 32, 0.0, [op(0.5, 0.8, "organ"), op(1, 0.8, "organ"), op(1.5, 0.7, "organ"), op(3, 0.2, "click"), None, None], **TREM(6.5, 0.12), **RV(0.2)),
    F("Gospel Organ", "Organ", 32, 0.1, [op(0.5, 0.8, "organ"), op(1, 0.8, "organ"), op(2, 0.7, "organ"), op(3, 0.5, "organ"), op(4, 0.4, "organ"), op(6, 0.3, "organ")], **CH(2, 0.6), **TREM(6.0, 0.15), **RV(0.3)),
    F("Church Pipes", "Organ", 32, 0.0, [op(0.5, 0.8, "swell"), op(1, 0.8, "swell"), op(2, 0.6, "swell"), op(4, 0.5, "swell"), op(8, 0.3, "swell"), None], **RV(0.6)),
    F("Percussive Organ", "Organ", 32, 0.0, [op(0.5, 0.75, "organ"), op(1, 0.8, "organ"), op(3, 0.6, "pluck"), op(2, 0.5, "organ"), None, None], **CH(1, 0.4), **RV(0.2)),
    F("Rock Organ", "Organ", 24, 0.5, [op(0.5, 0.8, "organ"), op(1, 0.8, "organ"), op(2, 0.6, "organ"), op(1, 0.6, "organ"), op(3, 0.5, "organ"), op(1, 0.35, "organ")], **TREM(6.5, 0.2), **RV(0.2)),
    F("Reed Organ", "Organ", 31, 0.3, [op(1, 0.8, "organ"), op(2, 0.5, "organ"), op(1, 0.6, "organ"), op(3, 0.4, "organ"), op(1, 0.5, "organ"), op(2, 0.4, "organ")], **CH(1, 0.4), **RV(0.3)),
    F("Flute Organ", "Organ", 32, 0.0, [op(1, 0.8, "swell"), op(2, 0.4, "swell"), op(4, 0.25, "swell"), None, None, None], **TREM(5.0, 0.1), **RV(0.35)),
    F("Combo Organ", "Organ", 32, 0.0, [op(1, 0.8, "organ", 2), op(2, 0.5, "organ", 2), op(4, 0.3, "organ"), None, None, None], **TREM(7.0, 0.2), **RV(0.2)),
    F("Pipe Mixture", "Organ", 32, 0.0, [op(1, 0.7, "organ"), op(2, 0.6, "organ"), op(3, 0.5, "organ"), op(4, 0.5, "organ"), op(6, 0.4, "organ"), op(8, 0.35, "organ")], **RV(0.55)),
    F("Soft Drawbars", "Organ", 32, 0.0, [op(1, 0.8, "organ"), op(2, 0.4, "organ"), op(0.5, 0.5, "organ"), None, None, None], **CH(2, 0.6), **RV(0.3)),
    F("Square Organ", "Organ", 31, 0.0, [op(1, 0.8, "organ", 2), op(2, 0.4, "organ", 2), op(0.5, 0.5, "organ"), op(4, 0.3, "organ"), op(1, 0.4, "organ"), op(3, 0.3, "organ")], **CH(1, 0.4), **RV(0.25)),
    F("Bright Drawbars", "Organ", 32, 0.0, [op(1, 0.7, "organ"), op(2, 0.7, "organ"), op(3, 0.6, "organ"), op(4, 0.6, "organ"), op(6, 0.5, "organ"), op(8, 0.5, "organ")], **CH(2, 0.4), **RV(0.2)),
    F("Dark Organ", "Organ", 32, 0.0, [op(0.5, 0.85, "organ"), op(1, 0.6, "organ"), op(1.5, 0.4, "organ"), None, None, None], **CH(1, 0.5), **RV(0.35)),
    F("Click Organ", "Organ", 32, 0.0, [op(0.5, 0.8, "organ"), op(1, 0.8, "organ"), op(2, 0.6, "organ"), op(8, 0.4, "click"), op(10, 0.3, "click"), None], **TREM(6.0, 0.12), **RV(0.2)),
    F("Calliope", "Organ", 32, 0.0, [op(1, 0.85, "organ", 1), op(2, 0.5, "organ", 1), op(3, 0.3, "organ"), None, None, None], **TREM(4.0, 0.25), **RV(0.3)),
    # ---------------------------------------------------------------- Pad
    F("Warm FM Pad", "Pad", 5, 0.0, [op(1, 0.85, "pad"), op(1, 0.4, "pad"), op(1, 0.7, "pad"), op(2, 0.3, "pad"), op(0.5, 0.6, "pad"), None], **CH(2, 0.6), **RV(0.45)),
    F("Glass Pad", "Pad", 5, 0.0, [op(1, 0.8, "slowpad"), op(3, 0.4, "slowpad"), op(2, 0.6, "slowpad"), op(5, 0.3, "slowpad"), None, None], **CH(2, 0.6), **RV(0.55)),
    F("Breathing Pad", "Pad", 5, 0.1, [op(1, 0.85, "pad"), op(1, 0.5, "pad"), op(1, 0.7, "pad"), op(2, 0.4, "pad"), None, None], lfoShape=0, lfoRate=0.3, lfoAmp=0.35, **CH(1, 0.5), **RV(0.45)),
    F("Choir Pad", "Pad", 7, 0.0, [op(1, 0.85, "slowpad"), op(2, 0.3, "slowpad"), op(1, 0.7, "slowpad"), op(3, 0.25, "slowpad"), op(5, 0.2, "slowpad"), op(1, 0.2, "slowpad")], **CH(2, 0.7), **RV(0.55)),
    F("String Pad", "Pad", 5, 0.2, [op(1, 0.85, "pad"), op(1, 0.55, "pad", 3), op(1, 0.7, "pad"), op(1, 0.5, "pad", 3), None, None], **CH(2, 0.7), **RV(0.45)),
    F("Bell Swell Pad", "Pad", 5, 0.0, [op(1, 0.8, "swell"), op(3.5, 0.35, "swell"), op(2, 0.6, "slowpad"), op(7, 0.25, "slowpad"), None, None], **CH(2, 0.5), **RV(0.6)),
    F("Hollow Pad", "Pad", 5, 0.0, [op(1, 0.85, "pad", 1), op(2, 0.35, "pad"), op(1, 0.6, "pad", 1), None, None, None], **CH(1, 0.6), **RV(0.5)),
    F("Dark Drone Pad", "Pad", 1, 0.3, [op(0.5, 0.85, "slowpad"), op(1, 0.45, "slowpad"), op(1, 0.5, "slowpad"), op(1.5, 0.35, "slowpad"), op(2, 0.25, "slowpad"), None], **CH(2, 0.6), **RV(0.6)),
    F("Shimmer Pad", "Pad", 5, 0.0, [op(2, 0.8, "slowpad"), op(4, 0.35, "slowpad"), op(4, 0.5, "slowpad"), op(9, 0.3, "slowpad"), op(1, 0.6, "slowpad"), None], **CH(2, 0.7), **RV(0.65)),
    F("Square Pad", "Pad", 5, 0.0, [op(1, 0.8, "pad", 2), op(1, 0.3, "pad"), op(0.5, 0.6, "pad"), None, None, None], **CH(2, 0.6), **RV(0.45)),
    F("Organ Pad", "Pad", 32, 0.0, [op(0.5, 0.7, "slowpad"), op(1, 0.7, "slowpad"), op(2, 0.5, "slowpad"), op(3, 0.4, "slowpad"), None, None], **CH(2, 0.6), **RV(0.5)),
    F("Tremolo Pad", "Pad", 5, 0.0, [op(1, 0.85, "pad"), op(2, 0.35, "pad"), op(1, 0.6, "pad"), None, None, None], **TREM(5.0, 0.4), **CH(1, 0.5), **RV(0.45)),
    F("Metallic Pad", "Pad", 1, 0.5, [op(1, 0.85, "slowpad"), op(1.41, 0.5, "slowpad"), op(1, 0.5, "slowpad"), op(2.39, 0.4, "slowpad"), None, None], **CH(2, 0.5), **RV(0.55)),
    F("Saw Pad", "Pad", 5, 0.3, [op(1, 0.8, "pad", 3), op(1, 0.4, "pad"), op(2, 0.45, "pad", 3), None, None, None], **CH(2, 0.7), **RV(0.45)),
    F("Soft Air Pad", "Pad", 5, 0.0, [op(1, 0.8, "slowpad"), op(1, 0.2, "slowpad"), op(2, 0.5, "slowpad"), op(1, 0.15, "slowpad"), None, None], **CH(2, 0.7), **RV(0.65)),
    F("Evolving Pad", "Pad", 16, 0.2, [op(1, 0.85, "slowpad"), op(1, 0.4, "swell"), op(2, 0.35, "slowpad"), op(3, 0.3, "pad"), op(1, 0.3, "slowpad"), op(0.5, 0.3, "swell")],
      lfoShape=1, lfoRate=0.2, lfoAmp=0.25, **CH(2, 0.6), **RV(0.55)),
    # ---------------------------------------------------------------- Mallet
    F("Marimba", "Mallet", 5, 0.0, [op(1, 0.95, "pluck"), op(4, 0.4, "click"), op(1, 0.6, "pluck"), op(10, 0.3, "click"), None, None], **RV(0.3)),
    F("Vibraphone", "Mallet", 5, 0.0, [op(1, 0.9, "bell"), op(4, 0.35, "click"), op(1, 0.6, "bell"), None, None, None], **TREM(5.0, 0.3), **RV(0.4)),
    F("Xylophone", "Mallet", 5, 0.1, [op(1, 0.9, "pluck"), op(3, 0.5, "click"), op(4, 0.6, "pluck"), op(10, 0.35, "click"), None, None], **RV(0.3)),
    F("Kalimba", "Mallet", 5, 0.0, [op(1, 0.9, "pluck"), op(5.4, 0.35, "click"), op(2, 0.5, "pluck"), op(11, 0.25, "click"), None, None], **RV(0.35)),
    F("Steel Drum", "Mallet", 5, 0.2, [op(1, 0.9, "struck"), op(2.32, 0.5, "fall"), op(1, 0.6, "struck"), op(1.5, 0.45, "fall"), None, None], **RV(0.35)),
    F("Wood Block", "Mallet", 5, 0.0, [op(1, 0.9, "knock"), op(3, 0.6, "click"), op(2.4, 0.5, "knock"), None, None, None], **RV(0.2)),
    F("Glockenspiel", "Mallet", 5, 0.0, [op(2, 0.85, "bell"), op(7, 0.35, "click"), op(5.4, 0.5, "bell"), None, None, None], **RV(0.45)),
    F("Soft Mallet", "Mallet", 5, 0.0, [op(1, 0.9, "struck"), op(1, 0.25, "click"), op(2, 0.4, "struck"), None, None, None], **CH(1, 0.4), **RV(0.4)),
    F("Log Drum", "Mallet", 5, 0.1, [op(1, 0.95, "fall"), op(2, 0.45, "click"), op(0.5, 0.6, "fall"), None, None, None], **RV(0.3)),
    F("Celesta", "Mallet", 5, 0.0, [op(2, 0.85, "bell"), op(4, 0.3, "click"), op(1, 0.5, "bell"), op(6, 0.25, "click"), None, None], **RV(0.4)),
    F("Balafon", "Mallet", 1, 0.3, [op(1, 0.9, "pluck"), op(3, 0.5, "click"), op(1, 0.5, "pluck"), op(2.4, 0.4, "click", 1), None, None], **RV(0.3)),
    F("Mbira", "Mallet", 5, 0.1, [op(1, 0.9, "pluck"), op(6, 0.4, "click"), op(3, 0.45, "pluck"), op(9, 0.3, "click"), None, None], **TREM(6.0, 0.15), **RV(0.35)),
    F("Hollow Mallet", "Mallet", 5, 0.0, [op(1, 0.9, "struck", 1), op(2, 0.4, "click"), op(1, 0.5, "struck", 1), None, None, None], **RV(0.35)),
    F("Dulcimer Hit", "Mallet", 5, 0.2, [op(1, 0.9, "bell"), op(1, 0.6, "click"), op(2, 0.6, "bell"), op(3, 0.4, "click"), op(1, 0.4, "bell"), op(5, 0.3, "click")], **CH(1, 0.3), **RV(0.35)),
    F("Bright Bars", "Mallet", 5, 0.0, [op(1, 0.85, "bell"), op(10, 0.4, "click"), op(4, 0.6, "bell"), op(11, 0.35, "click"), None, None], **RV(0.4)),
    F("Toy Mallet", "Mallet", 5, 0.0, [op(2, 0.8, "pluck"), op(3, 0.4, "click"), op(3, 0.5, "pluck"), None, None, None], **RV(0.3)),
    # ---------------------------------------------------------------- Metal
    F("Clangorous Hit", "Metal", 1, 0.8, [op(1, 0.9, "struck"), op(1.41, 0.8, "struck"), op(1, 0.6, "struck"), op(2.39, 0.7, "fall"), op(3.73, 0.6, "fall"), op(5.3, 0.5, "click")], **RV(0.35)),
    F("Metal Plate", "Metal", 7, 0.7, [op(1, 0.9, "longbell"), op(2.39, 0.6, "bell"), op(1.41, 0.7, "longbell"), op(4.71, 0.6, "bell"), op(3.73, 0.5, "bell"), op(5.3, 0.5, "bell")], **RV(0.45)),
    F("Anvil", "Metal", 1, 0.9, [op(1, 0.9, "struck"), op(3.73, 0.85, "click"), op(1.41, 0.6, "struck"), op(4.71, 0.7, "click"), None, None], **RV(0.3)),
    F("Sci-Fi Drone", "Metal", 16, 0.9, [op(0.5, 0.85, "slowpad"), op(1.41, 0.6, "slowpad"), op(2.39, 0.5, "slowpad"), op(3.73, 0.5, "slowpad"), op(1, 0.5, "slowpad"), op(5.3, 0.4, "slowpad")],
      lfoShape=5, lfoRate=6.0, lfoAmp=0.25, **RV(0.55)),
    F("Ring Mod Bell", "Metal", 5, 0.5, [op(1, 0.9, "bell"), op(7.1, 0.75, "bell"), op(1.41, 0.6, "bell"), op(9.7, 0.6, "bell"), None, None], **RV(0.45)),
    F("Noise Hit", "Metal", 1, 1.0, [op(1, 0.9, "click"), op(13.1, 0.9, "click"), op(1, 0.6, "click"), op(9.7, 0.9, "click"), op(7.1, 0.8, "click"), None], **RV(0.3)),
    F("Robot Voice", "Metal", 1, 0.6, [op(1, 0.9, "sus", 2), op(3, 0.6, "sus"), op(1, 0.4, "sus"), None, None, None], lfoShape=5, lfoRate=8.0, lfoAmp=0.3, **RV(0.2)),
    F("Factory Clank", "Metal", 7, 0.85, [op(1, 0.9, "fall"), op(2.39, 0.8, "click"), op(1.41, 0.7, "fall"), op(4.71, 0.7, "click"), op(5.3, 0.6, "click"), op(7.1, 0.6, "click")], **RV(0.35)),
    F("Cymbal Wash", "Metal", 1, 1.0, [op(1, 0.85, "longbell"), op(9.7, 0.9, "longbell"), op(1, 0.6, "longbell"), op(13.1, 0.9, "longbell"), op(7.1, 0.85, "bell"), None], **RV(0.45)),
    F("Broken Radio", "Metal", 16, 0.9, [op(1, 0.85, "sus"), op(2.39, 0.6, "sus", 3), op(4.71, 0.4, "sus"), op(1.41, 0.5, "sus"), op(3.73, 0.4, "sus"), op(1, 0.4, "sus")],
      lfoShape=5, lfoRate=12.0, lfoAmp=0.35, **RV(0.2)),
    F("Steel Strings", "Metal", 5, 0.4, [op(1, 0.9, "pluck"), op(1.41, 0.6, "pluck"), op(2, 0.6, "pluck"), op(2.76, 0.5, "pluck"), None, None], **CH(1, 0.4), **RV(0.35)),
    F("Bowl Resonance", "Metal", 5, 0.2, [op(1, 0.85, "longbell"), op(2.76, 0.4, "longbell"), op(1.57, 0.6, "longbell"), op(4.2, 0.35, "longbell"), None, None], **TREM(3.0, 0.2), **RV(0.6)),
    F("Distorted Metal", "Metal", 1, 1.0, [op(1, 0.95, "fall", 2), op(3.73, 0.85, "fall"), op(1, 0.6, "fall", 2), op(5.3, 0.8, "fall"), None, None], **RV(0.25)),
    F("Sheet Metal", "Metal", 7, 0.95, [op(1, 0.9, "bell"), op(5.3, 0.8, "bell"), op(1.41, 0.7, "bell"), op(7.1, 0.75, "bell"), op(9.7, 0.7, "bell"), op(13.1, 0.6, "bell")], **RV(0.4)),
    F("Alien Chirp", "Metal", 5, 0.8, [op(1, 0.85, "pluck"), op(9.7, 0.85, "fall"), op(2.39, 0.6, "pluck"), op(13.1, 0.7, "fall"), None, None], **RV(0.4)),
    F("Gong Swell", "Metal", 1, 0.5, [op(0.5, 0.85, "swell"), op(1.41, 0.6, "swell"), op(1, 0.6, "slowpad"), op(2.39, 0.5, "slowpad"), op(3.73, 0.4, "slowpad"), None], **RV(0.6)),
]
