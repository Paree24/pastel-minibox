# DAWproject export

⋯ ▸ **Export the project as DAWproject...** (Ctrl+Alt+E) writes the whole project as a `.dawproject` file, the open
exchange format that Bitwig Studio, Studio One, Cubase and Cubasis open. Every track arrives with the **Minibox VST3
plugin** on it, loaded with that track's instrument, preset and mix, so the project plays the same in the other DAW.
The Minibox plugin has to be installed there.

## What other apps' exports carry, and what Minibox writes

| DAWproject feature | Minibox |
|---|---|
| Tempo, time signature | the project's |
| Tracks with a plugin and its full state (`.vstpreset`) | a track per instrument: the plugin with the instrument, its preset name and parameters, macros, level, pan, delay / reverb sends and the master delay / reverb settings |
| Track mute / solo, volume, pan | mute and solo on the DAW channel; level and pan stay in the plugin, so the DAW faders start at unity |
| Note clips on the timeline | the patterns that hold notes, one after another (A, then B …), a marker naming each |
| Clip launcher scenes | a scene per pattern, a looping clip per track |
| Notes | as they play: step offsets, micro-timing, ratchets and swing baked in; muted notes left out |
| Note expressions in clips | pitch bend, mod wheel (CC 1), expression (CC 11) and channel pressure lanes |
| Plugin parameter automation | the macro lanes, on the plugin's Macro 1–8 parameters (each pattern starts from the instrument's own value) |
| Metadata | the project's name |

A Minibox track whose instrument differs between patterns becomes one DAW track per instrument, named with its
patterns, for example `1 Acid - Classic Line (A)` and `1 Acid - Other (B)`.

### How the sequencer's articulations travel

* **Accent** (acid line and drums): the note plays at full velocity, which both instruments read as accent.
* **Slide / tie** (acid line): the note overlaps the next one, and the Slide keyswitch (D0) is held over the next note's
  start, so the plugin slides exactly as the groovebox does. These D0 notes are visible in the DAW's clip.

### Not carried

* Step probability and trig conditions: every step plays.
* Per-step parameter locks (motion recorded into a step sequence): the plugin exposes only its macros to the host.
  Record on a piano-roll track (macro lanes) to keep the motion.
* Pattern loop ranges, performance mode, chord / arp settings (they shape what you play, not what is stored).
* Samples are referenced by their file path (factory samples ship with the plugin), not copied into the file.
* The bus compressor of the groovebox's master (each plugin instance has its own limiter).

## File layout

```
project.xml        tracks, devices, timeline, scenes (validates against the format's Project.xsd)
metadata.xml       title and comment
plugins/<id>.vstpreset   the Minibox plugin's state per track (VST3 preset: the "Comp" chunk is the plugin's own state)
```

The plugin's class ID is `ABCDEF019182FAEB5073746C4D6E6278`; macro parameter IDs are the ones the plugin reports to hosts.
Tests: `dawproject_export` (content, each preset opened by the plugin), `tool_dawproject_host` (each preset loaded into the
built VST3 through the host's preset loader, which checks the class ID; parameter IDs compared with the host's).
