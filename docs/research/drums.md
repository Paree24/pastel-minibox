# Drum research notes (modelling homework)

Reference machines named in the brief: two analog drum machines of 1980–83 (the "808" and "909" circuits),
a 6-part 2-layer DSP percussion synth with a waveguide resonator, an analog kick synth built around a
resonating filter, and an analog/PCM beat box. Names stay out of the product (docs, UI). These notes record
facts gathered from public papers and measurements; Minibox code is written from them, not copied.

## Sources
- K. J. Werner, J. S. Abel, J. O. Smith, "A physically-informed, circuit-bendable, digital model of the
  Roland TR-808 bass drum circuit", DAFx-14.
- K. J. Werner, J. S. Abel, J. O. Smith, "The TR-808 cymbal: a physically-informed, circuit-bendable,
  digital model", ICMC/SMC 2014.
- G. Reid, Synth Secrets: "Practical bass drum synthesis", "Practical snare drum synthesis" (Sound On Sound).
- Measurements below: public-domain recordings of a real 1980 analog drum machine (Hollow Sun
  "Classic-808" kit, /usr/lib/lv2/fabla808.lv2), analysed with docs/research/analyze.py.

## 1980 machine — circuit facts (papers)
### Bass drum
- 1 ms trigger pulse (4–14 V by accent) → pulse shaper: passive low shelf with a diode clipping the
  negative swing at one diode drop (−0.71 V). Rising edge = full trigger, falling edge ≈ VR162/(R162+R163)+0.71.
- Bridged-T network in an op-amp's feedback = band-pass resonator, centre ≈ 49.5 Hz (tuning chart 56 Hz).
  Inputs: shaped pulse, feedback buffer output, retrigger pulse.
- Decay = feedback buffer (high shelf, VR6 decay pot) feeding the bridged-T back: less attenuation → longer note.
- Attack: an envelope (Q41/Q42) grounds Q43's collector for ~6 ms, R_effective drops from R165+R166 to R166:
  centre frequency and Q jump up by more than an octave for less than one period → the "punch".
- Pitch "sigh": leakage through R161 into Q43 when V_comm drops below a diode drop; fitted
  iC = (log(1+e^{α(Vcomm−V0)})/α)^m, α=14.3150, V0=0.5560, m=1.4765e−5 → slowly falling centre frequency.
- Output: passive low-pass (tone pot) → level → high-pass. Retriggering before silence interacts with
  the ringing state (no machine-gun effect). Component tolerances ±20 % C / ±5 % R → unit-to-unit variation.
### Cymbal / hats / cowbell
- Six Schmitt-trigger square oscillators: 205.3, 304.4, 369.6, 522.7, 800 (trimmed), 540 (trimmed) Hz.
  Cowbell uses the 540 + 800 Hz pair.
- Two active band-passes on the summed cluster: ≈3440 Hz and ≈7100 Hz.
- Cymbal: three bands, each with its own envelope (EG #1–3) into a nonlinear "swing-type" VCA, then
  Sallen-Key high-passes (2nd/2nd/3rd order, the 3rd resonant near 10.5 kHz), tone pot balances band 3.
- Attack smoother: one-pole, VBE 0.7258, τ 1.0244e−4 s. EG release RC set by the decay pot.

## 1980 machine — measured (real recordings)
| Voice | Pitch / partials | Decay (−20 / −40 / −60 dB) | Spectrum |
|---|---|---|---|
| Kick long | 60.6 Hz at 2 ms → 57 (10 ms) → 52 (80 ms) → 49.8 (300 ms) → 49.5; H2 −19 dB, H3 −29 dB | 234 ms / 1.44 s / 2.06 s | >1 kHz below −60 dB: almost no click |
| Kick short | 61 → 49.5 Hz | 23 ms / 143 ms | same |
| Snare | body 172 Hz steady, 340 Hz −22 dB | 55–70 ms / 115–130 ms / ~220 ms | noise HP'd: 2–8 kHz, centroid ≈6 kHz; snappy sets noise −10…−42 dB vs body |
| Low tom | 90.2 → 88.5 Hz (slight fall) | 109 ms / 285 ms | nearly pure, faint noise |
| Hi tom | 190 → 187.5 Hz | 83 ms / 185 ms | nearly pure |
| Low conga | 200.9 → 200.2 Hz | 158 ms / 335 ms | pure sine-like |
| Rim shot | 476 Hz + 1713 Hz (−7.5) + 931 Hz | 9 ms / 12 ms / 17 ms | 250 Hz–4 kHz, very short |
| Clave | 2422 Hz pure | 22 ms / 33 ms | single partial |
| Cowbell | 558 + 824 Hz (squares) + harmonics | 43 ms / 295 ms / 480 ms (two-stage) | band-passed 0.5–2 kHz |
| Clap | noise 0.8–2 kHz (centroid 4.5 kHz early) | 37 ms / 215 ms | bursts at 0, 11, 23 ms then dense tail to ~55 ms |
| Closed hat | metal cluster | 34 ms / 72 ms | 4–20 kHz, centroid ≈11.6 kHz |
| Open hat | metal cluster | 448 ms / 555 ms | centroid ≈9.3 kHz |
| Cymbal | metal cluster | 146 ms / 1.35 s / 2.8 s | centroid 10.2 → 8.8 kHz over 250 ms |
| Maracas | HP noise | 9.5 ms / 13 ms | 4–16 kHz |

## 1983 machine — circuit facts (third-party service-level mod notes, R. Whittle "TR-909 sound mods")
### Bass drum
- Triangle VCO rounded to a near-sine by back-to-back diodes conducting at ±0.5–0.6 V; driving the diodes
  harder clips towards a square (odd harmonics).
- Tune pot does NOT set the base pitch: it sets the decay time of an envelope that pushes the VCO above its
  rest frequency at note start (start high, sweep down to normal). Measured on recordings by others:
  ≈230 Hz at 5 ms, ≈160 Hz at 10 ms, settling ≈50 Hz; body holds near full level 50–80 ms before decaying.
- Attack: a short pulse (click) plus low-passed noise through their own VCA/EG, mixed with the body.
### Snare
- VCO1 and VCO2 (VCO2 tuned higher) share one pitch CV = Tune + a short rising-and-falling pulse at note
  start (a pitch "bump" every note, independent of accent). Each VCO: triangle rounded by back-to-back
  diodes, own crude single-transistor VCA, own exponential decay whose start level follows accent.
- Noise path A: filtered noise, own short envelope (ENV5), quiet. Noise path B: noise through a different
  (capacitor high-pass) filter, VCA with envelope ENV4 whose start is set by accent: holds flat ≈24 ms
  then decays (the "steam valve" blast). Snappy = level of A+B; Tone = ENV4 decay.
- Digital noise: shift-register pseudo-random (two 18-bit registers), repeating but inaudibly so.
### Hand clap (both machines)
- Band-pass filtered noise through a VCA driven by a few sawtooth pulses (sharp rise, sloping fall),
  ≈11 ms apart, plus a longer, softer, slowly decaying "pseudo-reverb" pulse of the same noise.
  The 1980 machine uses analog transistor noise.

## Analog kick synth (resonating-filter kick)
- No oscillator: a 2-pole filter (the classic 12 dB semi-modular filter, early revision) at self-oscillation
  is the voice. Controls: Resonator pitch / bend / time (pitch sweep), Pulse colour / level (the exciting
  click), Amp attack / decay, Drive (multi-stage analog distortion, one knob), Tone (12 dB low-pass),
  Accent; bend can be reversed (low → high).

## 6-part DSP percussion synth (owner's manual)
- Per part: 2 identical layers mixed, + part BIT reduction, wave FOLD, DRIVE, PAN, pre-mix GAIN, pitch quantise.
- Per layer: source = sine, saw, HP-filtered noise, LP-filtered noise, BP-filtered noise; LEVEL; PITCH;
  pitch modulator = rise-fall, oscillate, random, with MOD AMOUNT and MOD RATE; amp EG = linear AR,
  exponential AR, multi-peak AR, with ATTACK and RELEASE.
- Shared waveguide resonator, models TUBE and STRING: SEND per part, DECAY, BODY (timbre), TUNE (low
  values drop below audio → delay-like).
