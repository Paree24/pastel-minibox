# MIDI learn and controller maps: expected functionality

The common ground of MIDI learn in DAWs and grooveboxes (Ableton Live, Bitwig, Reaper, Logic, Elektron/Polyend
devices). Status is kept current. Test: `Test/MidiLearnTests.cpp`; screenshot `15_midi_learn` (tool_ui_shots) against mockup 15.
Standalone only: in the plugin the host's own MIDI learn / automation does this.

## Learning
| # | Function | How | Status |
|---|---|---|---|
| 1 | Learn mode on / off | `⋯ > MIDI learn`, Ctrl+M; the drawer's × or Esc turns it off (the drawer is open while it is on) | done |
| 2 | Learnable controls are visible | in learn mode every knob, fader and learnable button is tinted; mapped ones show their message (e.g. `CC 74`) | done |
| 3 | Map a control | click a control (it is armed), then move a knob / fader or press a button on the controller | done |
| 4 | Map an action | top-bar buttons are learnable (play, stop, record, metronome, tap, A–H, Play drawer); the panel's *Action* menu arms the rest (select instrument ↑/↓, bar ←/→, undo, redo, save, back to start, …) | done |
| 5 | Follow the selected instrument | macro knobs (and level / pan) can be mapped "to the selected instrument" so one set of knobs plays whichever instrument is selected | done |
| 6 | Replace / remove | learning a message that is already mapped moves it; right-click a mapped control (in learn mode) or × in the list removes it; Clear all | done |
| 7 | Message kinds | CC (absolute, relative encoders: two's complement, binary offset, sign-bit — detected while learning), notes (buttons: trigger, toggle, momentary), pitch bend (14-bit faders) | done |
| 8 | Ports and channels | a mapping can be tied to a port (a controller's DAW port vs its keys port) and channel, so the same CC on two ports can do different things | done |
| 9 | Mapped messages don't play notes | a mapped note / CC is used by its mapping only; everything else reaches the instruments as before | done |
| 10 | Range and pickup | min / max per mapping; absolute controls can wait to pass the current value before taking over (pickup) | done |

## Maps
| # | Function | How | Status |
|---|---|---|---|
| 11 | The current map persists | saved automatically and loaded at start | done |
| 12 | Save / load map files | the panel's Save map… / Load map… (`.mbmidi`, JSON) | done |
| 13 | Factory maps | a list of shipped maps (KeyLab Essential 49 mk2) | done |
| 14 | Mapping list | the panel lists every mapping: message, port, target, mode, with remove | done |

## Factory map: KeyLab Essential 49 mk2
`Factory/midimaps/KeyLab Essential 49 mk2.mbmidi`, picked automatically on the first run when the controller is
connected; every row is named (Knob 1 …) and editable in **Settings ▸ MIDI controller** (target, mode, re-learn,
add, remove). Built from the controller's documented factory user-preset CCs; check them with *Last received*.
- **User mode (MID port):** knobs 1–8 (CC 74, 71, 76, 77, 93, 18, 19, 16) → macros 1–8 of the selected instrument,
  knob 9 (CC 17) → its pan; faders 1–8 (CC 73, 75, 79, 72, 80, 81, 82, 83) → track levels 1–8; master fader (CC 85) → volume.
- **DAW mode (DAW port, Mackie protocol):** Play, Stop, Record, Rewind (back to start), Fast-forward (next bar),
  Metro, Save, Undo; track ◀ ▶ → previous / next instrument, bank ◀ ▶ → previous / next preset; encoders
  (CC 16–23, relative) → macros 1–8; faders 1–8 → track levels, master fader → volume.
- Pads and keys are not mapped: they play the selected instrument.
