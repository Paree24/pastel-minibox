# Pedal research notes (Acid drive)

Acid lines are classically pushed through guitar pedals. Three circuit families are modelled
(`Source/dsp/Pedals.h`); names stay generic in the product (Overdrive, Distortion, Fuzz).

Sources: community circuit analyses and SPICE studies (Electrosmash analyses of each pedal, DIYStompboxes
"Spice analysis of Tube Screamer", PedalPCB "RAT circuit theory", Big Muff Pi analyses), plus the
published schematics' component values.

## Overdrive (1979 green overdrive)
- Non-inverting op-amp clipping amp. Gain = 1 + (51 kΩ + Drive 0–500 kΩ) / 4.7 kΩ, but the 4.7 kΩ leg runs
  through 47 nF: the gain only applies above ≈ 720 Hz (1/(2π·4.7k·47n)); bass passes at unity.
- 51 pF across the feedback: a drive-dependent HF corner, 1/(2π·(51k+Rd)·51p) ≈ 61 kHz … 5.7 kHz.
- Antiparallel silicon diodes in the op-amp feedback: soft clipping of the boosted part, added to the
  clean signal (the bass and some clean always come through).
- Tone stage: passive 1 kΩ / 0.22 µF low-pass ≈ 723 Hz, then an active treble control. Net: the mid hump.

## Distortion (1978 op-amp distortion)
- LM308 non-inverting stage, Distortion pot 0–100 kΩ in the feedback. Two legs to ground: 560 Ω + 4.7 µF
  (corner ≈ 60 Hz, gain up to 1 + 100k/560 ≈ 180) and 47 Ω + 2.2 µF (corner ≈ 1.5 kHz, up to ≈ 2100).
- The LM308's low gain-bandwidth and slew rate audibly roll off the top at high gain; 100 pF across the
  feedback caps it near 16 kHz.
- 1 kΩ then 1N914 diodes to ground: hard clipping at ≈ ±0.6 V.
- Filter: 1.5 kΩ + 100 kΩ pot with 3.3 nF, a low-pass from ≈ 32 kHz (open) down to ≈ 482 Hz.

## Fuzz (1969 four-stage fuzz)
- Four common-emitter transistor stages: an input booster (Sustain sets its gain), two clipping stages
  with antiparallel diodes in their collector-base feedback (≈ 30 dB each, soft-limited at a diode drop),
  then a passive tone stack and an output booster. Coupling capacitors between stages roll off the bass
  (≈ 80–120 Hz corners) so it does not turn to mud.
- Tone stack: a low-pass (≈ 408 Hz) and a high-pass (≈ 1.8 kHz) blended by the Tone pot — a mid scoop
  near 1 kHz at the centre.
