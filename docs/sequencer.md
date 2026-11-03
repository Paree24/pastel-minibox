# Sequencer and piano roll: expected functionality

Drawn from the Ableton Live MIDI Note Editor (Editing MIDI chapter), the FL Studio Piano Roll (toolbar, Edit and
Tools menus) and groovebox step sequencers (Elektron-style trig conditions, step copy / rotate). Everything that
fits Minibox's step model is listed with how it is reached; the few things left out say why. Every function is
reachable from a visible control (toolbar or menu), not only a shortcut. Status is kept current. Tests: `Test/PianoRollTests.cpp` (grid_snap, multi_bar, view_zoom_fold …), `Test/SequencerTests.cpp` (seq_off_grid_notes, record_live_chord_stays_together …).

## Two kinds of sequence (2026-10-24)
Each track of a pattern holds **one** kind of sequence:
- **Steps** (the step sequencer): notes on steps, chords up to 6 per step, step lanes (velocity, accent / slide,
  probability, ratchet, condition), parameter locks. **Step recording writes here.**
- **Piano roll** (a clip): free notes at any position (960 ticks a quarter), any length, any number per pitch.
  **Live (playback) recording writes here.**
- A step sequence can be **viewed and edited as a piano roll** (constrained to its steps) and **converted** to a
  piano-roll clip (one undo step). A clip converts back to steps (**Convert to steps**; the phone's Steps button):
  each note to its nearest step, its offset kept to 1/48 step, its own length, velocity and mute; notes starting
  together become a chord. Refused, with the reason, when steps can't hold it: more than 6 notes or one pitch twice
  in a step, notes of one step differing in accent / slide / chance, or controller lanes.
- Swing (the project's) warps clip notes and drum rolls on the step grid too: a note on an odd step plays as late
  as a step there, one between in proportion (Project::swungSteps).
- **Arpeggiate** (Chords menu; the phone's Arp button): each chord of the selection becomes its notes in turn with
  the track's Arp settings (rate, mode, octaves, gate) for as long as it lasts (seq/Arpeggiate.h).
- Live recording on a Steps track converts it first (one undo step), then records into the clip.
- Step recording on a Piano-roll track writes into the clip at the step cursor (on the grid).
- Seq page (mockups 02_seq, 02b, 02c): a step track shows **Steps | Piano roll** and **Convert to piano roll**;
  the Steps view is the bar in view as note cells (click on / off, drag up / down for pitch, Shift an octave,
  right-click clears) with lanes for velocity, gate, accent / probability, slide / ratchet, condition and a
  parameter lock. A clip track shows **Piano roll** with a velocity lane (a stalk per note; drag over them; a
  selected stalk moves the selection). The overview rows and the editor strip draw a clip in miniature.
- Status: **done** (tests: seq_piano_roll_clip, pianoroll_clip_mode, record_* , keyboard_navigation).

## Checklist (the user's list, 2026-10-24)
Status: **done**, **partial** (what is missing is said), **planned**. Each item gets a test when it is built.

### P0: core
| Item | Status |
|---|---|
| Create, select, move, resize, delete notes | done |
| Multi-note selection and group editing | done |
| Copy, cut, paste, duplicate | done |
| Undo / redo | done |
| Draw, paint and erase tools | done: toolbar Select / Draw / Paint (a note per grid unit along the drag) / Erase (what the drag passes over); each stroke one undo step |
| Piano keyboard with note audition | done |
| Note pitch, duration and velocity | done |
| Grid resolution and snap on / off | done |
| Straight, dotted and triplet grids | done (*Grid* ▸ Triplet / Dotted) |
| Horizontal / vertical zoom and scrolling | done |
| Bar / beat ruler and octave labels | done |
| Play, pause, stop, seek | done: Stop returns the cursor to the pattern's start (twice also cuts ringing notes); a click on the ruler moves the playhead (marker in the ruler) |
| Loop playback | done: the pattern loops; Shift-drag in the ruler sets a loop range, the toolbar's *Loop* turns it on / off (saved with the pattern) |
| Tempo and time signature | done: the `4/4 ▾` panel offers the common signatures in one click (4/4, 3/4, 5/4, 6/8, 7/8, 12/8), then beats and unit for any other |
| Basic MIDI playback | done |
| Note preview during editing | done (auditions are never recorded) |
| Playback cursor and auto-scroll | done |
| Reliable note-off and stuck-note prevention | done: tested for stop, seek, pattern switch, deleting a sounding note, notes longer than the pattern (seq_no_stuck_notes) |

### P1: usable
| Item | Status |
|---|---|
| Velocity editing lane | done |
| MIDI file import / export | done: `⋯ > Import MIDI file into the selected track…` (a clip, or drum lanes; pick a track or all merged; the pattern grows to fit) and `⋯ > Export pattern as a MIDI file…` (a track per instrument, tempo, time signature) |
| Multiple tracks and patterns | done (8 tracks, patterns A–H; the rack shows four strips and scrolls) |
| Pattern clips and basic arrangement | planned (left for later: the arrangement question was skipped) |
| Quantize | done |
| Transpose by semitones and octaves | done |
| Humanize timing and velocity | done |
| Swing and groove | swing done: the Seq bar strip's **Swing** control (50% straight to 75%; drag, Shift fine, right-click for the usual amounts, double-click straight, ↑/↓ when focused; MIDI-learnable). The off-beat steps of the pattern's grid play late; the DAWproject export bakes it in. Groove templates are P2 |
| Note mute and lock | done: mute (0), lock (Ctrl+L, Functions): locked notes are kept from moves, resizes, deletes, erasing and functions |
| Chord selection and editing | done: Functions ▸ Chords: select the chords of the selection, invert up / down, spread (open voicing), close voicing, add the bass an octave below |
| Scale highlighting and scale snapping | done (rows follow scale lock) |
| Piano input via computer keyboard | done |
| External MIDI keyboard input | done |
| MIDI device selection | done (Audio and MIDI settings) |
| MIDI CC editing lanes | done: a clip's lane chooser (under the notes): Velocity, Pitch bend, Mod wheel (CC 1), Expression (CC 11), Pressure, Macros 1–8; drawn on the grid, right-drag erases, chased on play / seek / loop |
| Pitch bend and modulation editing | done: every melodic instrument follows pitch bend (±2 semitones), the mod wheel and channel pressure (vibrato) and expression (level); played and recorded from MIDI |
| Sustain pedal support | done: held notes ring (chord-mode notes too) and record until the pedal comes up |
| Step recording | done (into the steps; on a piano-roll track, into its clip at the cursor) |
| Live MIDI recording and overdubbing | done (into the piano-roll clip; recording only adds: nothing already there is erased) |
| Metronome and count-in | done |
| Pattern duplication and looping | done |
| Save / load projects | done |
| Autosave and crash recovery | done: every minute while there are unsaved changes; after a crash the next start offers to recover (manual check) |
| Selection-based zoom and fit-to-notes | done: toolbar *Fit* ▸ the whole pattern / the notes / the selection (Ctrl+0, Ctrl+Shift+0) |

### P2: advanced
Not needed (the user, 2026-10-24). Built anyway along the way: per-note offsets, step probability, ghost notes from
other tracks, drum grid and step views, macro automation lanes, channel pressure.

### Added (2026-10-24)
| Item | Status |
|---|---|
| Live recording grows the pattern | done: in Extend (the default; Overdub off) the pattern never loops back while recording: at its end it gets another bar (the other tracks repeat into it) and the take goes on. A take runs from Play (or record on) to Stop (or record off); when it ends the pattern is the bars its notes reach (two one-bar chords: 2 bars), never shorter than before, so a take with nothing new drops every bar it added. Count-in is on by default. With Overdub the pattern loops and layers |
| Motion recording | done: a knob turned while recording (editor parameters, rack macros, mapped controllers) writes the steps it passes as locks (step tracks) or the macro's lane (piano-roll tracks); recording never erases notes |
| Scale change moves the notes | done: a new root or scale in the Play drawer transposes every melodic note of every pattern (steps and clips; drums stay). A new root shifts by the nearer interval (C → F up a fourth, F → C down a fourth); a new mode keeps each note's degree (C Phrygian's D♭ becomes C Minor's D; an off-scale note keeps its distance from the degree below); both at once do both (C Phrygian → F Minor); scales with a different number of notes snap to the nearest note. Chromatic, before or after, changes nothing. One undo step restores every pattern |
| Transpose buttons | done: the roll toolbar's -12 / -1 / +1 / +12 (the selection, or every note; ±1 is a scale step under scale lock); the keyboard labels the scale's root when a scale is in use |
| Performance mode | done: the top bar's ⏭ button (Ctrl+Shift+P, MIDI-learnable): a picked pattern (click, Alt+1…4, MIDI) starts when the playing one has played to its end (or its loop range); the queued pad shows a countdown; picking it again cancels; the view stays on what plays until the switch (mockup 16_perform) |

## Order of work
1. Steps / piano-roll split (model, playback, editor, conversion, recording routes, files). **done**
2. The rest of P0 (paint / erase, dotted, pause / seek / loop range, stuck-note audit). **done**
3. P1 (MIDI files, note lock, chord tools, fit selection, autosave, sustain recording, then CC / pitch bend / mod
   with instrument support). **done** except the arrangement
4. P2: not needed.

## Detailed reference (the piano roll as built)

## Toolbar (Notes view, above the piano roll)
Select / Draw tool · Grid (snap value, triplets, snap on / off) · new-note Length · Quantize · Functions · Scale (highlight / fold) ·
Ghosts · Zoom − / + / Fit · Undo · Redo · Follow. Above the notes a ruler (bar and beat numbers), below them a scroll bar.

## Grid and snapping
| # | Function | How | Status |
|---|---|---|---|
| 48 | Grid value | toolbar *Grid*: Adaptive (follows the zoom), 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1 bar | done |
| 49 | Triplet grids | *Grid*: 1/32T, 1/16T, 1/8T, 1/4T; the Triplet item turns the current value into its triplet | done |
| 50 | Snap on / off | *Grid* ▸ Snap, Ctrl+4; hold Alt while dragging to place freely (1/48 step) | done |
| 51 | Grid lines | drawn at the grid value; beats and bars stronger; triplet lines when a triplet grid is on | done |
| 52 | Drawing snaps | a new note starts on the grid line at or before the click; dragging sets its length to whole grid units | done |
| 53 | Moving snaps | the dragged note's start snaps to the nearest grid line, the other selected notes keep their distance to it | done |
| 54 | Resizing snaps | the dragged end (or start) snaps to the nearest grid line; at least one grid unit long | done |
| 55 | Nudge by grid | ←/→ move the selection a grid unit (Shift: a bar; Ctrl: 1/48 step) | done |
| 56 | Quantize to the grid | *Quantize*: starts (and optionally ends) to the grid value at 100 / 75 / 50 %; Q = 100 % | done |
| 57 | Off-grid notes | notes keep positions between steps (±½ step per note); they play early or late exactly where drawn | done |

## Zoom and scroll
| # | Function | How | Status |
|---|---|---|---|
| 58 | Zoom in / out (time) | toolbar − / +, Ctrl+wheel (around the mouse), Ctrl+= / Ctrl+−; from a quarter bar to the whole pattern | done |
| 59 | Zoom to fit | toolbar *Fit*, Ctrl+0, double-click the ruler: the whole pattern in view | done |
| 60 | Note height | Alt+wheel, Ctrl+Shift+= / Ctrl+Shift+−: fewer (taller) or more rows | done |
| 61 | Ruler | bar and beat numbers above the notes; drag sideways to scroll, up / down to zoom (around the point) | done |
| 62 | Scroll | wheel: pitch; Shift+wheel or a sideways swipe: time; the scroll bar under the notes (drag it, click beside it) | done |
| 63 | The view while editing | dragging notes or a box past the edge scrolls the view | done |

## Several bars
| # | Function | How | Status |
|---|---|---|---|
| 64 | Edit across bars | any range of bars in view (zoom), notes drawn, selected, moved and resized across bar lines | done |
| 65 | Long notes | a note can last up to the pattern's end (many bars) | done |
| 66 | Bars in the bar strip | the bars in view are highlighted; a click scrolls the roll to that bar; the playing bar is outlined and fills as it plays, and the top bar's position display beside the transport shows bar . beat . step (`2.3.1`, OF 4 BARS) on every page | done |

## Selection
| # | Function | How | Status |
|---|---|---|---|
| 1 | Select a note | click it (both tools) | done |
| 2 | Add / remove notes | Shift-click | done |
| 3 | Box selection | Select tool: drag on empty space; Draw tool: Ctrl-drag (Shift-drag adds) | done |
| 4 | Select all / none / invert | Ctrl+A, Esc, Ctrl+Shift+A; Functions menu | done |
| 5 | All notes of a pitch | click a key on the keyboard with Shift (a plain click auditions) | done |
| 6 | Clear selection | click empty space (Select tool), Esc | done |

## Adding and drawing
| # | Function | How | Status |
|---|---|---|---|
| 7 | Draw tool | toolbar or B: click adds a note, drag sets its length; Select tool: double-click adds | done |
| 8 | New-note length | toolbar *Length*: Last used (the note last selected / drawn / resized), 1/32 … 1 bar | done |
| 9 | New-note velocity | the last used velocity | done |
| 10 | Audition | notes sound when added, selected or moved; keyboard keys sound when clicked | done |

## Editing
| # | Function | How | Status |
|---|---|---|---|
| 11 | Move | drag (time and pitch); Shift-drag keeps the pitch | done |
| 12 | Copy by dragging | Ctrl-drag (Alt is free placement, see Grid) | done |
| 13 | Resize | drag either end; every selected note changes together | done |
| 14 | Nudge | ←/→ a grid unit (Shift a bar, Ctrl 1/48 step), ↑/↓ a semitone (Shift an octave) when notes are selected | done |
| 15 | Transpose | Functions ▸ Transpose ±1 / ±12 | done |
| 16 | Delete | Delete / Backspace, double-click or right-click a note, Functions ▸ Delete | done |
| 17 | Mute / unmute notes (kept, not played) | 0 key, Functions ▸ Mute | done |
| 18 | Cut / copy / paste / duplicate | Ctrl+X / C / V / D, Functions menu | done |
| 19 | Undo / redo | toolbar buttons, Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y (every sequence edit, a recording pass is one step) | done |
| 20 | Per-note length and velocity | each chord note has its own | done |

## Functions (toolbar menu; apply to the selection, or every note when nothing is selected)
| # | Function | What it does | Status |
|---|---|---|---|
| 21 | Quantize | snaps micro-timing to the grid at 100 / 75 / 50 %; Q = 100 % (toolbar *Quantize*) | done |
| 22 | Humanize | small random timing and velocity changes | done |
| 23 | Legato | each note lasts until the next one | done |
| 24 | Set length | to the new-note length | done |
| 25 | Stretch ×2 / ÷2 | doubles / halves positions and lengths from the first selected note | done |
| 26 | Reverse | mirrors the notes in time | done |
| 27 | Invert | flips the pitches around the selection's middle | done |
| 28 | Fit to scale | moves notes to the project scale (Play drawer) | done |
| 29 | Chop | splits notes into grid-step pieces | done |
| 30 | Glue | joins touching notes of the same pitch | done |
| 31 | Strum | staggers chord notes (up) and softens the later ones | done |
| 32 | Velocity: randomize, ramp up / down, set | | done |
| 33 | Rotate left / right | shifts the pattern by a step, wrapping around | done |
| 34 | Insert / delete step | at the first selected step: later notes move right / left | done |

## View
| # | Function | How | Status |
|---|---|---|---|
| 35 | Zoom | see Zoom and scroll | done |
| 36 | Scroll | see Zoom and scroll; ←/→ (bars, docs/keyboard.md) | done |
| 37 | Follow playhead | toolbar *Follow* | done |
| 38 | Scale highlight / fold | rows of the project scale are tinted; *Fold* shows only rows in use (or in the scale) | done |
| 39 | Ghost notes | other tracks' notes shown faintly behind | done |
| 40 | Notes across bars | drawn and selectable where they ring | done |

## Steps, lanes and patterns
| # | Function | How | Status |
|---|---|---|---|
| 41 | Toggle / paint steps | click, drag across (track rows, drum grid) | done |
| 42 | Lane values | velocity (selected notes or the step), probability, ratchet, micro, accent / slide, locks: drag, right-click clears | done |
| 43 | Trig conditions | a Condition lane: always, 1:2, 2:2, 1:3 … 4:4 (play on the Nth of every M loops), First, Not first | done |
| 44 | Bar copy / paste / clear | bar strip right-click menu | done |
| 45 | Grow a pattern | the bar count's > repeats bars into new, empty ones | done |

## Recording
| # | Function | Status |
|---|---|---|
| 46 | Live (quantized, held length = note length, overdub / replace) and Step (cursor, chords, Rest, Back, To start) | done |
| 47 | A recording pass is one undo step | done |

## Left out
- Per-track pattern lengths (polymeter), MPE, note colours, automation clips: they would change the project model
  well beyond the editor; not needed for the groovebox's pattern workflow.
- Two notes of the same pitch closer than half a step: each step holds a pitch once (a note sits on its nearest step
  with up to ±½ step of its own offset).
