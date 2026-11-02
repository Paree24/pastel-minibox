"""Drums factory bank: 32 hand-designed kits.

Engines per part: kick 0 Classic, 1 Punch, 2 Box, 3 Ring, 4 Layer; perc 0 Classic, 1 Bell, 2 Punch,
3 Box, 4 Layer; every other part 0 Classic, 1 Punch, 2 Box, 3 Layer.
Parts: kick, snare, clap, tom, c hat, o hat, cymbal, perc. Per-part fields: Pitch, Bend, BendTime,
Decay, Tone, Click, Drive, DriveType, Level, Pan, SendDelay, SendReverb, ResSend, Lay*.
"""

PARTS = ["kick", "snare", "clap", "tom", "chat", "ohat", "cym", "perc"]


def kit(name, engines, kitvals=None, **parts):
    """engines: 8 engine indices; parts: kick=dict(...), snare=dict(...)...; kitvals: kit-level values."""
    v = {}
    for i, e in enumerate(engines):
        v[f"p{i + 1}Eng"] = e
    for pname, fields in parts.items():
        idx = PARTS.index(pname) + 1
        for k, val in fields.items():
            v[f"p{idx}{k}"] = val
    v.update(kitvals or {})
    # a kit that sends some parts to the resonator sends only those (the others' default is 50 %)
    if any(k.endswith("ResSend") for k in v):
        for i in range(1, 9): v.setdefault(f"p{i}ResSend", 0.0)
    # the kit's own room: big spaces for the dub / synthwave / stadium styles, tight rooms for the dry ones
    size, tone, pre = ROOMS.get(name, (0.4, 0.55, 0.012))
    v.setdefault("revSize", size); v.setdefault("revTone", tone); v.setdefault("revPre", pre)
    return (name, v)


ROOMS = {
    "Echo Space": (0.85, 0.4, 0.04), "Big Reverb Snare": (0.8, 0.6, 0.03), "Driven Big Reverb": (0.75, 0.6, 0.025),
    "Long Boom": (0.65, 0.45, 0.02), "Dark Driven": (0.6, 0.3, 0.02), "Rumble Kick": (0.55, 0.35, 0.015),
    "Lo-Fi Box": (0.2, 0.25, 0.004), "Short Click": (0.25, 0.7, 0.002), "Tight Compressed": (0.25, 0.5, 0.005),
    "Beat Box": (0.2, 0.5, 0.004), "Crisp Room": (0.6, 0.65, 0.015), "Soft Dry": (0.2, 0.5, 0.003), "Standard Tight": (0.25, 0.55, 0.004), "Dry Machine": (0.3, 0.55, 0.008), "Hand Percussion": (0.35, 0.65, 0.01),
}


K = lambda **f: f
RV = lambda x: {"SendReverb": x}

DRUMS = [
    kit("Classic Analog", [0, 0, 0, 0, 0, 0, 0, 0], kick=K(Decay=0.9, Bend=0.45), snare=K(Click=0.6), clap=RV(0.25), cym=K(Decay=2.6)),
    kit("Warehouse", [1, 1, 1, 1, 1, 1, 1, 2], dict(kitComp=0.35), kick=K(Decay=0.45, Bend=0.45, Click=0.5), snare=K(Click=0.7, Tone=0.45, **RV(0.2)),
        clap=RV(0.3), cym=K(Decay=2.0)),
    kit("Beat Box", [2, 2, 2, 2, 2, 2, 2, 3], kick=K(Decay=0.4, Click=0.6), snare=K(Click=0.6, **RV(0.15)), clap=RV(0.2)),
    kit("Deep Ring", [3, 0, 1, 0, 0, 0, 0, 0], kick=K(Decay=0.8, Bend=0.5, BendTime=0.06, Tone=0.4, Drive=0.3), snare=RV(0.2), clap=RV(0.3)),
    kit("Layer Kit", [4, 3, 3, 3, 3, 3, 3, 4],
        kick=K(LayA=0, LayB=3, LayMix=0.2, Bend=0.5, BendTime=0.03), snare=K(LayA=0, LayB=4, LayMix=0.6, Pitch=200, Decay=0.2, **RV(0.2)),
        chat=K(LayA=2, LayB=4, LayMix=0.5), ohat=K(LayA=2, LayB=4, LayMix=0.5)),
    kit("Clicky Snap", [0, 0, 0, 0, 0, 0, 0, 1], kick=K(Decay=0.3, Click=0.9), snare=K(Click=0.9, Tone=0.7, **RV(0.2)), chat=K(Decay=0.06),
        ohat=K(Decay=0.35), perc=K(Pitch=560, Decay=0.25), clap=RV(0.25)),
    kit("Rumble Kick", [1, 1, 1, 1, 1, 1, 1, 2], dict(resMix=0.35, resTune=55, resDecay=0.75, kitComp=0.45),
        kick=K(Decay=0.7, Drive=0.35, DriveType=1, ResSend=0.6), snare=K(Click=0.5, **RV(0.25)), cym=K(Decay=2.6)),
    kit("Lo-Fi Box", [2, 2, 2, 2, 2, 2, 2, 3], dict(kitDrive=0.4, kitComp=0.5), kick=K(Drive=0.4, DriveType=2), snare=K(Drive=0.3, **RV(0.2))),
    kit("Long Boom", [0, 0, 0, 0, 0, 0, 0, 0], kick=K(Decay=2.0, Bend=0.6, Tone=0.3), snare=K(Click=0.4, Pitch=170, **RV(0.3)),
        chat=K(Decay=0.05), clap=RV(0.35)),
    kit("Long Sub Kick", [3, 1, 0, 0, 0, 0, 0, 0], kick=K(Decay=1.6, Bend=0.7, BendTime=0.05, Tone=0.5), snare=K(Pitch=250, Click=0.85, Tone=0.3, **RV(0.25)),
        chat=K(Decay=0.04, Tone=0.8), ohat=K(Decay=0.3), perc=K(Pitch=900, Tone=0.8, Decay=0.08), clap=RV(0.3)),
    kit("Compressed Punch", [1, 1, 0, 1, 1, 1, 1, 1], dict(kitComp=0.5), kick=K(Decay=0.5, Bend=0.4), clap=K(**RV(0.35), Decay=0.4),
        ohat=K(Decay=0.6), perc=K(Pitch=560, **RV(0.15)), snare=RV(0.2)),
    kit("Short Click", [0, 3, 0, 0, 2, 2, 0, 0], kick=K(Decay=0.25, Click=0.9, Tone=0.3), snare=K(LayA=0, LayB=2, LayMix=0.7, Decay=0.08, Pitch=400),
        chat=K(Decay=0.04), ohat=K(Decay=0.2), perc=K(Pitch=2400, Decay=0.05, Tone=0.0), clap=RV(0.2)),
    kit("Echo Space", [2, 0, 2, 0, 0, 0, 0, 0], dict(resMix=0.25, resType=0, resTune=110),
        snare=K(SendDelay=0.5, **RV(0.4), Pitch=160), clap=K(SendDelay=0.4, **RV(0.4)), tom=K(Decay=0.8, SendDelay=0.35), perc=K(SendDelay=0.4)),
    kit("Distorted Resonant", [1, 1, 1, 1, 1, 1, 1, 2], dict(kitDrive=0.7, resMix=0.4, resType=1, resTune=82, kitComp=0.6),
        kick=K(Drive=0.6, DriveType=2, Decay=0.6), snare=K(Drive=0.5, DriveType=2, ResSend=0.7), clap=K(ResSend=0.5), perc=K(ResSend=0.8)),
    kit("Tight Compressed", [1, 1, 1, 1, 0, 0, 0, 0], dict(kitComp=0.55, kitDrive=0.2), kick=K(Decay=0.35, Click=0.7), snare=K(Tone=0.6, Click=0.8, **RV(0.2)),
        chat=K(Decay=0.07, Tone=0.5)),
    kit("Chip Box", [4, 3, 3, 3, 3, 3, 3, 4], dict(kitDrive=0.3),
        kick=K(LayA=1, LayB=0, LayMix=0.4, Bend=0.6, BendTime=0.03, Drive=0.5, DriveType=2), snare=K(LayA=1, LayB=2, LayMix=0.5, Drive=0.4, DriveType=2),
        chat=K(LayA=1, LayB=2, LayMix=0.7, Pitch=2000), perc=K(LayA=1, LayB=1, LayMix=0.5, LayRatio=1.5, Pitch=880)),
    kit("Hand Percussion", [0, 3, 0, 0, 2, 2, 0, 0], kick=K(Pitch=70, Decay=0.3, Click=0.3), snare=K(LayA=4, LayB=4, LayMix=0.5, Pitch=900, Decay=0.12),
        tom=K(Pitch=200, Decay=0.35), perc=K(Pitch=340, Decay=0.3, Tone=0.2), chat=K(Decay=0.05), clap=RV(0.25)),
    kit("Airy Resonant", [3, 3, 3, 0, 0, 0, 0, 0], dict(resMix=0.3, resTune=147, resDecay=0.8),
        kick=K(Decay=1.2, Tone=0.2, Bend=0.3, **RV(0.25)), snare=K(LayA=0, LayB=3, LayMix=0.5, Decay=0.4, **RV(0.6)),
        clap=K(**RV(0.6)), cym=K(Decay=3.5, **RV(0.4)), perc=K(**RV(0.5), ResSend=0.6)),
    kit("Punch and Box", [1, 2, 1, 2, 2, 2, 1, 0], kick=K(Decay=0.4), snare=K(Pitch=210, **RV(0.25)), clap=RV(0.3), perc=K(Pitch=1700, Tone=0.6, Decay=0.08)),
    kit("Short Snappy", [1, 1, 1, 1, 0, 0, 0, 0], dict(kitComp=0.5), kick=K(Bend=0.6, Decay=0.3, Click=0.8), snare=K(Pitch=230, Click=0.9, Tone=0.4, **RV(0.25)),
        chat=K(Decay=0.05, Tone=0.7)),
    kit("Big Reverb Snare", [0, 1, 0, 1, 1, 1, 1, 0], dict(kitComp=0.4), kick=K(Decay=0.6), snare=K(**RV(0.55), Tone=0.6), tom=K(Bend=0.5, **RV(0.3)),
        clap=RV(0.5), cym=K(Decay=2.5)),
    kit("Ringing Sub", [3, 0, 1, 0, 0, 0, 0, 0], kick=K(Decay=1.6, Bend=0.6, BendTime=0.04), snare=K(Click=0.9, Pitch=210), chat=K(Decay=0.03, Tone=0.9),
        clap=RV(0.2)),
    kit("Tuned Toms and Bell", [2, 0, 0, 0, 2, 2, 0, 1], tom=K(Pitch=150, Decay=0.45), perc=K(Pitch=640, Decay=0.3), kick=K(Decay=0.35),
        snare=K(Pitch=230, Click=0.4), chat=K(Decay=0.06)),
    kit("Bent Layers", [4, 3, 3, 3, 3, 3, 3, 4], dict(kitDrive=0.3),
        kick=K(LayMod=2, Bend=0.4, BendTime=0.01, LayA=0, LayB=1, LayMix=0.3), snare=K(LayMod=2, Bend=0.5, BendTime=0.004, LayA=2, LayB=4, LayMix=0.5, LayEnv=2),
        clap=K(LayEnv=2, LayA=4, LayB=2, SendDelay=0.4), chat=K(LayMod=2, Bend=0.6, BendTime=0.002, LayA=1, LayB=2), perc=K(LayMod=1, Bend=0.6, LayA=0, LayB=1)),
    kit("Driven Big Reverb", [1, 1, 0, 1, 1, 1, 1, 2], dict(kitComp=0.6, kitDrive=0.25), kick=K(Drive=0.5, DriveType=1, Decay=0.7, Bend=0.5),
        clap=K(**RV(0.5)), snare=K(**RV(0.45)), cym=K(Decay=2.8, **RV(0.3))),
    kit("Soft Brush", [0, 3, 3, 0, 0, 0, 0, 0], kick=K(Click=0.1, Tone=0.2, Decay=0.5), snare=K(LayA=3, LayB=4, LayMix=0.6, Decay=0.25, Tone=0.3, **RV(0.35)),
        clap=K(LayA=3, LayB=3, Decay=0.2, **RV(0.3)), chat=K(Tone=0.2), ohat=K(Tone=0.2), cym=K(Decay=2.5, Tone=0.3)),
    kit("Metal Clang", [1, 2, 1, 2, 2, 2, 0, 1], dict(resMix=0.4, resType=1, resTune=220, resDecay=0.7),
        perc=K(Pitch=700, ResSend=0.6), chat=K(Tone=0.9), snare=K(ResSend=0.5), clap=K(ResSend=0.4)),
    kit("Dark Driven", [1, 2, 1, 1, 1, 1, 1, 2], dict(resMix=0.3, resTune=49, kitComp=0.5),
        kick=K(Decay=0.8, Tone=0.3, Drive=0.4, DriveType=1, ResSend=0.5), snare=K(**RV(0.3)), chat=K(Tone=0.3), ohat=K(Tone=0.3)),
    kit("Dry Machine", [0, 0, 0, 0, 2, 2, 0, 1], dict(kitComp=0.4), kick=K(Decay=0.4), snare=K(Click=0.7), perc=K(Pitch=500), clap=RV(0.2)),
    kit("High Snare Classic", [0, 1, 0, 0, 0, 0, 0, 0], kick=K(Decay=0.7), snare=K(Pitch=240, Click=0.6, **RV(0.2)), perc=K(Pitch=1200, Tone=0.7, Decay=0.1),
        clap=RV(0.25)),
    kit("Noise Kit", [4, 3, 3, 3, 3, 3, 3, 4], dict(kitDrive=0.35),
        kick=K(LayA=3, LayB=0, LayMix=0.6, Pitch=60, Decay=0.4), snare=K(LayA=2, LayB=4, LayMix=0.5, Drive=0.6), clap=K(LayA=4, LayB=4, LayEnv=2),
        chat=K(LayA=2, LayB=2), ohat=K(LayA=2, LayB=4), cym=K(LayA=2, LayB=4, Decay=2.0), perc=K(LayA=4, LayB=3, Pitch=1500)),
    kit("Crushed Drive", [1, 1, 1, 1, 1, 1, 1, 2], dict(kitDrive=0.6, kitComp=0.6), kick=K(Drive=0.8, DriveType=2, Decay=0.5), snare=K(Drive=0.6),
        clap=K(Drive=0.5, **RV(0.3))),
    # ---------------------------------------------------------------- standard kits (2026-11-01): plain, all-round sounds
    kit("Standard Punchy", [1, 1, 0, 1, 1, 1, 1, 1], kick=K(Decay=0.5, Click=0.5, Bend=0.4), snare=K(Click=0.6, Tone=0.5, **RV(0.15)),
        clap=RV(0.2), chat=K(Decay=0.06), ohat=K(Decay=0.45), cym=K(Decay=2.2)),
    kit("Standard Tight", [1, 1, 1, 1, 1, 1, 1, 2], dict(kitComp=0.55), kick=K(Decay=0.2, Click=0.75), snare=K(Decay=0.1, Click=0.75, Tone=0.6),
        chat=K(Decay=0.03, Tone=0.7), ohat=K(Decay=0.2), cym=K(Decay=1.2), clap=K(Decay=0.15)),
    kit("Standard Deep", [0, 0, 0, 0, 1, 1, 1, 0], kick=K(Decay=1.5, Bend=0.5, Tone=0.15), snare=K(Pitch=150, Decay=0.35, Tone=0.3, **RV(0.35)),
        tom=K(Decay=0.7, Tone=0.3), clap=RV(0.35), chat=K(Tone=0.35), ohat=K(Tone=0.35)),
    kit("Standard Bright", [0, 1, 1, 0, 1, 1, 1, 1], snare=K(Tone=0.65, Click=0.8, **RV(0.15)), chat=K(Tone=0.8), ohat=K(Tone=0.8), cym=K(Tone=0.7),
        clap=RV(0.2)),
    kit("Soft Dry", [0, 0, 0, 0, 2, 2, 0, 0], kick=K(Click=0.0, Tone=0.15, Decay=0.4), snare=K(Tone=0.2, Click=0.1, Decay=0.25), clap=K(Tone=0.3),
        chat=K(Tone=0.2, Decay=0.05), ohat=K(Tone=0.2, Decay=0.3), cym=K(Tone=0.2)),
    kit("Snappy Pop", [1, 1, 1, 1, 0, 0, 0, 1], kick=K(Decay=0.4, Click=0.7), snare=K(Pitch=220, Click=0.85, Tone=0.55, **RV(0.25)), clap=RV(0.3)),
    kit("Round Warm", [0, 0, 0, 0, 0, 0, 0, 0], dict(kitDrive=0.15), kick=K(Decay=0.7, Tone=0.3), snare=K(Tone=0.4, **RV(0.15)), chat=K(Tone=0.4),
        ohat=K(Tone=0.4)),
    kit("Crisp Room", [1, 1, 1, 1, 0, 0, 0, 1], kick=K(**RV(0.15)), snare=K(**RV(0.45), Click=0.7), clap=K(**RV(0.5)), tom=K(**RV(0.35)),
        chat=K(**RV(0.2)), cym=K(Decay=2.8, **RV(0.3))),
]
