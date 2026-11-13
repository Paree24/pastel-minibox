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

| Pad | Key | FX key | Effect (EFX Grid) | Held setting |
|---|---|---|---|---|
| Stutter | 1 | first + 0 |  Stutter | 1/16 repeats of the slice caught at the press, held |
| Glitch | 2 | first + 1 |  Glitch | Wild, full density |
| Shuffle | 3 | first + 2 |  Buffer Shuffler | its Shuffle macro |
| Slicer | 4 | first + 3 |  Slicer | short Euclidean gate |
| Crush | 5 | first + 4 |  Bitcrusher | 4 bits, 2.5 kHz |
| Filter | 6 | first + 5 |  Multimode Filter | low-pass at 200 Hz, resonant |
| Drive | 7 | first + 6 |  Soft Clip Drive | +30 dB drive, −6 dB out |
| Reverse | 8 | first + 7 |  Reverse | each slice backwards, fully wet |

- Hold with the mouse, the keys 1–8 (rebindable in Settings ▸ Keyboard), **FX keys**, or any controller control
  mapped to `Performance effects` in MIDI learn (Momentary). Several can be held together.
- **FX keys** (the PERFORM header's toggle, Ctrl+Shift+K, or a learnable button / pedal: `Performance effects ▸ FX keys`):
  while on, 8 notes from any controller (any input, any channel) hold the effects instead of playing; every other
  note plays. It works with any keyboard, including controllers whose pads send the same notes as the keys (the
  KeyLab Essential's pads send notes 36–43 on the keys' channel). The first note is 36 (C2) by default;
  right-click the toggle ▸ *Set the first note* and press a pad or key to move it. Saved with the controller map.
- Controllers whose pads or buttons send something of their own (another channel, port or a CC) can map each effect
  directly in MIDI learn instead.
- A press starts the effect fresh, quantized to its grid (its Div): a press in the first half of a step takes the
  slice from the line just passed (the timed effects keep the recent past while idle), a press in the second half
  waits for the next line. So Stutter repeats the beat under the press from its start, on the grid, even when the
  press is a little late. A release fades it out in a few ms.
- Effects follow the tempo; their grid starts with Play. While stopped they share the engine's free-running clock
  (the master slots, the pads and the tracks' slots alike), and a slot switched on or added mid-step starts its
  first slice on the line before.
- Tune a pad: right-click it (or the drawer's **Pads** view): its controls and mix; **Reset** restores the pad's settings.
