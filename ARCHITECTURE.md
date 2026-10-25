# Pastel Minibox: Architecture Guide

A small groovebox built around a rack of four devices. It ships in two builds:

| Build | What it contains |
|---|---|
| **Standalone groovebox** | Transport (record, play, stop), BPM, time signature, swing, the step sequencer and the **A–D pattern switcher**. **Every pattern owns its own 4 device slots.** |
| **Android app** (phone, portrait only) | The standalone groovebox with a touch layout. It has the same features as the desktop standalone build (see §15). |
| **Plugin "Minibox"** (VST3) | **One instrument at a time**, chosen from the 7 devices, played from host MIDI (omni) and synced to host tempo. There is no internal sequencer, pattern bank or transport. |

There are seven devices. Each one is a hybrid of several classic small-format instruments rather than a 1:1 copy of any one of them, so this guide describes the architecture that is actually being built and not the reference hardware.

> Mockups: `mockups/svg/*.svg` (desktop) and `mockups/mobile/svg/*.svg` (Android), with PNG renders in the sibling `png/` folders. Visual rules: `mockups/SPEC.md`. Regenerate with `mockups/tools/build.py`.

---

## 1. Product rules

1. Four tracks and four patterns (A–D). A pattern lasts 1–16 bars.
2. **Devices belong to a pattern.** Pattern A might be Drums, Acid, VA and FM, while pattern B is DrumSampler, Bassline, VA (VCO engine) and Sampler. A slot can be empty.
3. Pattern switching is musical. By default a switch is queued to the end of the current bar; other quantize options are in §4.3.
4. Each device has **8 macros**, which make up its rack face, and a full editor behind the slot's open button.
5. The rack and the devices are the same code in both builds. The standalone build wraps them in a sequencer and a pattern bank.
6. The design follows the Pastel family look: dark and pastel, one colour per track, low clutter, and no instructional text on screen.

---

## 2. System overview

```
                    ┌──────────────────────────── Standalone only ──────────────────────────┐
                    │  Transport ── Clock (BPM, time sig, swing) ── Pattern Scheduler      │
                    │      │                                     │                         │
                    │  Recorder (step/live) ──► Sequencer ◄──────┘ (A–D, queued switch)    │
                    └──────────────────────────────────┬────────────────────────────────────┘
                                                       │  NoteEvents + ParamLocks (sample-accurate)
 Host MIDI (plugin) ── Channel router ch1–4 ───────────┤
                                                       ▼
 ┌──────────────────────────────────────────── Rack (both builds) ───────────────────────────────────────────┐
 │  Slot 1..4:  DeviceProcessor ─► Level/Pan ─► Mute/Solo ─┬─► Main bus ─► Glue comp ─► Limiter ─► Out      │
 │                                                         ├─► Send A ─► Delay  ──┘                          │
 │                                                         └─► Send B ─► Reverb ──┘                          │
 │  (plugin: optional per-slot stereo outs 3-4 / 5-6 / 7-8 / 9-10)                                          │
 └───────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### Layers

| Layer | Owns | Thread |
|---|---|---|
| `Engine` | rack, master FX, device pool, render loop | audio |
| `Sequencer` | pattern playback, recording, scheduler | audio (logic); UI edits are passed in via a FIFO |
| `Model` | project, patterns, steps, device state (immutable snapshots) | message |
| `UI` | views and editors | message |
| `IO` | project and preset files, sample loading and decoding | background worker |

Threading (as built):

- **Parameters** are atomic floats with per-block smoothing, so knobs write them directly from the UI.
- **Structural changes** go through `Engine`'s audio lock, which the audio callback holds for each block. These are device swaps, rack rebuilds, project loads, transport and pattern switches, note events from the UI, and edits to lock-lane vectors or whole patterns (`Engine::ScopedEdit`). New devices and racks are built *outside* the lock, swapped in under it, and freed after it is released, so the audio thread never waits on allocation.
- **Samples** live in a `SampleSlot`. A spin lock guards only the pointer swap. A replaced buffer is retired and freed later on the message thread, never on the audio thread.
- A lock-free command queue remains a possible later refinement.

---

## 3. Data model

```
Project
 ├─ tempo: 30–300 BPM (0.1 resolution)    ├─ timeSig: beats 1–16 / unit {4, 8, 16}
 ├─ swing: 50–75 %                        ├─ countIn: off | 1 bar
 ├─ master: { delay, reverb, glue, limiter, volume }
 └─ patterns[4]  (A–D)
      ├─ lengthBars: 1–16
      ├─ stepRes: 1/8 | 1/16 | 1/32      (default 1/16)
      └─ slots[4]
           ├─ device: { type, version, state blob, macroMap[8] } | empty
           ├─ mixer:  { level, pan, mute, solo, sendA, sendB }
           └─ track:  { steps[], lanes[] (drum devices), lockLanes[], trackLength? (polymeter, optional) }
```

**Steps per bar** = `beats × (16 / unit)` at 1/16 resolution: 4/4 gives 16 steps, 3/4 gives 12, 7/8 gives 14 and 5/16 gives 5. This is clamped so that a pattern never exceeds `16 bars × 32 steps`, which is 512 steps per track.

**Step**

```cpp
struct Step {
    uint8_t  noteCount;          // 0 = rest; up to 6 (chords for VA/FM/Sampler)
    uint8_t  notes[6];
    uint8_t  vel[6];             // per note, 1–127
    uint16_t len[6];             // per note, 1/128 of a step (up to ~511 steps: notes can span many bars)
    int8_t   off[6];             // per note, ±24 ticks (1/48 step): off-grid notes, early or late
    int8_t   microTiming;        // the step's shift, ±1/2 step in 1/48 increments
    uint8_t  probability;        // 0–100 %
    uint8_t  ratchet;            // 1–4 repeats
    uint8_t  flags;              // ACCENT | SLIDE | TIE  (Acid uses accent/slide)
};
```
**Two kinds of sequence per track** (docs/sequencer.md): the steps above, or a **piano-roll clip**
(`Track::pianoRoll`, `std::vector<ClipNote> clip`: start / length in `kPPQ` = 1920 ticks a quarter, note, velocity,
accent / slide flags, mute, a stable id). Steps convert to a clip one way (`Track::convertToPianoRoll`); live
recording writes clips (the audio thread queues `RecEvent`s, `Engine::applyRecorded` writes them on the message
thread, converting a step track on its first note), step recording writes steps. The sequencer plays a clip's
notes at their exact ticks. Files: `"pianoRoll": true, "ppq", "clip": [[start, length, note, vel, flags, muted]]`.

A step note's position is `step × 48 + microTiming + off` (in 1/48 step). The piano roll places notes at any tick (grid snapping,
triplets, free with Alt) on the nearest step that can hold them; the sequencer plays notes timed before their
step one step ahead, so early notes sound exactly where they are drawn.

**Drum devices** (Drums, DrumSampler) use eight `Lane`s of `DrumStep { on, velocity, probability, ratchet, microTiming }` instead of note steps.

**Parameter locks** (motion sequencing): `LockLane { paramId, values[steps] (NaN = no lock) }`. Each track can have up to 8 lock lanes. In live recording, moving a knob writes locks on the steps it passes over.

**Memory**: the worst case is 4 patterns × 4 tracks × 512 steps × 24 B ≈ 200 KB plus lock lanes, so the whole pattern bank stays resident.

---

## 4. Standalone: transport, clock and patterns

### 4.1 Transport controls (top bar)
| Control | Behaviour |
|---|---|
| **Record** | Arms recording. In Live mode, notes and knob moves are recorded into the current pattern while it plays. In Step mode, record plus a step cell enters notes from the keyboard or MIDI. |
| **Play** | Starts from bar 1 of the current pattern, or continues if paused. A ring around the button shows the position in the pattern. |
| **Stop** | Pressing it once stops and sends note-offs, and devices finish their release tails. Pressing it twice resets to bar 1 and kills tails. |
| **BPM** | −/+ buttons (±1, or ±0.1 with Shift), drag on the value, double-click to type, and **Tap**. |
| **Time signature** | The `4/4 ▾` popover holds beats (1–16), unit (4/8/16), swing and count-in. Changing it while playing applies at the next bar. |
| **Metronome** | Toggles the click; the click level lives in `⋯`. |

### 4.2 Clock
- A sample-accurate PPQN clock runs internally at 96 PPQN. Swing delays the off-beat 1/16 steps (or 1/8, depending on the resolution).
- `Clock::advance(numSamples)` yields events at their exact sample offsets, and the rack renders in **sub-blocks** split at those offsets, so notes and locks land sample-accurately.
- Optional MIDI clock out and in, plus network tempo sync, are future work. The scheduler takes its tempo from an abstract `TempoSource`.

### 4.3 Pattern switcher (A–D)
- **Click** a pattern to queue it. The queued pattern gets a dashed outline, then switches at the chosen quantize point: `Bar` (default), `Pattern end` or `Immediate`, set in `⋯`.
- **Performance mode** (the ⏭ button left of A–D, Ctrl+Shift+P, mockup `16_perform`): a picked pattern always waits until the playing one has played to its end (or the end of its loop range), its pad counts down, a second pick cancels, and the rack / editors keep showing the playing pattern until the switch.
- **Shift-click** copies the current pattern into the target, devices included. **Alt-click** clears the target.
- Each button shows 4 dots in the colours of its slots, so empty slots read as grey.
- Pattern length is set from the bar strip on the Seq view (`‹ 8 bars ›`).
- **Chain mode** (future): a play order such as A A B A C.

### 4.4 Devices per pattern: engine design
Since every pattern has its own devices, switching patterns swaps up to four instruments mid-performance.

1. **Device pool.** When a project loads, every device in every pattern is instantiated, which is at most 16 instances. Allocation never happens on the audio thread.
2. **Active set.** Only the current pattern's devices receive events.
3. **Tail handoff.** On a switch, the outgoing devices get `allNotesOff()` and keep rendering until they report `isSilent()` (an RMS gate of −90 dB) or until 4 s have passed. They then go to sleep with zero CPU.
4. **Sleeping devices** keep their state and do not process audio.
5. **Editing a non-playing pattern** is allowed. Its devices are live for auditioning: when the Seq view is focused on a pattern other than the playing one, preview notes route to that pattern's devices.
6. **Shared sends.** All patterns feed the same master Delay and Reverb, so tails ring across a switch.

CPU budget: the active set plus at most one tailing set. This is the worst case for a switch between two FM- and VA-heavy patterns. See §10.

### 4.5 Recording
- **Record mode** (Play drawer, or `⋯ > Recording`): *Live* or *Step*. **Back to start** (the ⏮ button in the drawer's Record card) returns the playhead and the step cursor to step 1; Stop pressed twice does the same.
- **Live** recording writes into the playing pattern while the transport runs: each note lands on the nearest step at a quantize strength (0–100 %, the rest kept as micro-timing), keys struck together (within 60 ms) land on the same step as a chord, and its gate is how long the key was held (a note held across steps ties over). Recording only adds (nothing already there is erased). With Extend (the default; Overdub off) the pattern never loops back while recording: at its end it gains a bar (the other tracks repeat into it) and the take goes on; when the take stops the pattern is the bars its notes reach, never shorter than before; with Overdub the pattern loops and the take layers. What is heard is what is recorded: chord-mode chords and arp notes included.
- **Step** recording writes at a visible cursor (a red column in the Seq view and step strip), with or without the transport: the keys held together form one entry (a chord), and releasing the last one moves the cursor on by the step **Length** (1/16, 1/8, 1/4 or 1/2), with the gate spanning that length. **Rest** leaves a gap (also `→`), **Back** steps back and clears that entry. Chord mode applies; the arp does not (it needs time).
- **Play drawer** (top-bar keyboard button, mockup `14_play`): how live input plays. *Scale* is the whole project's key (labelled so in the drawer): **Lock** snaps off-scale keys into the scale (nearest, down or up) and **White keys** makes the white keys play the scale's degrees — two project-wide toggles, either can be off; the twelve note cells set the root and the scale menu holds 63 scales. *Chord* (per track): one key plays a chord, Auto follows the scale (diatonic triads, power chords for the pentatonic-style scales) and each degree can be set to its own chord; inversion (root, 1st, 2nd), an octave lower, a bass note below, strum. *Arp* (per track): Up, Down, Up-Down, Down-Up, Random or Played order, 1/4 to 1/64 (dotted, triplet), 1–4 octaves, gate, swing, latch; it follows the sequencer clock while playing and the project tempo when stopped. Drum tracks take the keys as they are. Ported from the user's Midi Note Modifier at their request (`Source/seq/Scales.h`, `Source/seq/NoteInput.cpp`).
- **Knob recording** writes locks when record is on and the transport is playing. Holding a step and turning a knob locks only that step.
- **Input** (standalone): every connected MIDI keyboard is enabled, and every channel plays the **selected instrument**: click anywhere on its rack strip (its knobs included), press Up / Down to move between instruments, click its Seq row or open its editor. The selected strip is outlined in its colour with a keyboard marker (also shown on its Seq row). `⋯ > MIDI channels 1-4 play tracks 1-4` restores per-channel routing. The computer keyboard plays the Input track too: the home row `A S D F G H J K` gives white keys, `W E T Y U O P` gives black keys, and `Z`/`X` shift the octave.
- **MIDI learn** (standalone, docs/midi-learn.md, mockup `15`): `⋯ > MIDI learn` or Ctrl+M opens the learn drawer and tints every learnable control (macros, mixer, master, editor parameters, top-bar buttons, tempo). Click a control, move a hardware control: the message is mapped (CC absolute or relative, notes as trigger / toggle / momentary, pitch bend), tied to its input port. Macros, mixer and parameters follow the selected instrument by default. A `MidiRouter` sits between the MIDI inputs and the audio thread: mapped messages go to `MidiMap` (applied on the message thread), the rest play the instruments. The map in use is saved automatically; `.mbmidi` files (JSON) save and load maps; `Factory/midimaps` ships the KeyLab Essential 49 mk2 map.
- **Editing** in the Seq piano roll follows the common piano-roll conventions, listed with their gestures and tests in `docs/sequencer.md`: add / draw, select (click, Shift-click, box, Ctrl+A), move and copy (drag, Alt-drag, arrow keys), resize either end, delete, clipboard and duplicate, quantize, undo / redo for every sequence edit, per-note length and velocity, notes ringing across bars, follow playhead. New notes take the length of the note last selected, drawn or resized. Steps and drum cells toggle on click and paint on drag. Growing a pattern (the bar count's `>`) repeats its bars into the new, empty ones; shrinking keeps the hidden bars.
- **Feedback:** every button and cell brightens under the mouse (with a pointing-hand cursor) and dims while pressed.
- **Samples** load from the **Browse** and **Load…** buttons on the Sample card (and the DrumSampler's Slice card), by dropping WAV, AIFF or FLAC files on the editor, or from the editor's `⋯` menu. *Browse* lists the factory library: Sampler sources by family, drum kits (one sound per part) and loops; *Load…* opens a file. The card shows the loaded sample's name. Several files on the Sampler become key zones. On the DrumSampler, a file dropped on a pad loads that pad, and a loop dropped on (or chosen in) the Slice card is sliced across the pads.
- **Reverb card:** every device editor has its own Reverb card (Mix, Size, Tone, Pre-delay) driving a plate inside the device (`DeviceReverb`), so a preset's space travels with it into the plugin. On Drums and DrumSampler the per-part Reverb sends feed the card, whose Mix is the return. The master Reverb (§1) remains a project-wide send in the standalone.

### 4.6 Presets
Each synth ships **128 presets** in 8 families of 16 (for example Bass, Sub, Lead, Pluck, Wobble, Driven, Chord Stab and FX on Bassline). Each drum device ships **32 kits**, one per style (Boom Bap, House, Techno Rumble, Brushed Jazz …). Names describe what the preset does, such as *Resonant Dark Wobble* or *Inharmonic Sweeping Bell*.

- **Rules:** presets never use detune, tune or transpose offsets, octave variants, glide, pitch LFO or pitch EG. They differ in their oscillators, filters, envelopes, LFO and drive/FM/modulation. Velocity, glide and level do not count as differences. There is no Init preset; a new slot or plugin tab starts on preset 001.
- **Data, not code:** factory presets are files in `Factory/presets/<device>/NNN Name [Family].mbx-<device>`, the same JSON as user presets. They are read at runtime from the bundle (`Contents/Resources/Factory/presets`), next to the standalone binary, or from the source tree.
- **Making the bank:** every synth preset and drum kit is hand-designed (`Source/app/presets/bank_*.py`): a named sound with its oscillators, filter, envelopes, modulation and effects. `banks.py write` writes them. `MiniboxTests tool_nudge_presets` then renders every preset and, where two sound too alike, moves only secondary settings of one of them within bounds that keep its name honest. `MiniboxTests tool_balance_presets` sets each preset's level for a common loudness. Tools run only when named; the plain test run never rewrites content.
- **Management (in the preset browser):** search; bank buttons (All, the families, User); a numbered list where selecting a row loads it and a double-click closes the browser; up/down arrows audition. An inline name box with **Save** writes a user preset, and a second click (**Overwrite?**) confirms replacing one. **Over** replaces the selected factory preset with the current sound: the copy goes to `Documents/Minibox Presets/<Device>/Factory/` under the same file name and shadows the shipped file (marked `*`). **Del** deletes a user preset or restores a shadowed factory preset. The header shows the preset name with its family chip, prev/next arrows and a save button that opens the browser.
- **Tests:** every preset renders finite, bounded and audible. No preset changes a forbidden parameter. Every pair within a device must differ by rendered audio (distance at least 0.6), measured on the envelope, the 24-band spectral shape and the brightness over time. File management (numbering, shadowing, restore, user save/delete) is tested in a scratch folder.
- **Sample content:** recorded from Minibox's own engines (`banks.py jobs`, then `MiniboxTests tool_render_samples`, then `banks.py samplers`): 128 Sampler sources (recordings of the synth banks' keys, pads, strings and brass, basses, leads, plucks, bells and textures), the 32 drum kits as one-shots for the Drum Sampler (each kit with its own sampler treatment), and 2 slicer loops played on the drum kits.

---

## 5. Plugin build: one instrument

| Topic | Decision |
|---|---|
| Instrument | One device at a time. The header's instrument tabs switch between Bassline, VA, Acid, FM, Sampler, Drums and Drum Sampler; switching loads that device's default preset. |
| Input | Host MIDI on any channel (omni), with sustain, all-notes-off and sample-accurate timing. |
| Output | Main stereo out. |
| Tempo | Host BPM and position drive tempo-synced LFOs, delays and the VA arpeggiator. |
| GUI | A top bar with the logo and instrument tabs, then exactly the standalone device editor (`03`-`10`) in the device's own colour: header (device pill, engine switch for VA, preset browser, save, Compare, `⋯`) and section cards, with a MIDI activity row in the step strip's place. There is no rack, Seq page or transport. |
| Formats | VST3 only for now. The factory samples and preset files ship inside the bundle (`Contents/Resources/Factory`). |
| Presets | The same factory files and preset browser as the standalone (§4.6). User presets live in `Documents/Minibox Presets/<Device>/`. |
| Host parameters | `Macro 1–8`, `Level`, `Pan` and `Mute` for the instrument, plus `Master Volume`, `Delay Send Return` (the Drums / DrumSampler part delay sends) and `Reverb Return`, which scales the instrument's own Reverb card (1× at its default 0.8) (14 in all). |
| State | `getStateInformation` writes the instrument, its preset name and full device state. Sessions saved by the older four-slot rack keep slot 1 and drop the rest. |

Standalone and plugin share `Rack`, `DeviceProcessor`, every device, the UI rack view and the editors. `StandaloneApp` adds `Sequencer`, `PatternBank`, `Transport` and the top-bar transport and pattern widgets.

---

## 6. Device framework

```cpp
class DeviceProcessor {
public:
    virtual ~DeviceProcessor() = default;
    virtual DeviceType type() const = 0;
    virtual void prepare(double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;
    virtual void handleEvent(const NoteEvent&) = 0;      // note on/off, velocity, accent/slide flags
    virtual void setLock(ParamId, float value) = 0;      // per-step parameter lock (cleared on next step)
    virtual void render(AudioBlock<float> out) = 0;      // stereo, called on sub-blocks
    virtual bool isSilent() const = 0;                   // for tail handoff
    virtual ParamLayout& params() = 0;                   // typed parameter table (id, range, default, unit)
    virtual std::array<MacroMap, 8>& macros() = 0;
    virtual void save(ValueTree&) const = 0;
    virtual void load(const ValueTree&) = 0;
};
```

- **Voices.** `VoiceManager<VoiceT, N>` provides allocation modes `Poly`, `Mono`, `Legato`, `Unison` (stacking N voices) and `Paraphonic` (shared filter and amp). Stealing picks the oldest released voice first, then the quietest.
- **Parameters.** Every parameter has an id, a normalized range, a skew and a display function. Locks override the smoothed value for one step.
- **Macros.** Each macro maps to up to 4 parameters, each with its own range and curve. The defaults are listed per device below.
- **Oversampling.** 2× is used on non-linear filters and drive (Acid, Bassline, VA) and is selectable as Off/2×/4× in `⋯ > Quality`. FM uses no oversampling; its operators are band-limited by design.
- **Denormals** are flushed to zero on the audio thread. Parameters are smoothed with 10–20 ms one-pole smoothers.

---

## 7. Shared DSP library

| Module | Implementation | Used by |
|---|---|---|
| `PolyBlepOsc` | saw, pulse with PWM, triangle (with BLAMP); hard sync; drift (slow random pitch walk) | Bassline, VA, Acid |
| `SubOsc` | square at −1 or −2 octaves, or a 25 % pulse at −2 octaves, derived from the main phase | Bassline, VA, Acid |
| `Noise` | white and pink, plus sample-and-hold random | all |
| `SineOp` | phase-modulation operator: 4096-entry sine table with linear interpolation, ratio/fixed frequency, self-feedback | FM |
| `SvfTpt` | zero-delay-feedback state-variable filter, 12 dB LP/BP/HP | Sampler |
| `Ladder4` | 4-pole ZDF transistor ladder with per-stage tanh, resonance compensation and self-oscillation | Bassline, VA |
| `bass::DiodeLadder` | the bass-line diode ladder solved from its circuit: input pair into 4 coupled nodes (bottom C half), tanh current per branch, resonance loop through the coupling high-pass; plus the accent sweep circuit | Acid |
| `Ota2Pole` | 2-pole OTA-style low-pass (12 dB) with a soft-clipping resonance loop | VA (VCO engine) |
| `HpfStepped` | 4-position non-resonant high-pass (position 0 is a low-shelf boost) | VA (DCO engine) |
| `Adsr` | analog-style RC curves with exponential segments; retrigger and legato modes | most |
| `DecayEnv` | attack-free decay with an accent "sweep" capacitor model (see Acid) | Acid, Drums |
| `RateLevelEnv` | 4 rates and 4 levels with key-rate scaling | FM |
| `Lfo` | tri/saw/square/S&H/noise, free or synced, delay/fade, key-trigger | all |
| `Drive` | soft (tanh), tube (asymmetric with bias), wavefold | Drums, VA |
| `pedal::Board` | three pedal circuits at 2×: Overdrive (clipping amp above 720 Hz, feedback diodes, 723 Hz tone stage), Distortion (high-gain op-amp with GBW roll-off, hard diodes to ground, sweepable filter), Fuzz (booster, two diode-feedback stages, mid-scoop tone stack); notes in `docs/research/pedals.md` | Acid |
| `BbdChorus` | dual-delay chorus with modes I, II and I+II, plus slight companding noise and HF roll-off | VA, FM, Bassline |
| `TapeDelay` | modulated delay with filtered feedback; synced or free | VA, Acid, Bassline, master |
| `PlateReverb` | FDN plate with pre-delay, size (line scaling) and RT60 decay | master, every device's Reverb card (`DeviceReverb`) |
| `Resampler` | 4-point Hermite (clean), or sample-rate and bit-depth reduction (vintage) | Sampler, DrumSampler |
| `Waveguide` | tube and string resonator: a delay line with a damping filter | Drums (kit resonator) |
| `GlueComp`, `Limiter` | RMS bus compressor; true-peak look-ahead limiter | master |

---

## 8. Devices

Each device below gives its concept, signal flow, parameters, its 8 default macros, and what it adds beyond the classic small-format design it is loosely based on.

### 8.1 Minibox VA: 6-voice polysynth with DCO and VCO engines (`05_poly`, `09_poly_va`)

**Concept:** one polysynth with two **engines**. The engine chooses the oscillator and filter blocks. Voices, envelopes, modulation, the arpeggiator and the effects are shared. The engine switch sits in the editor header.

Both engines are subtractive polysynths built from the same blocks (`PolyBlepOsc`, `SubOsc`, `Noise`, `Adsr`, `Lfo`, `VoiceManager` and the effects). They differ only in oscillator flavour and filter model, so one device, one editor renderer and one preset bank cover both.

```
 Voice ×6 ─┬─ Osc block (engine) ─┐
           ├─ Sub (−1 oct) ───────┼─► Mixer ─► Filter block (engine) ─► VCA ─┐
           └─ Noise ──────────────┘          ▲ Filter Env, LFO, Key, Vel    ▲ Amp: ADSR | Filter Env | Gate
 Voice mode: Poly · Unison · Chord · Octave · Fifth · Ring                    │
 Arp ─► voices                                     Sum ─► Drive ─► Chorus ─► Delay ─► Out
```

| Block | DCO engine | VCO engine |
|---|---|---|
| Oscillator | one DCO per voice: Pulse, Saw or Both; PW manual, LFO or Env | saw, pulse with PWM, or triangle; octave 16' to 2'; fine tune |
| High-pass | `HpfStepped`, 4 positions (0 is a low boost) | none |
| Low-pass | `Ladder4` 24 dB | `Ota2Pole` 12 dB with filter drive; *Shared* (paraphonic: one filter and envelope for all voices) or *Per voice* |
| Default macros | Cutoff, Reso, Env, PWM, Sub, Attack, Release, Chorus | Cutoff, Peak, Env, Detune, PW, Decay, LFO Rate, Delay |

- **Voice (shared):** 6 voices with drift, which adds slight random tuning and cutoff offsets per voice, plus detune and glide. The modes are:
  - *Poly:* one oscillator per voice.
  - *Unison:* all voices stacked with detune and spread.
  - *Chord:* chord memory; each note triggers the stored shape.
  - *Octave:* oscillators at +0, +12 and −12.
  - *Fifth:* oscillators at +0, +7 and +12.
  - *Ring:* oscillator 2 ring-modulates oscillator 1, with oscillator 3 as a sub.
- **Envelopes (shared):** a filter ADSR and an amp ADSR. The amp can use its own ADSR, follow the filter envelope (the classic single-envelope DCO behaviour) or act as a plain gate.
- **LFO (shared):** tri, saw, square or S&H, synced or free, with delay (fade-in) and auto or key trigger. It targets pitch, PWM and cutoff.
- **Arpeggiator (shared):** Off, Up, Down, Up-Dn or Rand, over 1–3 octaves, with a synced rate and a gate. It follows the sequencer clock in the standalone build and the host tempo in the plugin.
- **FX (shared):** Drive (Soft, Tube or Fold), then Chorus (Off, I, II or I+II), then Delay (synced or free, feedback, tone, mix), then the Reverb card.
- **Engine switch:** both engines' settings stay in the device state, so switching back restores them. A switch releases the sounding voices, as a preset change does.
- **Presets:** 128, each tagged with its engine. The browser can filter by engine.

### 8.2 Minibox FM: 6-operator phase-modulation synth (`06_fm`)

**Concept:** a full 6-operator engine with **32 algorithms**, driven from a **macro layer** that offsets every operator at once. You can play it fast from the macros or go deep per operator.

```
 Voice ×8:  OP1..OP6 (sine + optional wave: half-sine, square-ish, saw-ish)
            each: ratio | fixed Hz, detune, level, vel sens, key-level scaling, 4-rate/4-level env
            Algorithm 1–32 routes ops ─► carriers summed ─► VCA
            Feedback on the algorithm's feedback op
 Pitch EG (4 rate / 4 level)  ─► all ops
 LFO (sine, tri, saw↓, saw↑, square, S&H) ─► pitch / amp (per-op amp-mod sensitivity), with delay
 Voice sum ─► Chorus ─► Out
```

- **Macro layer:** non-destructive offsets on top of the patch.
  - Mod Attack, Mod Decay and Mod Level scale the envelopes and levels of the **modulator** operators.
  - Car Attack, Car Decay and Car Level do the same for the **carrier** operators.
  - Which operators count as modulators or carriers depends on the algorithm.
- **Operators view:** six columns showing role, ratio, a level bar and a mini envelope. Click a column to edit that operator in the *Operator* section.
- **Voice:** Poly, Mono or Legato, 1–8 voices, transpose and glide.
- **Electric pianos:** the carriers drop quickly after the hammer and then ring on (≈ 6 dB/s), so a held note sounds for 8–12 s like a struck tine.
- **Import:** reads standard 32-voice 6-operator bank files and maps them onto the parameter set.
- **Macros:** Mod Level, Feedback, Mod Attack, Mod Decay, Car Attack, Car Decay, Algorithm, Chorus.
- **Additions:** extra operator waveforms, chorus, the Reverb card, velocity on the macros, and an operator mute (shown as a dot on each column).

### 8.3 Minibox Acid: bass line synth (`04_acid`)

**Concept:** a single-oscillator acid voice modelled from the bass-line circuit itself (research in `docs/research/synths.md`, model in `Source/dsp/Bassline.h`). An optional **Stack** mode adds fifth or octave oscillators for fatter bass.

```
 Osc: saw <-> square morph + Sub + Noise          (Stack: + fifth or octave oscillators)
   ─► coupling high-pass ≈ 8 Hz
   ─► diode ladder (4× oversampled): input pair → 4 coupled nodes (bottom C half), top pair to ground,
      tanh current in every branch, resonance loop through the Bass Comp high-pass
        ◄─ cutoff · 2^(Env Mod · MEG + Accent · sweep circuit + LFO + Key)
   ─► VCA ◄─ volume envelope (sharp attack, long decay) + accented MEG through the 1.55 ms RC
   ─► Drive pedal (Overdrive | Distortion | Fuzz, 2×) ─► Delay (duck) ─► Reverb ─► Out
 Mono voice; sequencer slide / tie steps slide the pitch CV, any other new note re-triggers
```

- **Filter:** cutoff (200 Hz to 4 kHz, the frequency the resonance sings at), resonance (up to self-oscillation), Env Mod (up to 4.5 octaves of envelope sweep), Decay (0.2 to 2 s), key tracking and **Bass Comp**, which sets the high-pass inside the resonance loop (8–150 Hz; the circuit sits near 10 Hz, so rising resonance thins the bass as it should, and higher settings keep more low end). The passband loss is made up after the core, so the input stays at circuit level and resonance keeps its bite. *Diode* is the circuit's ladder; *Ladder* drives the same core harder at its input pair for a rounder character.
- **Accent:** amount, plus **Sweep** (the circuit's sweep pot): the accented main envelope charges a capacitor through a diode (≈ 3 ms anticlockwise to ≈ 147 ms clockwise) that drains in ≈ 100 ms, so consecutive accents sweep higher; the accented envelope also reaches the VCA through a 1.55 ms RC. **Acc Decay** is the main envelope's decay on accented notes.
- **Drive:** a pedal after the VCA, as acid lines are classically played: *Overdrive*, *Distortion* or *Fuzz* circuit models (`Source/dsp/Pedals.h`) with Amount, Tone and Mix.
- **Slide:** time, in *Fixed* (constant time) or *Legato* (proportional to the interval) mode. A slide step ties the gate into the next step.
- **Amp:** *Gate* (classic: sustains for the step length) or *Decay* (a percussive pluck), plus level and velocity.
- **Keyswitches (articulations):** hold C♯0 for Accent, D0 for Slide, D♯0 for both. They are momentary: the articulation applies only while the key is held (held keys combine), then the Articulation chip's own setting returns; keyswitches never sound. Under Slide, overlapping notes glide without re-triggering, as a slide step does; under Accent every note is accented. The Accent card's chip shows the current articulation (and sets a lasting one), and the Accent and Slide visuals label the keys.
- **Sequencer flags:** accent and slide per step. These show as `A` badges and slide arcs on the step strip, and as dedicated lanes in the Seq view.
- **Macros:** Cutoff, Reso, Env Mod, Decay, Accent, Slide, Drive, Delay.
- **Additions:** Stack mode, sub and noise, LFO, three drive types, a ducking delay and velocity.

### 8.4 Minibox Drums: multi-engine drum synth (`03_drums`)

**Concept:** 8 parts, each switchable between the **machines** the device draws on, plus kit-level effects. Each engine models its machine's circuit or architecture (research and measurements in `docs/research/drums.md`; models in `Source/dsp/DrumModels.h`).

| Engine | Machine it follows | Parts |
|---|---|---|
| **Classic** | the 1980 analog drum machine: bridged-T resonators kicked by a shaped 1 ms pulse (kick with attack Q/frequency jump and level-dependent sigh; snare with two resonators and 2–8 kHz high-passed snappy noise; toms, conga, rim, clave), six measured square oscillators into 3.44 / 7.1 kHz band-passes for hats and cymbal, sawtooth-gated band-passed noise for the clap | all |
| **Punch** | the 1983 analog/PCM drum machine: diode-rounded triangle VCOs with pitch envelopes (kick, toms), the two-VCO snare with a pitch bump and a 24 ms flat-top snappy blast, shift-register noise clap, 6-bit metal recordings for hats and cymbal, a three-mode rim | all |
| **Box** | the compact analog/PCM beat box: resonator voices with click and low-pass, a resonant oscillator-cluster hat, short PCM clap / crash / agogo played at a variable speed | all |
| **Ring** | the resonating-filter kick synth: a self-oscillating 2-pole low-pass bent down from above, pulse, 12 dB tone and multi-stage drive | kick |
| **Bell** | the 1980 cowbell: two squares band-passed, with a fast drop and a long tail | perc |
| **Layer** | the 2-layer DSP percussion synth (below) | all |

- **Layer:** two layers, each with a source (sine, saw, HP noise, LP noise or BP noise), a pitch, a pitch-modulation type (decay, rise or LFO), an amp envelope type (AD, exponential or multi-hit) and its own level. The layers are mixed and drive-able.
- **Per-part controls:** level, pan, delay and reverb sends, velocity sensitivity and choke group (closed and open hat default to choke 1). The engine controls take up to 8 knobs, such as Pitch, Bend, Bend Time, Decay, Tone, Click, Accent and Drive.
- **Kit resonator:** a shared Tube or String waveguide that each part feeds through its own send. It adds the "ringing body" character.
- **Sequencing:** 8 lanes, each step storing velocity, probability, ratchet (1–4) and micro-timing. The lane editor switches between Vel, Prob and Ratchet views.
- **Macros:** Kick Tune, Kick Decay, Snare Tone, Snappy, Hat Decay, Metal, Resonator, Drive.

### 8.5 Minibox Sampler: chromatic sampler (`10_sampler`)

**Concept:** a sample played chromatically with a pitch envelope, a simple filter and amp, and an optional vintage resampler. Up to 8 key zones are supported.

```
 Zone (sample, root key, key range) ─► Resampler (Clean Hermite | Vintage: rate + bits)
   Start / Length / Loop Start / Loop Length / Reverse,  play mode One-shot | Gate | Loop
   Pitch: Tune, Fine, Pitch EG (Int, Attack, Decay), LFO
 ─► SVF (LP | BP | HP) ◄─ Env, Key, LFO
 ─► VCA ◄─ ADSR, velocity ─► Drive ─► Out          Voices 1–8, glide in mono
```

- **Hero waveform:** draggable markers **S** (start), **L** (loop start) and **E** (end), with the loop region shaded and a zone strip showing the key range and root.
- **Character:** *Clean* or *Vintage*, with bits (4–16), rate (4–48 kHz) and drive.
- **Input:** drag and drop WAV, AIFF or FLAC files. In the standalone build you can also **resample the master output** into a slot (future).
- **Macros:** Start, Length, Tune, Cutoff, Pitch EG, Decay, Bits, Reverb.
- **Additions:** a resonant multimode filter instead of a fixed high-cut, ADSR instead of attack/decay, loop points, multiple zones, polyphony and an LFO.

### 8.6 Minibox DrumSampler: 8-pad drum sampler (`07_drumsampler`)

**Concept:** the Sampler's voice applied per pad, without key tracking. It suits one-shots and chopped loops.

- **Per pad:** sample, start, length, pitch (±24 semitones), attack, decay, pitch EG (Int and Decay), Hi-Cut, level, pan, sends, bits and drive, a *One-shot | Gate* play mode, reverse and choke group.
- **Slice:** drop a loop on the editor and slice it to 8 pads, either at transients or into equal parts. The sequencer lanes are then filled to recreate the loop at the project tempo. Time-stretch is out of scope for v1, so the loop is retimed per slice.
- **Sequencing** is the same as Drums: 8 lanes with Vel, Prob and Ratchet views.
- **Macros:** Kick Pitch, Snare Pitch, Hat Decay, Start (all pads), Hi-Cut (all), Bits, Drive, Reverb.

### 8.7 Minibox Bassline: single-oscillator mono synth (`08_mono`)

**Concept:** one oscillator with **simultaneous waveforms through a source mixer**, a 24 dB ladder filter, one envelope and an LFO. It adds Unison, Poly and Chord modes.

```
 Range (16'–2') ─► Osc core ─┬─ Pulse (PW, PWM src: LFO | Env | Manual) ─┐
                             ├─ Saw ───────────────────────────────────┤
                             ├─ Sub (−1 sq | −2 sq | −2 25% pulse) ────┼─► Mixer ─► Ladder4 24 dB ─► VCA ─► FX ─► Out
                             └─ Noise ─────────────────────────────────┘     ▲ Env ±, LFO, Key, Bend    ▲ Env | Gate
 LFO: tri | square | random (S&H) | noise, free or tempo-synced (Sync), fade-in, key-trig  ─► pitch, PWM, cutoff
 Glide: Off | On | Auto (only when legato)
```

- **Editor:** the **Oscillator** card holds what sounds (Saw, Pulse, Sub and Noise levels, the sub type, Range and Tune) and the **Pulse** card shapes the pulse (Width, PWM and its source: LFO, Env or Manual).
- **Envelope:** one ADSR drives the filter, and the amp either follows it (*Env*) or is a plain gate (*Gate*). Decay and Release are times to silence (−80 dB), so a pluck that should ring ≈ 0.7 s needs a Decay of ≈ 1.8 s.
- **Voice:** *Mono*, *Unison* (4 stacked with detune), *Poly* (4 voices) or *Chord* (stores a chord shape that each note triggers).
- **FX:** chorus and delay, then the Reverb card.
- **Families:** Bass, Sub, Lead, Pluck, Wobble (tempo-synced filter, PWM and gate movement in many shapes and divisions), Driven (filter overdrive), Chord Stab, FX.
- **Macros:** Cutoff, Reso, Env, Sub, PWM, Decay, Glide, LFO.

---

## 9. UI architecture

### 9.1 Views
| View | Build | Content |
|---|---|---|
| **Rack** (`00_new_project`, `01_rack`) | standalone | 4 slot strips showing identity, preset, M/S, 8 macros, a device visual, level, pan, Delay and Reverb sends and an open-editor button, plus a master row (Delay, Reverb, Glue, Limit, meter, volume; click the delay or reverb visual for time, feedback, tone, decay and damp). A new project starts empty: no devices and no notes. |
| **Device editor** (`03`–`10`, plugin: `11_plugin`) | both | Header (track pill, track dots, preset browser, save, Compare, `⋯`), signal-flow section cards, and a step strip in standalone or a MIDI activity row in the plugin |
| **Seq** (`02_seq`) | standalone | Bar strip, 4 track rows for the current bar (with the Input marker), and a detail editor for the selected track (piano roll with note-length handles, Velocity/Gate/Locks lanes, accent and slide lanes for Acid) |
| **Play drawer** (`14_play`) | standalone | Over any view, from the top-bar keyboard button: Scale, Chord, Arp and Record cards (§4.5) |
| **Top bar** | both | *Standalone:* brand, Rack/Seq, record/play/stop, BPM −/+/Tap, `4/4 ▾`, metronome, A–D switcher, then the project name and **New, Open, Save, Export** buttons (Ctrl+N/O/S/E) and ⋯ (audio/MIDI, pattern switch, recording, click level, quality, look-ahead, rack presets, Save As). *Plugin:* brand, instrument tabs and the preset browser |

- **Slot device picker:** the `▾` next to the device name opens a list of the 7 devices plus *Empty*. Changing a device in the standalone build only affects the current pattern.
- **Editor layout:** sections are laid out left to right in signal-flow order (source, filter, envelopes, modulation, FX). Each section has at most 6 knobs, one choice pill and at most one visual. This is the mid-density rule from the Pastel family.

### 9.2 Theme tokens
| Token | Hex | Use |
|---|---|---|
| `bg` | #141518 | window background, visual wells |
| `surface` | #20232A | section cards, slot strips |
| `control` | #262930 | knob bodies, pills, empty steps |
| `divider` | #2A2D34 | 1 px lines |
| `text` / `quiet` / `faint` | #ECE8E3 / #A8A49E / #55585E | text levels |
| `track1..4` | #9EB9F3 / #8FD0C2 / #E8B86A / #E9A3B8 | track identity, knob arcs, steps |
| `pattern` | #B9A7F0 | pattern switcher, play ring, playhead |
| `record` | #E88A8A | record only |
| `mute` | #F2B48F | mute state |

Font: Lato (900 for titles, 700 for labels, 400 for values). Corner radii are 12 for cards, 8–10 for buttons and steps, and pills are fully round.

### 9.3 Implementation
- The UI uses JUCE components with a single `MiniboxLookAndFeel`. Knobs, pills and steps are custom-painted, so no bitmaps are needed.
- The editor is vector-scaled from a 1600 × 1000 base in 4 steps (75 %, 100 %, 125 %, 150 %), and the plugin window is resizable on those steps.
- Device editors are built from a declarative `EditorSpec` (sections, knobs, choices, visual type) for each device, so all 8 editors share one renderer. This mirrors `mockups/tools/build.py`.
- Visuals are filter response, envelope, waveform, algorithm graph and sample waveform. They redraw at 30 Hz from a lock-free snapshot of parameter values; they are not computed from audio.

---

## 10. Performance targets
| Item | Target |
|---|---|
| CPU at 48 kHz with 128-sample buffers (one modern core) | full rack (Drums, Acid, Poly 6 voices, FM 8 voices) under **15 %**; a switch with tails under 25 % |
| Latency | zero added latency, except optional look-ahead on the master limiter (reported to the host) |
| Allocations on the audio thread | none (enforced by a debug allocator hook in tests) |
| Pattern switch jitter | 0 samples (sample-accurate) |
| Sample memory | streamed into RAM; 512 MB soft cap per project, with a warning |

---

## 11. Persistence
- **Project** (`.minibox`): a zip containing `project.json` (the model in §3), the device states, and a `samples/` folder with the referenced audio. Missing samples are relinked on load.
- **Device preset** (`.mbx-<device>`): JSON with the device type, version, parameter values and macro map. Factory presets ship per device, tagged with a category chip (Bass, Lead, Pad, Keys, Kit, Vocal …).
- **Rack preset** (`.mbrack`): 4 slot device presets plus the mixer, shared by the plugin and the standalone build ("load into current pattern").
- **Versioning:** each device has a `version` field and a migration table. Parameter IDs are never reused. The retired `va` device id (an early standalone VA device) migrates to the VA device (id `poly`) with the VCO engine selected. Display names changed (Mono → Bassline, Poly → VA); the stable ids `mono` and `poly` keep presets, projects and host state loading.

---

## 12. Code layout (JUCE 8, C++20, CMake)

```
Pastel Minibox/
├── CMakeLists.txt        # MiniboxStandalone, MiniboxPlugin (VST3), MiniboxTests
├── Source/
│   ├── app/              Main (audio/MIDI I/O, window), MainComponent (pages, file actions, computer keyboard)
│   │   └── presets/      PresetBank (factory files, user presets, overwrite/restore), bank_*.py + banks.py (factory content)
│   ├── plugin/           PluginProcessor (14 host parameters, state), PluginEditor (header, editor, MIDI row)
│   ├── engine/           Engine (device pool, audio lock, sub-block rendering, tails, MIDI routing, recording), MasterBus
│   ├── model/            Project (patterns, tracks, steps, lanes, locks, mixer), Serializer (.minibox, .mbrack, WAV export)
│   ├── seq/              Sequencer (clock, swing, micro-timing, ratchets, probability, locks, quantized switching, count-in, click)
│   ├── dsp/              DspCommon, Oscillators, Filters, Envelopes, Effects (§7)
│   ├── devices/          DeviceProcessor (params, locks, macros, voices), DeviceFx, SampleSlot, DeviceFactory
│   │   ├── mono/ poly/ (Arp) acid/ fm/ (Algorithms, bank import) sampler/ drums/ drumsampler/ (slicer)
│   └── ui/               Theme, Paint, Widgets, Visuals, EditorSpecs, EditorView, RackView, SeqView, TopBar, PresetBrowser, LookAndFeel
├── Factory/              presets/ and samples/ (generated from the banks and the engines)
├── Test/                 device, sequencer, preset, project, DSP, allocation, CPU, host and screenshot tests; golden/
└── mockups/
```

Build and test:

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build
./build/MiniboxTests_artefacts/Release/MiniboxTests            # all tests
./build/MiniboxTests_artefacts/Release/MiniboxTests tool_ui_shots   # renders build/shots/*.png to compare with mockups/png
```

---

## 13. Build order
1. **Core:** the model, the engine with the rack and slots, the shared DSP blocks (oscillators, SVF, ADSR, LFO), and the **Mono** device. This is enough to get the plugin build making sound.
2. **Plugin shell:** MIDI channel routing, host parameters and macros, the rack view and a generic editor renderer.
3. **Acid and VA (DCO engine):** the ladder and diode filters, chorus and arpeggiator.
4. **Standalone:** the clock, transport, sequencer, Seq view, recording, and the pattern bank with the **device pool and tail handoff**.
5. **Drums and DrumSampler:** lanes, engines, the kit resonator, slicing.
6. **FM and the Poly VCO engine:** algorithms, bank import, the stacking voice modes, and the per-voice and shared OTA filter.
7. **Sampler:** zones and the vintage resampler.
8. **Polish:** presets, the master bus, scaling and a CPU pass.

## 14. Testing
`MiniboxTests` covers the following:

- **DSP:** SVF cutoff within 5 %, ladder self-oscillation near the cutoff, envelope timing within 1 ms, oscillator tuning.
- **Devices:** every device renders, decays to silence and makes a byte-identical save → load → save round-trip. All 32 FM algorithms render.
- **Sequencer:** note-on sample positions across tempos and swing, time signatures, a pattern switch on the bar line with released notes, drum ratchets and probability, and parameter locks applied and cleared per step.
- **Presets:** counts (128 / 32), forbidden parameters, plain defaults with preset 001 as the start sound, file management, every preset rendered (finite, bounded, audible), and audio distinctness for every pair.
- **Project:** a save/load round-trip including devices, steps, slides, locks, lanes, mixer and samples. New projects are empty. WAV export renders 24-bit stereo.
- **Real time:** no heap allocation on the audio thread during playback and pattern switches (an allocation-counting hook). A full rack uses less than 15 % of one core at 48 kHz with 128-sample buffers.
- **Regression:** an offline render of a fixed demo is compared with `Test/golden/demo_render.wav` (the difference must be below −60 dB).
- **Host:** the built VST3 is loaded through JUCE's VST3 host. It must play MIDI, respond to macro automation, round-trip its state and open its editor. Checks in third-party DAWs are still to be done by hand.
- **UI:** `tool_ui_shots` renders every screen for comparison with the mockups.

---

## 15. Android app (final target)

The end goal is an Android build of the groovebox. Every engine decision above, including C++, a lock-free audio thread, no allocations during playback and a declarative editor spec, is chosen so that the same core runs unchanged on Android.

> Desktop (standalone + VST3) is finalised first; the Android port follows.

### 15.1 Scope
- It is the **standalone groovebox**: 4 tracks, patterns A–D each with its own 4 devices, the sequencer, transport, BPM and time signature. There is no plugin build on Android.
- The app is **portrait only** on phones and tablets (`android:screenOrientation="portrait"`). Tablets use the same layout, centred at a maximum width of 600 dp.
- The mockups are in `mockups/mobile/` and cover 13 screens (see `mockups/SPEC.md`).

### 15.2 Stack
| Layer | Choice |
|---|---|
| Engine and devices | The shared C++ core (`engine/`, `seq/`, `dsp/`, `devices/`, `model/`) built with the NDK through CMake |
| Audio I/O | **Oboe**: AAudio in low-latency exclusive mode with a sustained-performance hint, falling back to OpenSL ES. JUCE's Android audio device wraps Oboe if JUCE is kept. |
| UI | **Option A (recommended): JUCE UI** with a separate mobile layout set. It reuses `LookAndFeel`, the widgets and `EditorSpec`, so there is one codebase. **Option B:** a Kotlin and Jetpack Compose UI over a thin JNI command and snapshot bridge, which feels more native but means a second UI. |
| MIDI | The Android MIDI API (USB and BLE controllers): note input, clock in and out (future) |
| Files | The Storage Access Framework for importing samples and exporting projects and WAVs. Projects live in app-private storage and are shared with the share sheet. |

### 15.3 Mobile UI architecture
- **Navigation:** Rack · Seq · Mix · Project in the bottom navigation. Device editors are pushed on top of Rack. Patterns and Tempo are modal bottom sheets.
- **App bar:** always shows record, play, stop, BPM with time signature, and A–D. Playing music should never be more than one tap away.
- **Editors:** each desktop section becomes a **tab**, showing one section at a time with its visual, chips and up to 6 knobs. The `EditorSpec` from §9.3 is reused, and only the layout changes (`DesktopLayout` lays sections out in rows, `PhoneLayout` puts them in tabs).
- **Rack:** shows 4 macros per card, with a swipe to reach macros 5–8.
- **Sequencer:** the selected track's current bar is shown as a 4×4 step grid. Drum tracks show lanes for the selected part, and tapping a pad selects the lane. A per-step editor (velocity, gate, probability, ratchet, locks) opens on a long-press.
- **Play surface:** synths use in-scale white keys, with Scale and Chromatic modes. Drum kits use 8 pads with Play, Select and Mute modes, plus Roll for note repeat.
- **Gestures:** drag a knob vertically, or long-press and drag for fine control. Double-tap resets to the default. Hold a step and turn a knob to write a parameter lock. Haptic ticks confirm step toggles and detents.

### 15.4 Audio and performance on phones
| Topic | Rule |
|---|---|
| Buffer | Start at the device burst size × 2 and adapt on xruns (watching the underrun counter). The target round-trip is under 20 ms on Pixel-class devices. |
| CPU tiers | At first launch, a quick benchmark sets a *Quality* tier. *Low* means no oversampling, Poly at 4 voices, FM at 6 voices and chorus in mono. *Normal* means 2× oversampling on the Acid and Mono filters only. |
| Threads | Request a real-time audio thread and pin it to a big core where possible. The UI repaints at 60 Hz or less, and the visuals at 30 Hz. |
| Thermal | Listen to `PowerManager` thermal status. Above *moderate*, drop the quality tier and show a small badge. |
| Background | A foreground service with a media-style notification (play/stop) keeps the music running when the app is backgrounded. Audio focus is requested and the app ducks or pauses when it loses focus. |
| Lifecycle | Save the project snapshot on `onPause`. The engine survives configuration changes. If the audio device is lost (headphones unplugged, Bluetooth), restart the stream and keep the transport position. |
| Bluetooth | Warn about latency on A2DP outputs and offer a latency offset in Settings. |

### 15.5 Build order addition
9. **Android:** bring up the Oboe audio path with the Bassline device, then the phone layouts (Rack, Seq, sheets), then the rest of the devices with the quality tiers. Profile on a low-end device (4 cores, 3 GB RAM) before adding polish.
