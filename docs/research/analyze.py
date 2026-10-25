import sys, wave, numpy as np
def load(p):
    w = wave.open(p); n = w.getnframes(); sw = w.getsampwidth(); ch = w.getnchannels(); sr = w.getframerate()
    raw = w.readframes(n)
    if sw == 2: x = np.frombuffer(raw, np.int16) / 32768.0
    elif sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3); x = ((b[:,0].astype(np.int32) | (b[:,1].astype(np.int32)<<8) | (b[:,2].astype(np.int32)<<16)) << 8 >> 8) / 8388608.0
    elif sw == 4: x = np.frombuffer(raw, np.int32) / 2**31
    else: x = (np.frombuffer(raw, np.uint8) - 128) / 128.0
    x = x.reshape(-1, ch).mean(1)
    return x, sr
def env_times(x, sr):
    a = np.abs(x); k = int(0.002 * sr)
    e = np.sqrt(np.convolve(a**2, np.ones(k)/k, 'same')); pk = e.max(); ip = e.argmax()
    out = {"peak_ms": 1000*ip/sr}
    for db in (6, 20, 40, 60):
        idx = np.where(e[ip:] < pk * 10**(-db/20))[0]
        out[f"-{db}dB_ms"] = round(1000*(idx[0])/sr, 1) if len(idx) else None
    return out
def pitch_track(x, sr, lo=30, hi=2000):
    res = []
    for t in (0.002, 0.005, 0.01, 0.02, 0.04, 0.08, 0.15, 0.3, 0.6):
        s = int(t * sr); L = int(0.03 * sr) if t < 0.05 else int(0.06*sr)
        seg = x[s:s+L]
        if len(seg) < L: break
        seg = seg * np.hanning(len(seg))
        N = 1 << 17; S = np.abs(np.fft.rfft(seg, N)); f = np.fft.rfftfreq(N, 1/sr)
        m = (f > lo) & (f < hi); i = np.argmax(S[m]); res.append((int(t*1000), round(float(f[m][i]), 1)))
    return res
def partials(x, sr, t0=0.0, dur=0.08, n=8, lo=30, hi=16000):
    s = int(t0*sr); seg = x[s:s+int(dur*sr)]; seg = seg*np.hanning(len(seg))
    N = 1 << 17; S = 20*np.log10(np.abs(np.fft.rfft(seg, N))+1e-12); f = np.fft.rfftfreq(N, 1/sr)
    pk = []
    for i in range(2, len(S)-2):
        if lo < f[i] < hi and S[i] > S[i-1] and S[i] >= S[i+1] and S[i] > S[i-2] and S[i] > S[i+2]: pk.append((S[i], f[i]))
    pk.sort(reverse=True); top = S.max(); out = []
    for v, fr in pk:
        if all(abs(fr - o[0]) > max(8, fr*0.03) for o in out): out.append((round(fr,1), round(v-top,1)))
        if len(out) >= n: break
    return out
def bands(x, sr, t0=0.0, dur=0.05):
    s = int(t0*sr); seg = x[s:s+int(dur*sr)]; seg = seg*np.hanning(len(seg))
    S = np.abs(np.fft.rfft(seg))**2; f = np.fft.rfftfreq(len(seg), 1/sr)
    edges = [20, 60, 120, 250, 500, 1000, 2000, 4000, 8000, 12000, 20000]
    tot = S.sum()+1e-20
    return [(f"{edges[i]}-{edges[i+1]}", round(10*np.log10(S[(f>=edges[i])&(f<edges[i+1])].sum()/tot+1e-12),1)) for i in range(len(edges)-1)]
def centroid_track(x, sr):
    out = []
    for t in (0.0, 0.01, 0.03, 0.06, 0.12, 0.25):
        s = int(t*sr); seg = x[s:s+1024]
        if len(seg) < 1024: break
        S = np.abs(np.fft.rfft(seg*np.hanning(1024))); f = np.fft.rfftfreq(1024, 1/sr)
        out.append((int(t*1000), int((S*f).sum()/(S.sum()+1e-12))))
    return out
if __name__ == "__main__":
    for p in sys.argv[1:]:
        x, sr = load(p)
        i0 = np.argmax(np.abs(x) > 0.02 * np.abs(x).max()); x = x[i0:]
        print("=====", p.split("/")[-1], f"sr={sr} len={len(x)/sr:.3f}s peak={np.abs(x).max():.2f}")
        print(" env:", env_times(x, sr))
        print(" pitch:", pitch_track(x, sr))
        print(" partials 0-60ms:", partials(x, sr, 0, 0.06))
        print(" partials 60-200ms:", partials(x, sr, 0.06, 0.14))
        print(" bands 0-50ms:", bands(x, sr))
        print(" centroid:", centroid_track(x, sr))
