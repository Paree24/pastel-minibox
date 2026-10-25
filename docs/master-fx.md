# Master and performance effects (groovebox only)

The groovebox's master bus runs, in order: the delay and reverb returns, the **performance effects** (while a pad is
held), the **master effects chain**, then the bus's own Glue, Volume and Limit. The VST3 plugin has none of this.

The effects are those of the user's Pastel EFX Grid, borrowed at their request (`Source/efx`, see its README).

## Master effects chain (mockup `19_master_fx`)

Rack page ▸ MASTER ▸ **Effects** (or Ctrl+Shift+F) opens the drawer above the master strip.

| Feature (as in EFX Grid / DAW insert chains) | Minibox |
|---|---|
| Slots in series | 8, left to right; the chain stays packed (a new effect goes to the first free slot; removing one shifts the rest left) |
| Effects | every EFX Grid effect except analysis-only ones and the few edited by drawing (8-Band EQ, Trance Gate, …): dynamics, EQ, filters, modulation, stereo, pitch, drive, reverbs and delays, spectral and time effects, by category |
| Per slot | bypass (the tile's dot), mode, every control (the ones that do nothing in the mode are hidden), mix |
| Edit | Replace, Remove, move ← / → (Alt+← / Alt+→ on a focused tile, Delete removes it) |
| Saved | with the project (`masterFx` in the project file) |
| Glitch-free | a slot's settings and its running effect change together; a click-free bypass fade |

The strip's **Effects** button shows a dot per slot in use, in its category's colour (grey when bypassed).

## Performance effects (the PERFORM pads)

Eight pads on the MASTER strip, on only while held:

| Pad | Key | KeyLab pad | Effect (EFX Grid) | Held setting |
|---|---|---|---|---|
| Stutter | 1 | 1 | Stutter | 1/16 repeats of the slice caught at the press, held |
| Glitch | 2 | 2 | Glitch | Wild, full density |
| Shuffle | 3 | 3 | Buffer Shuffler | its Shuffle macro |
| Slicer | 4 | 4 | Slicer | short Euclidean gate |
| Crush | 5 | 5 | Bitcrusher | 4 bits, 2.5 kHz |
| Filter | 6 | 6 | Multimode Filter | low-pass at 200 Hz, resonant |
| Drive | 7 | 7 | Soft Clip Drive | +30 dB drive, −6 dB out |
| Reverse | 8 | 8 | Reverse | each slice backwards, fully wet |

- Hold with the mouse, the keys 1–8 (rebindable in Settings ▸ Keyboard), the KeyLab's pads, or any button mapped to
  `Performance effects` in MIDI learn (Momentary). Several can be held together.
- A press starts the effect fresh (Stutter repeats what plays at that moment); a release fades it out in a few ms.
- Effects follow the tempo; their grid starts with Play.
- Tune a pad: right-click it (or the drawer's **Pads** view): its controls and mix; **Reset** restores the pad's settings.
- KeyLab Essential 49 mk2: the pads send notes 36–43 on channel 10, the keys play on channel 1, so the keys
  stay notes even on the same pitches. A map saved before the pads existed gets them when the app starts.
