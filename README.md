# Pastel Minibox — Groovebox (Standalone + VST3)

> **Disclaimer:** this project is vibe-coded for personal use. It is provided
> as-is, without warranty of any kind. The author is not responsible for
> anything — Use at your own risk.

Pastel Minibox is a small groovebox built around seven instruments: **Bassline**
(mono synth), **VA** (6-voice polysynth with DCO and VCO engines and an
arpeggiator), **Acid** (bass line synth), **FM** (6-operator, with DX7 bank
import), **Sampler** (chromatic), **Drums** (multi-engine drum synth) and
**Drum Sampler** (8 pads, with a loop slicer). It ships in two builds:

- **Standalone groovebox:** eight tracks and eight patterns (A–H), and every
  pattern owns its own devices. It has a transport with record, BPM, time
  signature and swing, a step sequencer and piano roll (probability, ratchets,
  micro-timing, parameter locks), quantized pattern switching with tails, and
  live or step recording. Master effects and hold-to-play performance effects
  come from Pastel EFX Grid. It also has MIDI learn and controller maps,
  full keyboard control, and export to WAV, MIDI and DAWproject.
- **Minibox VST3 plugin:** one instrument at a time, played from host MIDI
  and synced to the host tempo. It uses the same devices, editors and
  presets, and exposes 14 host parameters (8 macros, level, pan, mute and
  the master returns).

Factory presets and samples are data files (`Factory/`), shipped inside the
VST3 bundle and next to the standalone app. Each device has a preset browser
with categories, and user presets, overwrites and restore.

## License

GPL version 3 — see [LICENSE](LICENSE). This also satisfies the JUCE
framework's licensing terms for this project.

The embedded typeface (Lato) is SIL Open Font License 1.1; see
[Assets/OFL-NOTICE.txt](Assets/OFL-NOTICE.txt). The master and performance
effects are borrowed from Pastel EFX Grid (same author, same license; see
[Source/efx/README.md](Source/efx/README.md)).

## Installing a release (Linux)

Every push to `main` builds on Linux, macOS and Windows and publishes an
automatic release. The Linux package contains the VST3, the standalone app
and an installer:

```sh
tar xzf Pastel-Minibox-Linux.tar.gz
cd Pastel-Minibox-Linux
./install.sh
```

| What       | Where                                                                     |
|------------|---------------------------------------------------------------------------|
| VST3       | `~/.vst3/Minibox.vst3` (factory content inside the bundle)                |
| Standalone | `~/.local/opt/pastel-minibox/`, run it with `pastel-minibox` (`~/.local/bin`) |
| Menu entry | app icon + `~/.local/share/applications/pastel-minibox.desktop`           |

Options: `--vst3-only`, `--standalone-only`, `--uninstall` (removes the
installed files but keeps your presets, sessions and settings). Rescan plugins
in your DAW afterwards.

The macOS and Windows releases are plain zips (the VST3, and the app with its
`Factory` folder). They are built by CI but not tested here. See the notes below.

## Building

Requirements: CMake 3.22+, a C++20 compiler, Ninja (or Make), and Git (JUCE
8.0.6 is fetched automatically). On Linux you also need JUCE's dependencies
(ALSA, X11, freetype, etc.); see the JUCE docs for the full list.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

To build against a local JUCE checkout of the same tag instead of fetching it,
add `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE` to the configure step.

Outputs:

- VST3: `build/MiniboxPlugin_artefacts/Release/VST3/Minibox.vst3` (with the
  factory content under `Contents/Resources/Factory/`)
- Standalone: `build/MiniboxStandalone_artefacts/Release/Pastel Minibox`
  (with `Factory/` next to it)
- Tests: `build/MiniboxTests_artefacts/Release/MiniboxTests` (device,
  sequencer, preset, project, DSP, no-allocation, CPU, host-load and
  keyboard tests; exit 0 = all good)

From a source checkout, `./install.sh` builds and then installs, as described
above. Add `--no-build` to install what is already built.

User presets live in `Documents/Minibox Presets/<Device>/`.

### OS-specific notes

> Only the Linux build has been compiled and tested here. CI also builds and
> tests macOS and Windows. If you hit an OS-specific snag, please file an issue
> with the failing command and its full output.

#### Linux (verified)

1. Install dependencies (Debian/Ubuntu shown; Fedora/Arch: equivalent
   `-devel` packages):
   ```sh
   sudo apt install build-essential cmake ninja-build git pkg-config \
     libasound2-dev libx11-dev libxcomposite-dev libxcursor-dev \
     libxinerama-dev libxrandr-dev libfreetype6-dev libfontconfig1-dev \
     libcurl4-openssl-dev libwebkit2gtk-4.1-dev
   ```
2. Build, then run the tests (must end with `0 failures`):
   ```sh
   ./build/MiniboxTests_artefacts/Release/MiniboxTests
   ```
3. Install with `./install.sh`, or copy `Minibox.vst3` to `~/.vst3/`.

#### macOS (built by CI, not tested here)

1. Install the Xcode command-line tools, then `brew install cmake ninja git`.
2. Same configure and build as Linux. For a universal binary, add
   `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.
3. Copy `Minibox.vst3` to `~/Library/Audio/Plug-Ins/VST3/`. For the app, the
   `Factory` folder goes in `Pastel Minibox.app/Contents/Resources/` (the
   release zip already has it there). These are unsigned personal builds: if
   macOS refuses them, run `xattr -dr com.apple.quarantine <path>` and/or
   `codesign --force --deep -s - <path>`.

#### Windows (built by CI, not tested here)

1. Install Visual Studio 2022 with **Desktop development with C++**, plus
   CMake, Ninja and Git (`winget install Kitware.CMake Ninja-build.Ninja Git.Git`).
2. In an **x64 Native Tools Command Prompt**, run the same configure and build.
3. Copy `Minibox.vst3` to `C:\Program Files\Common Files\VST3\`. Keep the
   app's `Factory` folder next to `Pastel Minibox.exe`.

## Repository layout

- `Source/app/` — standalone app: audio/MIDI I/O, window, pages, file actions;
  `presets/` holds the preset bank and the scripts that generate the factory content
- `Source/plugin/` — VST3 wrapper: host parameters, state, editor
- `Source/engine/` — device pool, sub-block rendering, tails, MIDI routing,
  recording, master bus
- `Source/model/` — project, serializer (`.minibox`, `.mbrack`, WAV), MIDI
  files, DAWproject export
- `Source/seq/` — sequencer, scales, live note input (chords, arp)
- `Source/dsp/`, `Source/devices/` — shared DSP and the seven devices
- `Source/efx/` — the effects borrowed from Pastel EFX Grid
- `Source/ui/` — theme, widgets, rack, sequencer and editor views
- `Factory/` — factory presets, samples and controller maps (data)
- `Test/` — the test suite and golden files
- `ARCHITECTURE.md` — the design guide; `docs/` — feature notes (sequencer,
  MIDI learn, keyboard, master FX, DAWproject)
- `install.sh` — Linux installer (source checkout or release package)
