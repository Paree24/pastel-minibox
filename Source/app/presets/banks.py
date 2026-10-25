#!/usr/bin/env python3
"""Builds the factory content from the hand-designed banks.

    python3 Source/app/presets/banks.py write [dev] # Mono / Poly / Acid / FM / Drums presets (all, or one bank)
    python3 Source/app/presets/banks.py jobs       # sample jobs: Sampler sources and Drum Sampler kits,
                                                   #   recorded from the engines by
                                                   #   MiniboxTests tool_render_samples
    python3 Source/app/presets/banks.py samplers   # manifest + Sampler and Drum Sampler presets
    MiniboxTests tool_balance_presets              # then level-balance every bank

Rules: no detune, tune, octave offsets, glide, pitch LFO or pitch EG in any preset.
"""
import glob
import json
import os
import shutil
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bank_mono import MONO      # noqa: E402
from bank_poly import POLY      # noqa: E402
from bank_acid import ACID      # noqa: E402
from bank_drums import DRUMS    # noqa: E402
from bank_fm import FM          # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
PRESETS = os.path.join(ROOT, "Factory", "presets")
SAMPLES = os.path.join(ROOT, "Factory", "samples")
JOBS = os.path.join(ROOT, "build", "sample_jobs.json")
SR = 48000

FORBIDDEN = {"range", "tune", "fine", "detune", "vaFine", "vaOctave", "transpose", "glide", "glideTime", "glideMode", "lfoPitch",
             "drift", "stackDetune", "pegInt", "slideMode"} | {f"op{n}Detune" for n in range(1, 7)} | {f"pd{n}Pitch" for n in range(1, 9)}


BASS_FAMILIES = {"Bass", "Sub", "Deep", "Wobble"}
REVERB_KEYS = ("reverb", "fxReverb", "revSize", "revTone", "revPre")


def is_bass(dev, fam, name):
    """Basses never get reverb (user rule): bass / sub / deep / wobble families, every acid line except leads,
    and anything named as a bass."""
    return fam in BASS_FAMILIES or (dev == "acid" and fam != "Lead") or "Bass" in name.split()


def write_dev(dev, items, family_of=None):
    d = os.path.join(PRESETS, dev)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    names = set()
    for i, it in enumerate(items):
        name, fam, vals, samples = it
        assert name not in names, (dev, name)
        names.add(name)
        bad = FORBIDDEN & set(vals)
        assert not bad, (dev, name, bad)
        vals = dict(vals)
        if is_bass(dev, fam, name):
            vals = {k: v for k, v in vals.items() if k not in REVERB_KEYS}
        vals.setdefault("kitLevel" if dev in ("drums", "drumsampler") else "level", 0.8 if dev in ("drums", "drumsampler") else 0.7)
        doc = dict(type=dev, version=1, name=name, family=fam, values=vals, macros="", samples=samples)
        with open(os.path.join(d, f"{i + 1:03d} {name} [{fam}].mbx-{dev}"), "w") as f:
            json.dump(doc, f, indent=1, sort_keys=True)
    print(f"{dev}: {len(items)} presets")


def fm_presets():
    out = []
    for f in sorted(glob.glob(os.path.join(PRESETS, "fm", "*.mbx-fm"))):
        out.append(json.load(open(f)))
    return out


def write(only=None):
    """only: a device id to rewrite just that bank (the others keep their nudged / balanced files)."""
    banks = dict(mono=[(n, f, v, []) for n, f, v in MONO], poly=[(n, f, v, []) for n, f, v in POLY],
                 acid=[(n, f, v, []) for n, f, v in ACID], drums=[(n, "Kits", v, []) for n, v in DRUMS])
    for dev, items in banks.items():
        if only in (None, dev): write_dev(dev, items)
    if only in (None, "fm"):
        write_dev("fm", [(n, f, v, []) for n, f, v in FM])


# ------------------------------------------------------------------------------------------ samples
def by_family(items, fam):
    return [it for it in items if it[1] == fam]


def sampler_sources():
    """128 sources: each a recording of one of the new synth patches, in 8 families of 16."""
    fm = [(d["name"], d["family"], d["values"]) for d in fm_presets()]
    pick = []
    def add(family, dev, items, n, note, hold, maxlen, sustained):
        for name, _, vals in items[:n]:
            # decaying sounds are recorded with the key held, so the sample keeps the whole natural ring
            pick.append(dict(family=family, device=dev, name=name, values=vals, note=note,
                             hold=hold if sustained or family == "Texture" else round(0.85 * maxlen, 2), max=maxlen, sustained=sustained))
    add("Keys", "poly", by_family(POLY, "Keys"), 10, 60, 0.8, 4.0, False)
    add("Keys", "fm", by_family(fm, "Electric Piano"), 6, 60, 0.8, 4.0, False)
    add("Pad", "poly", by_family(POLY, "Pad"), 16, 60, 3.0, 6.0, True)
    add("Strings & Brass", "poly", by_family(POLY, "Strings"), 8, 60, 2.5, 5.0, True)
    add("Strings & Brass", "poly", by_family(POLY, "Brass"), 8, 60, 2.0, 4.0, True)
    add("Bass", "mono", by_family(MONO, "Bass"), 6, 36, 1.0, 2.5, False)
    add("Bass", "acid", by_family(ACID, "Deep"), 5, 36, 1.0, 2.5, False)
    add("Bass", "poly", by_family(POLY, "Bass"), 5, 36, 1.0, 2.5, False)
    add("Lead", "mono", by_family(MONO, "Lead"), 10, 60, 2.0, 4.0, True)
    add("Lead", "acid", by_family(ACID, "Lead"), 6, 48, 1.5, 3.5, True)
    add("Pluck", "poly", by_family(POLY, "Pluck"), 8, 60, 0.4, 3.5, False)
    add("Pluck", "mono", by_family(MONO, "Pluck"), 8, 60, 0.4, 3.5, False)
    add("Bell", "fm", by_family(fm, "Bell"), 8, 72, 0.6, 5.0, False)
    add("Bell", "fm", by_family(fm, "Mallet"), 8, 72, 0.4, 3.5, False)
    add("Texture", "mono", by_family(MONO, "FX"), 10, 60, 1.2, 4.0, False)   # effects one-shots: peak early
    add("Texture", "fm", by_family(fm, "Pad"), 6, 60, 3.0, 6.0, True)
    assert len(pick) == 128, len(pick)
    for i, p in enumerate(pick):
        p["file"] = f"src{i + 1:03d}_" + "".join(c if c.isalnum() else "_" for c in p["name"].lower())
    return pick


DRUM_PARTS = ["kick", "snare", "clap", "tom", "chat", "ohat", "cym", "perc"]
PAD_ORDER = ["kick", "snare", "chat", "ohat", "clap", "tom", "cym", "perc"]   # drum sampler pads: 36 38 42 46 39 45 49 37


def kit_files(i):
    return {p: f"kit{i + 1:02d}_{p}" for p in DRUM_PARTS}


def jobs():
    for f in glob.glob(os.path.join(SAMPLES, "src*.wav")) + glob.glob(os.path.join(SAMPLES, "kit*.wav")):
        os.remove(f)
    out = []
    for s in sampler_sources():
        # tonal sources are recorded dry (the sampler adds its own room); textures keep their space as part of the sound
        dry = {k: (0.0 if k in ("reverb", "fxReverb") and s["family"] != "Texture" else v) for k, v in s["values"].items()}
        out.append(dict(file=s["file"], device=s["device"], values=dry, note=s["note"], hold=s["hold"], max=s["max"]))
    for i, (name, vals) in enumerate(DRUMS):
        for pi, part in enumerate(DRUM_PARTS):
            out.append(dict(file=kit_files(i)[part], device="drums", values=dict(vals, kitReverb=0.0), part=pi, hold=0.1,
                            max=4.0 if part == "cym" else 2.5))
    # slicer loops, recorded from two of the kits at 120 BPM
    def loop(kit_idx, pattern, file):
        step = 60.0 / 120.0 / 4.0
        hits = [[k * step, part, vel] for k, part, vel in pattern]
        out.append(dict(file=file, device="drums", values=DRUMS[kit_idx][1], hits=hits, hold=0.0, max=step * 32))
    brk = []
    for bar in range(2):
        o = bar * 16
        brk += [(o + 0, 0, 0.95), (o + 4, 1, 0.9), (o + 7, 0, 0.7), (o + 10, 0, 0.85), (o + 12, 1, 0.95), (o + 14, 1, 0.5)]
        brk += [(o + k, 4, 0.6 if k % 4 else 0.8) for k in range(0, 16, 2)]
    loop(14, brk, "break_loop_120")
    ele = []
    for bar in range(2):
        o = bar * 16
        ele += [(o + 0, 0, 0.95), (o + 3, 0, 0.7), (o + 6, 0, 0.8), (o + 10, 0, 0.9), (o + 4, 2, 0.9), (o + 12, 2, 0.95)]
        ele += [(o + k, 4, 0.7) for k in range(0, 16, 2)] + [(o + 14, 5, 0.6), (o + 9, 7, 0.6)]
    loop(5, ele, "electro_loop_120")
    os.makedirs(os.path.dirname(JOBS), exist_ok=True)
    json.dump(out, open(JOBS, "w"))
    print(f"{len(out)} sample jobs → {JOBS}")


def wav_seconds(file):
    with wave.open(os.path.join(SAMPLES, file + ".wav")) as w:
        return w.getnframes() / w.getframerate()


# sampler shaping per source (cycled within each family): (prefix, values)
SHAPES = [
    ("", {}),
    ("", dict(reverb=0.25)),
    ("Muted", dict(filterType=0, cutoff=1400, reso=0.15, reverb=0.15)),
    ("Dusty", dict(character=1, bits=12, rate=20, reverb=0.2)),
    ("", dict(reverb=0.35)),
    ("Pulsing", dict(lfoShape=2, lfoRate=4.0, lfoCut=0.5, cutoff=3000)),
    ("Crushed", dict(character=1, bits=8, rate=12)),
    ("", dict(reverb=0.3, drive=0.3)),
]


def samplers():
    srcs = sampler_sources()
    man = dict(sampler=[], kits=[])
    items = []
    count = {}
    names = set()
    for s in srcs:
        fam = s["family"]
        k = count.get(fam, 0)
        count[fam] = k + 1
        length = wav_seconds(s["file"])
        man["sampler"].append(dict(file=s["file"], name=s["name"], family=fam, root=s["note"], sustained=s["sustained"]))
        prefix, shape = SHAPES[k % len(SHAPES)]
        if not s["sustained"] and prefix == "Pulsing":   # an LFO-chopped filter only suits held sounds
            prefix, shape = "Dusty", dict(character=1, bits=12, rate=20)
        if fam == "Texture" and prefix in ("Muted", "Crushed"):   # risers and sweeps keep their top end, in a big room
            prefix, shape = "Spacious", dict(reverb=0.45)
        if s["sustained"]:
            v = dict(playMode=2, loopStart=min(0.9, 0.45 * s["hold"] / length), loopLen=min(0.95, 0.4 * s["hold"] / length),
                     attack=0.005, decay=1.0, sustain=1.0, release=0.6)
        else:
            # one-shot textures (risers, sweeps) play out whole; the rest follow the key
            # the sample carries its own decay: the envelope just holds it while the key is down
            v = dict(playMode=0 if fam == "Texture" else 1, attack=0.001, decay=min(8.0, length), sustain=1.0, release=0.4)
        v.update(z1Root=s["note"], z1Lo=0, z1Hi=127)
        v.update(shape)
        if "reverb" in v:   # the room grows with the send, as on the synth banks
            v.update(revSize=min(0.9, 0.25 + 1.1 * v["reverb"]), revPre=round(0.008 + 0.05 * v["reverb"], 4))
        name = (prefix + " " if prefix else "") + s["name"]
        if name in names: name = s["name"] + " Sample"
        names.add(name)
        items.append((name, fam, v, ["factory:" + s["file"]]))
    write_dev("sampler", items)
    # drum sampler: the 32 kits, each with its own sampler treatment
    treat = [
        ("", {}), ("Tape", dict(HiCut=9000, Drive=0.3)), ("Crushed", dict(Bits=8, HiCut=7000)), ("Dusty", dict(Bits=12, HiCut=6000)),
        ("Driven", dict(Drive=0.5)), ("Gated", None), ("Lo-Fi", dict(Bits=6, HiCut=4000)), ("Bright", dict(HiCut=20000)),
    ]
    ditems = []
    for i, (name, vals) in enumerate(DRUMS):
        files = kit_files(i)
        man["kits"].append(dict(name=name, files=[files[p] for p in PAD_ORDER]))
        prefix, t = treat[i % len(treat)]
        v = {}
        for pad, part in enumerate(PAD_ORDER):
            n = pad + 1
            v[f"pd{n}Decay"] = min(4.0, wav_seconds(files[part]) + 0.05) if t is not None else (0.12 if part in ("kick", "snare", "clap", "tom") else 0.08)
            v[f"pd{n}Pan"] = {"kick": 0, "snare": 0.05, "chat": 0.25, "ohat": 0.25, "clap": -0.15, "tom": -0.3, "cym": -0.2, "perc": 0.35}[part]
            for key, val in (t or {}).items():
                v[f"pd{n}{key}"] = val
            if part in ("snare", "clap"): v[f"pd{n}SendReverb"] = 0.2
        for key in ("revSize", "revTone", "revPre"):   # the kit's room carries over from the synth kit
            if key in vals: v[key] = vals[key]
        ditems.append(((prefix + " " if prefix else "") + name, "Kits", v, ["factory:" + files[p] for p in PAD_ORDER]))
    write_dev("drumsampler", ditems)
    with open(os.path.join(SAMPLES, "manifest.json"), "w") as f:
        json.dump(man, f, indent=1)
    print("manifest written")


if __name__ == "__main__":
    if sys.argv[1] == "write": write(sys.argv[2] if len(sys.argv) > 2 else None)
    else: {"jobs": jobs, "samplers": samplers}[sys.argv[1]]()
