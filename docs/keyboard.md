# Keyboard: expected functionality

Everything can be reached and operated without a mouse, following the common conventions of desktop apps
and DAWs (focus moves with Tab, arrows move or adjust, Enter activates, Esc steps back), while the computer
keyboard still plays notes (`A S D F G H J K` white keys, `W E T Y U O P` black keys, `Z`/`X` octave).
Status is kept current. Test: `Test/KeyboardTests.cpp` (drives the real window by keys only).

## Focus
| # | Function | Keys | Status |
|---|---|---|---|
| 1 | Move focus through every control of the visible page (buttons, chips, knobs, faders, cells, pads, the piano roll) | Tab / Shift+Tab (reading order) | done |
| 2 | See where focus is | a ring around the focused control | done |
| 3 | Move focus spatially | arrow keys go to the nearest control in that direction (on buttons, chips and cells) | done |
| 4 | Activate | Enter / Return clicks the focused control (opens menus, toggles steps, presses buttons) | done |
| 5 | Adjust | on a knob or fader: ↑/↓ a step, Shift for fine, Page Up/Down large steps, Delete resets to default; on a choice: ←/→ | done |
| 6 | Leave focus | Esc clears focus (a second Esc closes the drawer / editor) | done |
| 7 | Menus, browsers and dialogs | arrow keys, Enter and Esc (built in) | done |

## Global shortcuts (work whenever no text field has focus)
| # | Function | Keys | Status |
|---|---|---|---|
| 8 | Play / stop; back to start | Space; Home | done |
| 9 | Record | Ctrl+R live record, Ctrl+Shift+R step record (the LIVE and STEP buttons; again turns it off) | done |
| 10 | Pages | Ctrl+1 Rack, Ctrl+2 Seq, Ctrl+3 editor of the selected instrument (also Enter on a rack strip); Esc goes back | done |
| 11 | Drawers | Ctrl+P Play drawer, Ctrl+M MIDI learn, Alt+R recording settings (↑ ↓ a row, ← → its value; Step: ← Back, → Rest) | done |
| 12 | Instrument | ↑/↓ select (when nothing is focused, every page); Rack and editor pages: ←/→ previous / next preset; Ctrl+B its preset browser; Ctrl+[ / Ctrl+] previous / next preset; + / = adds an instrument to the first empty track (device menu) | done |
| 13 | Patterns | Alt+1…8 switch to A–H (in performance mode: queued to the pattern's end); Ctrl+Shift+P performance mode | done |
| 14 | Tempo | Ctrl+↑ / Ctrl+↓ ±1 BPM (Shift ±0.1); metronome Ctrl+K | done |
| 15 | Undo / redo, save, new, open, export | Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y, Ctrl+S, Ctrl+N, Ctrl+O, Ctrl+E (WAV), Ctrl+Shift+E (MIDI), Ctrl+Alt+E (DAWproject) | done |

## Sequencer
| # | Function | Keys | Status |
|---|---|---|---|
| 16 | Bars | Seq page: ← / → previous / next bar (when nothing is focused or selected) | done |
| 17 | Step recording | ← / → move the record cursor (nothing is erased); Rest, Back and To start as in the drawer | done |
| 18 | Piano roll with focus | arrows move a cursor cell (Shift: a bar / an octave), Enter adds / removes the note there, Shift+Enter adds / removes it from the selection, Alt+arrows nudge the selection; the editing keys of docs/sequencer.md apply | done |
| 19 | Step rows and drum grid | arrow between cells, Enter toggles | done |
| 20 | Piano roll view | Ctrl+= / Ctrl+− zoom in time, Ctrl+Shift+= / Ctrl+Shift+− note height, Ctrl+0 the whole pattern, Ctrl+4 snap on / off | done |
| 21 | Piano roll nudge | ←/→ a grid unit (Shift a bar, Ctrl 1/48 step) with notes selected | done |

## Shortcuts are configurable
The arrows are page keys: each page has its own entry in Settings ▸ Keyboard (Rack page, Seq page, Editor page),
so the same keys can do what fits the page.

Every shortcut above (global, piano roll, the computer-keyboard notes and octave) is rebindable in **Settings ▸
Keyboard** (`⋯ ▸ Settings`, Ctrl+,): change, add a second key, reset to defaults; saved to
`<user app data>/Pastel Minibox/shortcuts.xml`. Focus keys (Tab, arrows, Enter, Esc) stay fixed.

## Everything the mouse does, by keys
With a control focused (Tab / arrows):

| Mouse | Keys | Status |
|---|---|---|
| Click | Enter | done |
| Shift / Alt / Ctrl-click | Shift / Alt / Ctrl+Enter (pad copy / clear, step lock / slide / accent …) | done |
| Right-click (clear a value, remove, context menus: bars, pattern pads, notes) | Shift+F10 or Delete | done |
| Double-click (reset a knob, fit the ruler, load a preset) | F2 | done |
| Drag a knob or fader | ↑ / ↓ (Shift fine), Page Up / Down, Delete resets | done |
| Drag a lane value (velocity, gate, probability, ratchet, micro, lock) | ↑ / ↓ on the focused cell (Shift fine) | done |
| Drag a Steps note cell (pitch) | ↑ / ↓ (Shift an octave) | done |
| Paint steps / drum cells by dragging | Enter on each cell | done |
| Piano roll: draw, select, move, resize, velocity | the cursor (arrows, Enter adds / removes, Shift+Enter selects), arrows nudge, `,` / `.` length, `[` / `]` velocity, Delete | done |
| Piano roll: marquee | Shift+Enter at the cursor, Ctrl+A, Ctrl+Shift+A | done |
| Piano roll: ruler click (seek), Shift-drag (loop) | Ctrl+Enter (playhead to the cursor), Ctrl+Shift+L (loop the selection), Alt+L | done |
| Piano roll: wheel, ruler and scroll bar drags (zoom, scroll) | Ctrl+= / Ctrl+−, Ctrl+Shift+= / −, Ctrl+0, Ctrl+Shift+0; the cursor scrolls the view | done |
| Clip lanes: drag velocity stalks / draw a controller | focus the lane: ← / → the cursor step, ↑ / ↓ the value there (velocity: the selection) | done |
| Editor visuals: envelope points, sample markers, FM wave / mute, drum-lane steps | focus them: arrows move / adjust, Enter toggles | done |
| Top bar: BPM drag / double-click | Ctrl+↑ / ↓; Enter on the BPM types a tempo | done |
| Top bar: time signature popover | ↑ / ↓ a row, ← / → its value | done |
| MIDI learn: click a control to arm, right-click to remove | Enter / Delete on the focused control; Page Up / Down scrolls the list | done |
| Preset browser | ↑ / ↓ the list, Enter loads, Tab the chips and buttons | done |
| Dropping sample files | the Load… and Browse buttons | done |
| The plugin window | Tab, arrows, Enter, Esc as in the app | done |
