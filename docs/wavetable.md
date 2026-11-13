# Wavetable

A synth built for bass music: deep subs, wide Reese basses, growls, neuro and foghorn sounds, wobbles. It also covers
leads, pads, plucks, keys, bells and effects. The design mockups are `mockups/png/wavetable_desktop.png` (desktop) and
`minibox-android/mockups/png/wavetable_phone.png` (phone).

## Sound path

Each note runs two wavetable oscillators (A and B), a sub, and noise. The note's filter follows, with its own drive, and
the amp envelope. After the notes come the instrument's distortion, the mono low end, and the level.

- **Oscillators A and B.** A table (64 built in) and a position through it (16 frames, crossfaded), octave, semitone,
  fine, start phase (the oscillators restart on each new note, for a consistent attack) and level.
- **Osc B's modulation from A.** *Sync* restarts B with every cycle of A, and Mod raises B's pitch up to three octaves.
  *FM* lets A modulate B's phase, with Mod as the index. *Ring* multiplies B by A, with Mod as the mix.
- **Sub.** A sine or triangle at the note's pitch (0 oct, what a low bass note wants) or one or two octaves under it.
  It is always in the middle and never part of the unison.
- **Noise.** White or pink, with Tone (dark to bright) and Decay. Decay's top setting, the default, holds the noise for
  as long as the note sounds. A short Decay makes a burst for an attack.
- **Filter.** LP 24, LP 12, HP, BP and Notch, with Cutoff, Reso (up to self-oscillation), Drive (before the filter),
  Env (the filter envelope, either way) and Key (follows the note).
- **Envelopes.** Amp and Filter, both ADSR; the card switches between them.
- **LFO 1 and 2.** Sine, triangle, saw, square, S&H, smooth; free (Hz) or synced to the tempo (a free LFO restarts with
  the first key). Depth, Fade (fade-in), and Dest: a quick route to any destination. Both LFOs also feed the matrix.
- **Distortion.** Soft, Hard or Tube drive, then Fold (a sine wavefolder), Crush (bits and sample rate), Tone and Mix.
  It runs 4× oversampled.
- **Squash.** Three-band upward and downward compression after the distortion (crossovers at 150 Hz and 2.5 kHz).
  The mid and top bands are pulled toward the whole sound's level, so the result doesn't depend on the patch's
  volume, and the peaks this creates are rounded off. The more Squash, the closer the top comes: 10 dB under at light
  settings, 4 dB under at full, the flat, bright top of a heavily squashed growl. This gives a squashed bass its density, edge and top end. The
  low band is left alone, so the sub stays steady.
- **Chorus.** After Squash, for width. Mono Bass then makes the low end mono again.
- **Unison.** Up to 16 stacks per note with Detune and Width, and Phase (a random start for each stack). Mono Bass
  (Hz) keeps everything below it mono: the side signal is high-passed at 24 dB an octave, so a wide stack keeps a
  solid, mono-compatible low end. Voice modes: Mono, Legato (overlapping notes glide and don't retrigger) and Poly (up to 6
  notes, fewer with big stacks: 32 stacks in all, halved in Eco). Glide in seconds.

## Modulation matrix

There are 8 slots, each a source, a destination and a bipolar amount, all automatable.

- **Sources:** LFO 1, LFO 2, Amp Env, Filter Env, Velocity, Note, Mod Wheel, Pressure, Random (one value per note).
- **Destinations:** Pitch, Position A, Position B, Level A, Level B, Osc B Mod, Pitch B, Sub, Noise, Cutoff, Reso,
  Filter Drive, Distortion, Fold, Crush, Detune, Width, Pan, Amp, LFO 1 Rate, LFO 2 Rate, Squash, Chorus and Volume.

The distortion, Squash, Chorus, Volume and LFO-rate destinations act on the whole instrument and follow the most recent
note. Volume is the level after the distortion and Squash (±24 dB): an envelope there swells a sound the drive would
otherwise hold flat.

On the desktop, click a source or destination to choose it. Drag along the bar to set the amount; a double-click
zeroes it, and a right-click clears the slot. On the phone, tap to choose, drag the bar, and long-press a row to
clear it.

## Macros

**Wobble** (LFO 1 depth and rate), **Movement** (both table positions), **Dirt** (drive, fold, crush, Squash), **Bite**
(cutoff, reso, Osc B's mod) and **Width** (detune, width, Chorus) sit in the band above the cards. Sub, Noise and Glide are
macros 6 to 8. As on every instrument, the macros are automatable and MIDI-learnable, and the plugin exposes them to
the host.

## Randomize

**Randomize** (on the phone: the die) sets new values within musical ranges: the sub stays solid and the level stays
where it was. The scope can be everything, the oscillators, filter and envelopes, modulation, or distortion and
unison. On the phone, long-press the die to choose the scope.

## Step locks

Every parameter can be locked per step from the Seq page's Lock lane. Under the editor's step strip, the Wavetable
shows one lock lane: Cutoff, Position, FM or Dirt (the pill under the bar numbers). Drag along a step's bar to set its
lock; right-click clears it.

## Tables

All 64 are built in: generated in code, band-limited at nine resolutions (512 harmonics down to 2, read with cubic
interpolation), and normalized frame by frame.

- **Basic:** Basic Shapes (sine → triangle → saw → square)
- **Analog:** PWM, Saw Sweep, Square Sweep, Tri to Square, Organ, Sine Harmonics, Odd Harmonics, Even Harmonics, Hollow
- **Sub:** Sub Sine, Warm Sine, Sine Fold, Round Pulse, Sine Sync, Deep Tri
- **Reese:** Reese Saw, Reese Square, Reese Phase, Dispersed Saw, Phase Saw, Rasp, Buzz, Reese Hollow
- **Growl:** Growl, Vowel A-O, Vowel E-I, Yoy, Talk, Foghorn, Horn, Throat. The vowels use real vowel formants, placed
  for a 55 Hz note, on a bright buzz: Growl opens "oo" → "oh" → "ah", Yoy goes "ee" → "oh", Talk runs A, E, I, O, U,
  and Foghorn is a hollow horn whose "aw" opens with the position
- **Neuro:** Neuro Grind, Neuro Bite, Snarl, Saw Fold, Square Fold, Crushed Saw, Shred, Gnarl
- **Metal:** Metal Bell, Clang, Ring Mod, Tine, Gong, Chime, Alloy, Tin
- **FM:** FM 1:1, FM 1:2, FM 1:3, FM 2:1, Feedback FM, Hard Sync, Sync Square, PD Reso
- **Digital:** Bitcrush, Digital Steps, Harmonic Sweep, Comb, Spectral, Noise Hash, Pulse Train, Squelch

## Presets

There are 128: 64 bass presets in 8 families (Sub, Reese, Growl, Neuro, Wobble, FM Bass, Pluck Bass, Digital Bass)
and 64 others (Lead, Pad, Pluck, Keys, Bell, Stab, Texture, FX). The bank lives in
`Source/app/presets/bank_wavetable.py`: `banks.py write wavetable`, then `MB_BALANCE=wavetable MiniboxTests
tool_balance_presets`, then `MB_NUDGE=wavetable … tool_nudge_presets` (it separates presets that render too alike),
then balance again. The nudge never turns on what a preset leaves off (noise, chorus, a filter envelope). As in every bank, presets have no tuning offsets, no glide and no pitch modulation. The one
exception is unison detune, which spreads the stack around the played pitch.

The bank is designed against reference bass sounds. A growl is a vowel moved by an envelope, once per note ("rawr",
"yoy", "wow"), with a slower vowel movement fading in for held notes; its filter stays open and it is driven hard and
squashed, so its level and top stay put while the vowel changes (an LFO on the filter is a wobble, not a growl). Foghorn ramps up instead: each note swells from dark into the horn
and louder over about a second and a half and holds there (a folded sine FM-ing another through a low-pass at
200 Hz, one slow envelope opening the filter, the FM and the fold and raising the Volume), driven hard and squashed. The
Reese presets each move a different way (detune beating alone, a slow filter, a notch, phasing, a filter envelope, the
spread, fold, distortion). Plucks and stabs ring: their Decay (the time to −80 dB) is at least 2.2 s, 1.6 s for the pluck basses. Stepped LFOs (S&H, square) are kept for the
presets named Step. Every bass preset moves: an LFO or an envelope runs on its table,
filter, FM, fold or noise, and those without a second LFO of their own drift slowly through table A. Most are wide
above a mono low end. Their filters barely follow the key, so low notes stay bright. They have some drive and Squash,
a family-specific noise layer, and a sub at the note's pitch as a supporting layer. The same principles, lighter,
apply to the other families.

To measure a pass: `MB_WT_OUT=<dir> MiniboxTests tool_wavetable_render` (bass presets on G1, others on C3, 4 s;
`MB_WT_SET="squash=0.6,…"` tries settings), then `python3 -I Source/app/presets/analyse_sounds.py <dir>` on the
renders and on the reference sounds. Compare the medians: movement (centroid spread), brightness, low and high share,
width and noisiness. On 2026-11-13, against the references, the bass bank measured:

| | Bass presets | References |
|---|---|---|
| Movement (octaves) | 0.54 | 0.60 |
| Brightness | 383 Hz | 620 Hz |
| Share under 120 Hz | 0.78 | 0.70 |
| Width | −13 dB | −9 dB |
| Noisiness | 0.23 | 0.30 |

Before this pass it measured 0.15 octaves of movement, 199 Hz, a share of 0.95, mono, and 0.03.

## Tests

`Test/WavetableTests.cpp` covers:

- the bank: 64 tables, every frame there
- pitch
- band-limiting: at C7, aliasing stays more than 45 dB under the harmonics
- the mono low end under a wide stack
- matrix routing
- every table played through all modes and the distortion
- Squash lifting what is above the sub
- the noise holding with the note

`MB_WT=1 MiniboxTests tool_wavetable_cpu` measures the heaviest patch.
