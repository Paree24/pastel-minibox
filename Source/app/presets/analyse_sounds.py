"""Sound analysis for preset design (docs/wavetable.md): how much the timbre moves (spread, flux, cent_sd: the
spectral centroid's spread in octaves), its rate, brightness (cent), the share below 120 Hz (low) and above 2 kHz
(high), stereo width overall and below 150 Hz (dB, side against mid), noisiness 2–8 kHz (flat), the level's movement
and the crest factor. Run it on reference sounds and on preset renders (MB_WT_OUT=<dir> MiniboxTests
tool_wavetable_render) and compare the medians.

    python3 -I Source/app/presets/analyse_sounds.py <files or folders …> [--csv out.csv]"""
import os
import struct
import sys

import numpy as np


def read_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a wav")
    pos, fmt, raw = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            tag, ch, sr = struct.unpack("<HHI", body[:8]); bits = struct.unpack("<H", body[14:16])[0]
            if tag == 0xFFFE: tag = struct.unpack("<H", body[24:26])[0]
            fmt = (tag, ch, sr, bits)
        elif cid == b"data":
            raw = body
        pos += 8 + size + (size & 1)
    tag, ch, sr, bits = fmt
    if tag == 3:
        x = np.frombuffer(raw, dtype="<f4" if bits == 32 else "<f8").astype(np.float64)
    elif bits == 16:
        x = np.frombuffer(raw[: len(raw) // 2 * 2], dtype="<i2") / 32768.0
    elif bits == 24:
        b = np.frombuffer(raw[: len(raw) // 3 * 3], dtype=np.uint8).reshape(-1, 3)
        v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        v = np.where(v & 0x800000, v - 0x1000000, v)
        x = v / 8388608.0
    elif bits == 32:
        x = np.frombuffer(raw[: len(raw) // 4 * 4], dtype="<i4") / 2147483648.0
    else:
        x = (np.frombuffer(raw, dtype=np.uint8) - 128) / 128.0
    x = x[: len(x) // ch * ch].reshape(-1, ch)
    if ch == 1: x = np.repeat(x, 2, axis=1)
    return x[:, :2], sr


def analyse(x, sr, maxsec=4.0):
    x = x[: int(maxsec * sr)]
    mono = x.mean(axis=1)
    side = 0.5 * (x[:, 0] - x[:, 1])
    pk = np.max(np.abs(x)) + 1e-12
    # the sounding part: from the first frame above -30 dB of peak to the last
    hop, n = int(0.02 * sr), 4096
    win = np.hanning(n)
    frames = []
    env = []
    for i in range(0, len(mono) - n, hop):
        seg = mono[i:i + n]
        env.append(np.sqrt(np.mean(seg ** 2)))
        frames.append(np.abs(np.fft.rfft(seg * win)))
    if not frames: return None
    S = np.array(frames); env = np.array(env)
    loud = env > env.max() * 0.1
    S = S[loud]; envl = env[loud]
    f = np.fft.rfftfreq(n, 1 / sr)
    band = (f > 80) & (f < 8000)
    # movement: the timbre's change (the level taken out), frame to frame and against the average timbre
    L = np.log(S[:, band] + 1e-9)
    L = L - L.mean(axis=1, keepdims=True)
    spread = np.mean(np.sqrt(np.mean((L - L.mean(axis=0)) ** 2, axis=1)))   # dB-ish distance from the mean spectrum
    flux = np.mean(np.sqrt(np.mean(np.diff(L, axis=0) ** 2, axis=1))) if len(L) > 1 else 0
    P = S ** 2
    cent = (P[:, band] * f[band]).sum(1) / (P[:, band].sum(1) + 1e-12)
    lc = np.log2(cent + 1)
    cent_sd = float(np.std(lc))   # octaves
    # the movement's rate: the centroid's strongest periodicity (Hz), 0.5–20 Hz
    rate = 0.0
    if len(lc) > 16:
        c = lc - lc.mean()
        ac = np.correlate(c, c, "full")[len(c) - 1:]
        ac /= ac[0] + 1e-12
        lags = np.arange(len(ac)) * 0.02
        ok = (lags > 0.1) & (lags < 2.0)
        if ok.any():
            k = np.argmax(np.where(ok, ac, -1))
            if ac[k] > 0.3: rate = 1.0 / lags[k]
    tot = P.sum(1) + 1e-12
    low = float(np.mean(P[:, f < 120].sum(1) / tot))
    high = float(np.mean(P[:, f > 2000].sum(1) / tot))
    # stereo: side energy against mid, overall and below 150 Hz
    def bandrms(sig, lo, hi):
        F = np.fft.rfft(sig); ff = np.fft.rfftfreq(len(sig), 1 / sr)
        F[(ff < lo) | (ff > hi)] = 0
        return np.sqrt(np.mean(np.fft.irfft(F, len(sig)) ** 2)) + 1e-12
    midsig = x.mean(axis=1)
    width = 20 * np.log10(np.sqrt(np.mean(side ** 2)) / (np.sqrt(np.mean(midsig ** 2)) + 1e-12) + 1e-12)
    lowwidth = 20 * np.log10(bandrms(side, 20, 150) / bandrms(midsig, 20, 150))
    # noisiness: spectral flatness 2–8 kHz
    hb = (f > 2000) & (f < 8000)
    flat = float(np.mean(np.exp(np.mean(np.log(P[:, hb] + 1e-20), 1)) / (np.mean(P[:, hb], 1) + 1e-20)))
    # the level's own movement (a wobble moves the level too)
    ldb = 20 * np.log10(envl + 1e-9)
    lvl_sd = float(np.std(ldb))
    crest = 20 * np.log10(pk / (np.sqrt(np.mean(mono ** 2)) + 1e-12))
    return dict(dur=len(x) / sr, spread=float(spread), flux=float(flux), cent=float(np.median(cent)), cent_sd=cent_sd,
                rate=rate, low=low, high=high, width=float(width), lowwidth=float(lowwidth), flat=flat, lvl_sd=lvl_sd, crest=float(crest))


def files(args):
    for a in args:
        if os.path.isdir(a):
            for root, _, fs in os.walk(a):
                for fn in sorted(fs):
                    if fn.lower().endswith(".wav"): yield os.path.join(root, fn)
        else:
            yield a


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    csv = None
    if "--csv" in sys.argv: csv = sys.argv[sys.argv.index("--csv") + 1]; args = [a for a in args if a != csv]
    keys = ["dur", "spread", "flux", "cent_sd", "rate", "cent", "low", "high", "width", "lowwidth", "flat", "lvl_sd", "crest"]
    rows = []
    for p in files(args):
        try:
            x, sr = read_wav(p)
            r = analyse(x, sr)
        except Exception as e:
            print("skip", p, e); continue
        if r is None: continue
        rows.append((p, r))
        print(f"{os.path.basename(p)[:44]:44s} " + " ".join(f"{k}={r[k]:.3g}" for k in keys))
    if rows:
        print("MEDIAN " + " ".join(f"{k}={np.median([r[k] for _, r in rows]):.3g}" for k in keys))
    if csv:
        with open(csv, "w") as f:
            f.write("file," + ",".join(keys) + "\n")
            for p, r in rows: f.write(p.replace(",", ";") + "," + ",".join(f"{r[k]:.5g}" for k in keys) + "\n")


if __name__ == "__main__":
    main()
