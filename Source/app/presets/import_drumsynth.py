#!/usr/bin/env python3
"""Drum Sampler kits from Pastel Drum Synth (2026-11-01): one-shots rendered by its KitProbe tool (each part through
the whole synth: voice, part FX, global chain, limiter) become Factory/samples/dkNN_<part>.wav and Drum Sampler
presets numbered after the existing ones.

    pastel-drum-synth/build/KitProbe <renders>            # every factory kit, <renders>/NN_Kit_Name/00_KICK.wav …
    python3 Source/app/presets/import_drumsynth.py <renders> <kit numbers …>
    MB_BALANCE=drumsampler MiniboxTests tool_balance_presets   # then level-balance

Pads (the Drum Sampler's order): kick ← KICK, snare ← SNARE, chat ← CHH, ohat ← OHH, 5th ← MEM PERC, tom ← TOM 2,
cym ← CRASH, perc ← MET PERC. The membrane and metal pads are named by the kit's model (Clap, Conga, Cowbell …),
never as a clap they are not. Samples: mono, 48 kHz (soxr), trimmed to the sound, the kit scaled as one (the parts
keep their balance) to a -1 dBFS peak.
"""
import glob
import json
import os
import subprocess
import sys
import wave
import xml.etree.ElementTree as ET

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
SYNTH = os.path.join(os.path.dirname(ROOT), "pastel-drum-synth")
SAMPLES = os.path.join(ROOT, "Factory", "samples")
PRESETS = os.path.join(ROOT, "Factory", "presets", "drumsampler")
SR = 48000

# Drum Sampler pad ← Drum Synth part (index into KICK, SNARE, CHH, OHH, RIDE, CRASH, TOM 1, TOM 2, LOW TOM, MEM, MET, SFX)
PADS = [("kick", 0), ("snare", 1), ("chat", 2), ("ohat", 3), (None, 9), ("tom", 7), ("cym", 5), (None, 10)]
MEM_MODELS = ["perc", "conga", "bongo", "rim", "wood", "glass", "snap", "clap"]          # MEM PERC: CLASSIC, CONGA …
MET_MODELS = ["metal", "clang", "cowbell", "agogo", "tambourine", "glass"]               # MET PERC: CLASSIC, CLANG …


def kit_xml(num):
    f = glob.glob(os.path.join(SYNTH, "Factory", f"{num:03d} *.xml"))[0]
    root = ET.parse(f).getroot()
    parts = {int(p.get("id")): p for p in root.findall("PART")}
    return root.get("kitName"), parts


def load(path):
    """The render, mono and at 48 kHz (ffmpeg's soxr resampler), as float32."""
    out = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-af", f"aresample=resampler=soxr:osr={SR}", "-ac", "1",
                          "-f", "f32le", "-"], check=True, capture_output=True).stdout
    return np.frombuffer(out, dtype=np.float32).copy()


def trim(x):
    """From just before the hit to where the tail falls under -60 dB of the peak, with a 5 ms fade."""
    peak = float(np.max(np.abs(x))) if len(x) else 0.0
    if peak < 1e-6:
        return x[:SR // 10] * 0
    above = np.nonzero(np.abs(x) > peak * 1e-3)[0]
    a = max(0, int(above[0]) - 32)
    b = min(len(x), int(above[-1]) + int(0.01 * SR))
    y = x[a:b].copy()
    f = min(len(y), int(0.005 * SR))
    y[len(y) - f:] *= np.linspace(1, 0, f)
    return y


def write16(path, x):
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((np.clip(x, -1, 1) * 32767).astype("<i2").tobytes())


def main(renders, nums):
    with open(os.path.join(SAMPLES, "manifest.json")) as f:
        man = json.load(f)
    have = {k["name"] for k in man["kits"]}
    existing = sorted(glob.glob(os.path.join(PRESETS, "*.mbx-drumsampler")))
    n = len(existing)
    for num in nums:
        name, parts = kit_xml(num)
        if name in have:
            print("skip (already in):", name)
            continue
        d = glob.glob(os.path.join(renders, f"{num}_*"))[0]   # KitProbe numbers them without padding
        files = sorted(glob.glob(os.path.join(d, "[0-9][0-9]_*.wav")))
        raw = [trim(load(files[p])) for _, p in PADS]
        scale = 0.89 / max(1e-6, max(float(np.max(np.abs(r))) for r in raw))   # the kit as one: the parts keep their balance
        stems, values = [], {"revSize": 0.4, "revTone": 0.55, "revPre": 0.012}
        for i, ((role, p), x) in enumerate(zip(PADS, raw)):
            model = int(float(parts[p].get("model", "0")))
            if role is None:
                names = MEM_MODELS if p == 9 else MET_MODELS
                role = names[model] if 0 <= model < len(names) else names[0]
            stem = f"dk{num:02d}_{role}"
            write16(os.path.join(SAMPLES, stem + ".wav"), x * scale)
            stems.append(stem)
            values[f"pd{i + 1}Decay"] = round(len(x) / SR, 4)
            values[f"pd{i + 1}Pan"] = round((float(parts[p].get("pan", "0.5")) - 0.5) * 2.0, 3)
        values["kitLevel"] = 0.8
        n += 1
        doc = dict(type="drumsampler", version=1, name=name, family="Kits", values=values, macros="",
                   samples=[f"factory:{s}" for s in stems])
        with open(os.path.join(PRESETS, f"{n:03d} {name} [Kits].mbx-drumsampler"), "w") as f:
            json.dump(doc, f, indent=1, sort_keys=True)
        man["kits"].append(dict(name=name, files=stems))
        print(f"{n:03d} {name}: " + ", ".join(stems))
    with open(os.path.join(SAMPLES, "manifest.json"), "w") as f:
        json.dump(man, f, indent=1)


if __name__ == "__main__":
    main(sys.argv[1], [int(a) for a in sys.argv[2:]])
