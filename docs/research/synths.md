# Synth research notes (modelling homework)

References from the brief (names stay out of the product): the Poly's DCO engine follows a 1982 6-voice
DCO polysynth (and its 2022 compact reissue); the VA engine follows a 2013 compact 3-voice analog
paraphonic synth; the Mono follows a 1982 single-VCO monosynth (and its 2020s compact remake); the Acid
follows the 1981 bass-line synth plus two compact successors. Facts below come from published specs,
service-level descriptions and public emulation write-ups; Minibox code is written from them.

## 1982 DCO polysynth (Poly, DCO engine)
- DCO: pitch from a digital master clock (no pitch drift). Saw = a capacitor ramp reset per cycle: slight
  upward curvature (≈ pos·(1 + 0.15·(1 − pos))) and a short undershoot after each reset. Pulse = a
  comparator on that ramp (PWM manual / LFO / envelope). Sub = a flip-flop square one octave down,
  softened by a passive RC (≈ 4 kHz). Noise ≈ white low-passed near 8 kHz.
- HPF, 4 positions: 0 = +10 dB low shelf below ~150 Hz, 1 = flat, 2 = 1-pole HP ≈ 240 Hz, 3 = ≈ 720 Hz.
- VCF: 4-pole OTA cascade (integrated chip), tanh-like saturation per stage, resonance to self-oscillation,
  resonance tapers at the very top of the range, passband drops with resonance. Per-voice cutoff
  tolerance ≈ ±5 %, envelope timing ≈ ±8 %, VCA gain ≈ ±0.5 dB.
- One ADSR (or gate) drives VCF and VCA; attack is an RC charge aimed above the peak (comparator at 1.0).
- LFO: triangle (rounded peaks), delay ramp up to ~1.5 s.
- Chorus: 256-stage BBD, centre ≈ 3.5 ms. I: triangle 0.513 Hz, ±0.5 ms. II: 0.863 Hz, ±1.1 ms. I+II:
  both. L/R taps 180° apart. ≈ 15 kHz filtering before/after the BBD, BBD charge saturates softly.

## 2013 compact paraphonic synth (Poly, VA engine)
- 3 VCOs (saw / square), voicing: Poly, Unison, Octave, Fifth, Unison Ring, Poly Ring; detune,
  portamento, VCO EG intensity.
- One 12 dB/oct low-pass VCF from a 1974 compact synth (Cutoff, Peak, EG Int), one VCA, one EG
  (attack, decay/release, sustain) shared by all voices (paraphonic), one LFO (saw / tri / square →
  pitch, cutoff), tape-style delay whose time changes bend the pitch.

## 1982 monosynth (Mono)
- One VCO (saw, pulse with PWM from LFO / env / manual), sub (−1 oct square, −2 oct square, −2 oct 25 %
  pulse), noise; mixer.
- VCF 10 Hz – 20 kHz, 4-pole OTA (the same chip family as the polysynth), resonance to self-oscillation,
  env / mod / keyboard follow 0–100 %.
- ADSR: attack 1.5 ms – 4 s, decay 2 ms – 10 s, release 2 ms – 10 s. VCA: env or gate.
- LFO 0.1 – 30 Hz (tri, square, random, noise). Portamento 0–5 s, modes auto / off / on.

## 1981 bass-line synth (Acid)
- Filter (T. Stinchcombe, diode ladder analysis): differential input pair feeding current into the bottom
  node of a 4-capacitor ladder (bottom capacitor half the others), nodes coupled through diode pairs, the
  top node to ground through the top pair. H(s) = 1/(s⁴ + 6.727 s³ + 14.142 s² + 9.514 s + 1),
  ωc = I_f / (2^{3/4} · 2V_T · C). Verified here: that nodal model reproduces the polynomial exactly, DC
  gain 1, resonant peak at 1.189 ωc, self-oscillation at a loop gain of 17.0. The coupling capacitors add
  high-pass poles near 10 Hz, including inside the resonance loop.
- Envelopes and accent (R. Whittle): VEG drives only the VCA (sharp attack, long decay). MEG (sharp attack,
  exponential decay) drives the filter; on accented notes its decay is forced to the minimum. Accent adds
  MEG to the VCA through 47 kΩ / 0.033 µF (≈ 1.55 ms) and to the filter through the sweep circuit: a diode
  and 47 kΩ into the resonance pot's second section with 1 µF to ground (≈ 147 ms charge clockwise,
  ≈ 100 ms drain), so consecutive accents climb.

Verified here (2026-10-23): with the loop high-pass near the circuit's ≈ 10 Hz, a 55 Hz saw at cutoff 450 Hz
loses ≈ 6 dB of fundamental from resonance 0 to 0.95 while the resonant peak rises ≈ 15 dB above it; with the
loop corner at 125 Hz (and make-up gain at the input) the fundamental never fell, which read as a permanent sub.
