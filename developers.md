# TrackDeck — Developer Guide

This document covers building TrackDeck from source, its internal
architecture, and how to customize it. For a friendly overview of what
TrackDeck does and how to use it, see [README.md](README.md).

TrackDeck is a JUCE plugin that loads several audio files, plays them all in
perfect sample-sync, and exposes **one mono plugin output bus per physical
output** (32 by default), with each loaded file's channel(s) independently
routable to any of them — so a stereo file can feed two separate physical
outputs and a mono file feeds just one, all selectable and changeable from
the UI.

It builds as **VST3** (Windows/macOS/Linux), **AU** (macOS only), **CLAP**
(Windows/macOS/Linux, via the vendored `clap-juce-extensions` library — see
"CLAP support" below), and a **Standalone** app, all from the same
`CMakeLists.txt` and the same source. Nothing in `Source/` is aware of which
format it's running as; format support is entirely a build/packaging
concern, handled in `CMakeLists.txt`.

## Feature checklist

| Requirement | Where it's implemented |
|---|---|
| Select 1+ audio files | `PluginEditor::chooseAndLoadFiles()` — multi-select `FileChooser`, via the "+ Add Track" button below the track list |
| Only loaded tracks shown; 16-track limit with a friendly error | `TrackDeckAudioProcessorEditor::getNumRows()` rebuilds a `visibleSlots` list of just the currently-loaded slots each time, so empty slots take up no space in the UI; the track number shown is each track's position in that visible list (1, 2, 3...), not its underlying slot index. `chooseAndLoadFiles()` stops loading once all 16 slots are full and shows "Sorry, only 16 tracks can be loaded." via `AlertWindow::showMessageBoxAsync()`; the Add Track button also disables itself once the limit is reached |
| WAV / MP3 / FLAC support | `AudioFormatManager::registerBasicFormats()` in `PluginProcessor`, with `JUCE_USE_FLAC=1` and `JUCE_USE_MP3AUDIOFORMAT=1` set in `CMakeLists.txt` |
| Synchronous play | `PluginProcessor::startPlayback()` sets every loaded file's read position to the same shared transport time, then all slots are pulled in the *same* `processBlock()` call each cycle, so they stay sample-locked |
| Stereo → 2 outputs, mono → 1 output, by default | The plugin declares `maxOutputChannels` (32) **mono** output buses at construction (`buildBusesProperties()`). When a file loads, `assignDefaultOutputs()` gives it the next free consecutive block of output buses matching its channel count — 2 for stereo, 1 for mono |
| Output(s) selectable per file in the UI | Each `TrackRow` shows a single "Output" combo box: for a mono file it lists individual outputs (1, 2, 3...); for a stereo file it lists adjacent pairs (1+2, 3+4, ...), since stereo channels are always kept together. Changing it calls `TrackDeckAudioProcessor::setSlotOutputChannel()` (mono) or `setSlotOutputPair()` (stereo), which re-routes immediately, live |
| Mute / remove per file | `TrackRow` in `PluginEditor.cpp` — mute toggle calls `setSlotMuted()`, remove button calls `removeSlot()` |
| Per-track volume | Each `TrackRow` has a `juce::Slider` spanning the full width of the right-hand column, with mute/output/delete stacked below it. `FilePlayer::fillBlock()` applies the gain every block (0.0-1.0 linear), independent of mute, so unmuting always reflects the current fader position. The value is included in `buildStateTree()`, so it's saved/restored via both the host project mechanism and manual `.tracks` files (see below) exactly like file paths, mute state, and output routing |
| Timeline + waveform per track | Each `TrackRow` renders its file's waveform via `juce::AudioThumbnail`; all rows share one time-to-pixel scale (based on the longest loaded file) and are stacked with no gaps, so their individual playhead lines visually merge into a single timeline cursor spanning every track |
| Click-anywhere-to-seek | `TrackRow::mouseDown`/`mouseDrag` → `handleSeek()` converts the click's x position into a time and calls `TrackDeckAudioProcessor::seekTo()`, which repositions every loaded slot immediately — works whether stopped or already playing |
| Large, low-light-friendly transport | Three `juce::ShapeButton`s (Play/Pause/Stop) at 100x100px with bold icon shapes and saturated colours (green/amber/red), no border — the Play button's fill brightens while playing instead — plus a large (56pt) bold digital time readout |
| Resizable window, waveform stretches to fit | `setResizable()`/`setResizeLimits()` in the editor constructor; each `TrackRow`'s waveform area (`getWaveArea()`) is computed as the row's full width minus fixed-pixel margins for the track-number label (left) and mute/remove/output controls (right), so only the waveform grows or shrinks as the window is resized horizontally |
| Every new instance starts blank | No auto-restore of any kind happens in the constructor — `players` is populated with empty `FilePlayer`s and nothing else. See "Track configuration files" below for how state is handled instead |
| Manual save/load of the whole track set to/from a `.tracks` (YAML) file | `TrackDeckAudioProcessor::saveTracksToFile()`/`loadTracksFromFile()`, wired to the editor's Save/Load buttons (`PluginEditor::saveTracksToFile()`/`loadTracksFromFile()`). Entirely separate from `getStateInformation()`/`setStateInformation()` — see "Track configuration files" below |

### Play / Pause / Stop semantics

- **Play** — starts every loaded file from the current shared position.
- **Pause** — stops audio but leaves the position where it was; Play resumes from there.
- **Stop** — stops audio and rewinds the shared position to `00:00.0`.
- **Seeking** (tap/click/drag on any track's waveform) works at any time, including mid-playback, and immediately repositions all tracks together.

### Output routing

- Each of the plugin's 32 outputs is its own mono bus, so it can be mapped
  to any single physical output in your DAW's routing matrix.
- **Defaults are automatic**: load a stereo file first and it claims the
  adjacent pair of outputs 1+2. Load a mono file next and it claims output 3
  (mono files can use any free single output). Load another stereo file and
  it skips past output 3 (already taken) to claim the next free *even-aligned*
  pair, 5+6 — stereo files only ever land on adjacent pairs (1+2, 3+4, 5+6,
  ...), never split across arbitrary outputs. Removing a file frees its
  outputs for the next file's defaults to reuse.
- **Overriding is manual and immediate**: each track row has a single
  "Output" dropdown. For a mono file it lists individual outputs (1, 2,
  3...); for a stereo file it lists adjacent pairs (1+2, 3+4, ...) — stereo
  files can't be routed to non-adjacent outputs, since that's rarely wanted
  and it keeps the control simple. Changes take effect on the next audio
  block, including mid-playback, with no need to stop first.
- Multiple files (or channels) can deliberately share the same output — the
  plugin mixes (sums) any signals routed to the same bus rather than one
  silently overriding another.
- Files with more than 2 channels only use their first 2 channels (routed as
  if the file were stereo).

## Project layout

```
TrackDeck/
  CMakeLists.txt
  LICENSE
  Source/
    FilePlayer.h         # decodes + resamples one file, handles mute/position
    Theme.h               # ALL colours/fonts/spacing constants - edit this to restyle the UI
    PluginProcessor.h/.cpp
    PluginEditor.h/.cpp
  libs/
    clap-juce-extensions/ # git submodule - see "CLAP support" below
  media/                  # README screenshots
  JUCE/                   # you add this (see Build steps)
```

### Restyling the UI

Every colour, font size, and layout spacing constant the editor uses lives in
`Source/Theme.h`, grouped into namespaces by the UI element they affect
(`Theme::Window`, `Theme::TrackList`, `Theme::TrackRow`, `Theme::Waveform`,
`Theme::VolumeSlider`, `Theme::MuteButton`, `Theme::DeleteButton`,
`Theme::ChannelDropdown`, `Theme::AddTrackButton`, `Theme::Transport`,
`Theme::HelpText`). To change how the plugin looks — recolour a button,
adjust the gap between tracks, swap the waveform colour — edit the relevant
value in that one file and rebuild; you never need to touch
`PluginEditor.cpp` for a pure styling change. See the comments at the top of
`Theme.h` for the colour-format convention used.

## Prerequisites

You need three things before you touch this project: **Git**, a **C++ compiler
toolchain**, and **CMake**. JUCE itself is not "installed" like a normal
library — you just clone its source code into this folder and CMake builds it
together with the plugin. You do **not** need a JUCE account, installer, or
license purchase to build a VST3 for your own use.

Pick your OS below and install everything listed before moving on to "Build
steps".

### Windows

1. Install **Visual Studio 2022 Community** (free): https://visualstudio.microsoft.com/downloads/
   - In the installer, tick the **"Desktop development with C++"** workload.
     This gives you the MSVC compiler, which JUCE needs on Windows.
2. Install **Git for Windows**: https://git-scm.com/download/win
   - Default options are fine.
3. Install **CMake**: https://cmake.org/download/
   - Use the Windows `x64 Installer`. During install, choose **"Add CMake to
     the system PATH for all users"**.
4. Open **"Developer Command Prompt for VS 2022"** or **"x64 Native Tools
   Command Prompt for VS 2022"** from the Start menu for all the commands
   below (a normal Command Prompt/PowerShell window may not find the
   compiler).

### macOS

1. Install the Xcode command line tools (gives you `clang`, `git`, `make`):
   ```
   xcode-select --install
   ```
   If you plan to open the project in Xcode itself, install full **Xcode**
   from the App Store instead, then still run the command above once.
2. Install **Homebrew** (if you don't already have it): https://brew.sh
3. Install CMake:
   ```
   brew install cmake
   ```
4. Use the built-in **Terminal** app for all the commands below.

### Linux (Debian / Ubuntu)

1. Install the compiler, Git, and CMake:
   ```
   sudo apt update
   sudo apt install build-essential git cmake pkg-config
   ```
2. Install the extra development libraries JUCE needs to build GUI apps and
   talk to audio devices on Linux:
   ```
   sudo apt install libasound2-dev libjack-jackd2-dev \
       ladspa-sdk libcurl4-openssl-dev \
       libfreetype6-dev libfontconfig1-dev \
       libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
       libxinerama-dev libxrandr-dev libxrender-dev \
       libwebkit2gtk-4.1-dev libglu1-mesa-dev mesa-common-dev
   ```
   (Package names are for recent Ubuntu/Debian releases. On other
   distributions look up the equivalent package names for these libraries —
   e.g. on Fedora these are typically named `alsa-lib-devel`,
   `webkit2gtk4.1-devel`, etc. If `libwebkit2gtk-4.1-dev` isn't found, try
   `libwebkit2gtk-4.0-dev`.)

## Build steps

These are the same on every OS once your prerequisites are installed — run
them from a terminal (Windows: the Developer Command Prompt from step 4
above).

1. Get the plugin source (skip this if you already have the
   `TrackDeck` folder from wherever you downloaded/extracted it),
   then move into it:
   ```
   cd TrackDeck
   ```
   If you cloned this from git rather than extracting a zip, and
   `libs/clap-juce-extensions` looks empty, you need to pull in its
   submodules too (it depends on its own nested libraries for CLAP
   support):
   ```
   git submodule update --init --recursive
   ```
   (Or clone the whole repo with `git clone --recurse-submodules ...` in
   the first place, which pulls everything in one step. A zip download
   normally already has these files included, so this step is git-only.)
2. Clone JUCE into a subfolder called `JUCE` right next to `CMakeLists.txt`.
   This downloads the JUCE framework source code — nothing needs to be
   "installed" separately:
   ```
   git clone --branch 7.0.12 --depth 1 https://github.com/juce-framework/JUCE.git
   ```
   This creates network traffic of a few hundred MB and may take a few
   minutes. When it's done you should have a `TrackDeck/JUCE/`
   folder full of JUCE's own source code.
3. Ask CMake to generate build files (this step also compiles JUCE's own
   helper tools, so it can take a minute or two the first time):
   ```
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   ```
   **On Windows, if you're planning to run the Standalone app** (rather than
   only using the VST3 inside a DAW), stop here and read the "ASIO support
   on Windows" section below first — you'll want to configure with ASIO
   enabled from the start rather than reconfiguring later. The plain command
   above builds without ASIO, which is fine for VST3-only use.
4. Compile the plugin:
   ```
   cmake --build build --config Release
   ```
   This is the step that actually compiles all the C++ code. On a modern
   laptop expect roughly 2-10 minutes the first time (JUCE itself has to be
   compiled too); it's much faster on subsequent builds since only changed
   files are recompiled.
5. If the build succeeds, you'll find the finished files here:
   - **VST3 plugin**: `build/TrackDeck_artefacts/Release/VST3/TrackDeck.vst3`
   - **AU plugin** (macOS only): `build/TrackDeck_artefacts/Release/AU/TrackDeck.component`
   - **CLAP plugin**: `build/TrackDeck_artefacts/Release/CLAP/TrackDeck.clap`
   - **Standalone app** (a self-contained window, no DAW needed):
     `build/TrackDeck_artefacts/Release/Standalone/TrackDeck.exe` (Windows) /
     `.app` (macOS) / no extension (Linux)

   AU only gets built on macOS — `juce_add_plugin`'s `FORMATS AU VST3
   Standalone` in `CMakeLists.txt` automatically skips it on Windows/Linux,
   so you won't see an `AU` folder there, and that's expected rather than
   a build failure.

### Installing VST3 / AU / CLAP so your DAW can find them

Copy (or symlink) the whole plugin **bundle/folder** (don't cherry-pick
files out of it) into the matching system folder for that format:

| Format | OS | Folder |
|---|---|---|
| VST3 | Windows | `C:\Program Files\Common Files\VST3` |
| VST3 | macOS | `~/Library/Audio/Plug-Ins/VST3` (per-user) or `/Library/Audio/Plug-Ins/VST3` (all users) |
| VST3 | Linux | `~/.vst3` (per-user) or `/usr/lib/vst3` (all users) |
| AU | macOS | `~/Library/Audio/Plug-Ins/Components` (per-user) or `/Library/Audio/Plug-Ins/Components` (all users) |
| CLAP | Windows | `C:\Program Files\Common Files\CLAP` |
| CLAP | macOS | `~/Library/Audio/Plug-Ins/CLAP` (per-user) or `/Library/Audio/Plug-Ins/CLAP` (all users) |
| CLAP | Linux | `~/.clap` (per-user) or `/usr/lib/clap` (all users) |

Then open your DAW and trigger a plugin rescan (most DAWs do this
automatically on startup, or have a "Rescan plugins" option in preferences).
It should show up as **"TrackDeck"** regardless of format.

AU has one extra wrinkle: macOS caches AU plugin info more aggressively than
VST3/CLAP, so if a rebuilt AU doesn't show up (or a host still shows an old
version), quit the DAW, run `killall -9 AudioComponentRegistrar` in Terminal,
then relaunch the DAW to force it to rescan.

### Quickest way to test it: the Standalone app

You don't need a DAW at all to try the plugin out. Just run the Standalone
build from step 5 above by double-clicking it (macOS/Windows) or running
`./"TrackDeck"` from the build folder (Linux). It opens a window
with its own audio device settings (Options button/menu) where you can pick
your sound card and test everything — file loading, sync playback, seeking,
mute/remove — before ever touching a host.

### Rebuilding after making code changes

You don't need to repeat the JUCE clone. Just re-run the build step:
```
cmake --build build --config Release
```

## CLAP support

CLAP support comes from [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions),
vendored as a git submodule in `libs/clap-juce-extensions` (which itself
pulls in `clap` and `clap-helpers` as its own nested submodules — see "Build
steps" above for the `git submodule update --init --recursive` step if
you're missing these). It's the standard, widely-used way to add CLAP output
to an existing JUCE plugin without changing any of the plugin's own source
code.

### How it's wired into CMakeLists.txt

Two things make the CLAP build work, both in `CMakeLists.txt`:

```cmake
add_subdirectory(JUCE)
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)
```
pulls in the library's own CMake build (as a dependency-only subdirectory —
`EXCLUDE_FROM_ALL` keeps it out of the default "build everything" target set
so it doesn't add its own unrelated build products), and, right after the
`juce_add_plugin(TrackDeck ...)` block:

```cmake
clap_juce_extensions_plugin(
    TARGET                      TrackDeck
    CLAP_ID                     "com.christaylor.trackdeck"
    CLAP_FEATURES               audio-effect utility
    CLAP_DESCRIPTION            "A simple audio plugin for playing multiple audio files, for example synced backing track stems"
)
```
adds a CLAP target that wraps the *same* `TrackDeckAudioProcessor`/
`TrackDeckAudioProcessorEditor` used by every other format — this is a
build-time addition only, not a code change, which is why `Source/` has no
CLAP-specific code anywhere. `CLAP_ID` should stay a reverse-DNS-style
identifier unique to this plugin if you fork/rename the project.

### If you don't want CLAP support

Since it's a whole extra vendored dependency, you may not want it in a
fork/derivative that doesn't need CLAP. Remove the `add_subdirectory(...)`
and `clap_juce_extensions_plugin(...)` block from `CMakeLists.txt`, drop
`libs/clap-juce-extensions` from the repo, and change `FORMATS AU VST3
Standalone` back to just the formats you want. VST3, AU, and Standalone have
no dependency on CLAP or on `libs/` at all.

### Testing a CLAP build

Popular hosts with CLAP support at the time of writing include Bitwig
Studio, REAPER, and FL Studio — any of them will do for a quick check. There
isn't a CLAP standalone runner analogous to JUCE's own Standalone app, so
testing a CLAP build specifically means loading it in one of these.

## ASIO support on Windows

**Required if you're running the Standalone app on Windows.** The default
Windows audio backends (DirectSound/WASAPI) are fine for a quick check, but
for actual use — reliable, low-latency playback — ASIO is what you want, and
for many audio interfaces it's the only driver they properly support at all.
Treat this as a required step if the Standalone app is how you're going to
run TrackDeck on Windows, not an optional extra.

**If you only use the VST3 inside a DAW**, you can skip this whole section —
the DAW itself owns the audio device and its own ASIO driver connection; the
plugin just sends/receives audio inside the host's own audio engine and has
no say in what driver the host uses.

### Why an extra step is needed at all

Steinberg owns ASIO and requires anyone who builds ASIO support into their
own software to download the ASIO SDK directly from Steinberg and agree to
its license, which **forbids redistributing the SDK itself**. That's why it
can't just be bundled in this project — you download it once yourself, and
CMake picks it up from wherever you put it. This is a one-time setup step;
after it's done, every future build just works.

### Step-by-step

1. **Download the ASIO SDK.** Go to Steinberg's developer site
   (search "Steinberg ASIO SDK download" if this exact page moves):
   https://www.steinberg.net/developers/ — you'll need to accept their
   license terms to download the ZIP. No payment or company affiliation is
   required for the SDK itself.
2. **Extract it** somewhere convenient, e.g. `C:\SDKs\ASIOSDK2.3.3`. After
   extracting, that folder should directly contain subfolders named
   `common`, `host`, `driver`, `pc`, etc. — if you see one more level of
   nesting (e.g. `ASIOSDK2.3.3\ASIOSDK2.3.3\common`), point at the inner
   folder instead.
3. **Do not put the SDK inside the `TrackDeck` project folder** if
   you intend to share/commit this project anywhere — its license doesn't
   allow redistribution. Keep it in a separate location like the `C:\SDKs\`
   example above.
4. **Configure with ASIO turned on**, pointing at the SDK (from the
   Developer Command Prompt) — this replaces the plain `cmake -B build`
   command in "Build steps" above:
   ```
   cmake -B build -DCMAKE_BUILD_TYPE=Release -DTRACKDECK_ENABLE_ASIO=ON -DTRACKDECK_ASIO_SDK_PATH="C:/SDKs/ASIOSDK2.3.3"
   ```
   (Forward slashes work fine for CMake on Windows, even though the rest of
   Windows uses backslashes.)
5. **Build as normal:**
   ```
   cmake --build build --config Release
   ```
6. **Launch the Standalone app**, open its audio settings (the "Options" /
   gear icon near the top of its window), and you should now see **ASIO**
   available as an audio device type alongside DirectSound/WASAPI, with your
   interface's ASIO driver selectable from there.

No source code changes are needed for any of this — audio device handling
for the Standalone app lives entirely inside JUCE itself; this project only
needed the CMake wiring above (see `TRACKDECK_ENABLE_ASIO` /
`TRACKDECK_ASIO_SDK_PATH` near the top of `CMakeLists.txt`) to switch it on
and tell JUCE where to find the SDK headers.

### Disabling ASIO support

ASIO support is opt-in at the CMake level (`TRACKDECK_ENABLE_ASIO` defaults
to `OFF`), specifically so a first-time build doesn't fail for people who
haven't downloaded the SDK yet, and so VST3-only builds never need it at
all. If you've already turned it on and want to go back to the
DirectSound/WASAPI-only build — or you're building on Windows but only ever
plan to use the VST3, and don't want the SDK dependency at all — just
reconfigure without the option (or with it explicitly `OFF`):
```
cmake -B build -DTRACKDECK_ENABLE_ASIO=OFF
cmake --build build --config Release
```
This is also the default if you never pass `-DTRACKDECK_ENABLE_ASIO=ON` in
the first place — the plain `cmake -B build` command in "Build steps" above
builds without ASIO.

## Troubleshooting

- **`add_subdirectory(JUCE)` / "JUCE does not exist" error from CMake** — you
  skipped or the `git clone` step failed. Make sure there's a `JUCE` folder
  directly inside `TrackDeck/` containing JUCE's own
  `CMakeLists.txt`.
- **`add_subdirectory(libs/clap-juce-extensions ...)` fails, or the folder
  looks empty/incomplete** — if you cloned this repo with plain `git clone`
  (not `--recurse-submodules`), the submodule contents won't have been
  pulled in. Run `git submodule update --init --recursive` from the
  `TrackDeck` folder to fetch `clap-juce-extensions` and its own nested
  `clap`/`clap-helpers` submodules, then reconfigure.
- **Windows: "cl.exe not found" or similar compiler errors** — you're using a
  plain Command Prompt/PowerShell instead of the "Developer Command Prompt
  for VS 2022" / "x64 Native Tools Command Prompt for VS 2022", or the
  "Desktop development with C++" workload wasn't installed in Visual Studio.
- **Linux: CMake errors about missing X11/ALSA/WebKit, etc.** — you're
  missing one of the `apt install` packages listed above; the error message
  usually names the missing library, which maps closely to the package name.
- **DAW doesn't see the plugin after copying the `.vst3`/`.component`/`.clap`** —
  double-check you copied the whole bundle (each is actually a folder on
  Windows/Linux, and a macOS "package" that looks like a single file in
  Finder but is really a folder), that it's in the right folder for that
  format and OS, and that you triggered a rescan in the DAW's plugin
  manager. For AU specifically, also try the `killall -9
  AudioComponentRegistrar` step mentioned above — macOS's AU cache is
  usually the culprit if VST3/CLAP show up fine but AU doesn't.
- **MP3 files won't load** — confirm the build picked up
  `JUCE_USE_MP3AUDIOFORMAT=1` (it's set in `CMakeLists.txt`'s
  `target_compile_definitions`); if you changed that file, delete the
  `build` folder and re-run the configure step to be safe.
- **Build is very slow every time** — make sure you're passing
  `--config Release` (or configuring with `-DCMAKE_BUILD_TYPE=Release`);
  Debug builds are much slower to run and sometimes to compile.
- **CMake error: "TRACKDECK_ASIO_SDK_PATH doesn't look like a valid ASIO SDK
  folder"** — you pointed `TRACKDECK_ASIO_SDK_PATH` at the wrong folder
  depth. It needs to be the folder that directly contains a `common`
  subfolder with `iasiodrv.h` inside it — check for an extra nested folder
  from unzipping (e.g. `ASIOSDK2.3.3/ASIOSDK2.3.3/common/...`).
- **Standalone app doesn't show ASIO as an option even after following the
  ASIO steps** — double-check the configure command actually printed
  `ASIO support enabled. SDK: ...` (if it didn't, the option wasn't picked
  up — delete the `build` folder and reconfigure from scratch), and confirm
  you're launching the freshly rebuilt Standalone `.exe`, not an older copy
  from before ASIO was enabled.

## How the multi-output routing works

The plugin has **no audio input** and 32 **mono** output buses, all declared
at construction time (`TrackDeckAudioProcessor::buildBusesProperties()`).
Each loaded file's channel(s) are routed to whichever bus(es) are currently
assigned to them (`FilePlayer::outputAssignment`, set via
`setSlotOutputChannel()` for mono files or `setSlotOutputPair()` for stereo
files), independent of load order or slot index — this is what lets a
stereo file land on outputs 1-2 while a mono file elsewhere lands on output
7, or lets you manually park two files on the same output so they mix
together. Unused buses simply output silence.

In your host:
- Insert the plugin on a track that supports multi-output plugins (most hosts
  treat this like a multi-out instrument/generator, e.g. Cubase, Studio One,
  Reaper, Bitwig). You'll be offered a list of the plugin's 32 output buses
  ("Output 1".."Output 32") to assign to individual tracks/physical outs —
  match these numbers to the same numbers shown in the plugin's own "Output"
  dropdown so what you patch in the host lines up with what you picked in
  the plugin UI.
- Ableton Live requires you to explicitly configure "multi out" when loading
  the device — check Live's documentation for enabling extra output chains.
- Standalone mode (built alongside the VST3) is the simplest way to test
  the plugin without worrying about host multi-bus support at all — the
  JUCE standalone wrapper lets you map each bus to a physical audio device
  channel in its audio settings dialog.

## How sample-accurate sync is achieved

Each loaded file is wrapped in a `FilePlayer`, which owns an
`AudioFormatReaderSource` (decoding) feeding a `ResamplingAudioSource`
(converts the file's native sample rate to the host's rate). When you press
**Play**:

1. `startPlayback()` computes the shared transport position (in seconds) and
   calls `setPosition()` on every loaded slot with that exact same value.
2. `playing` is flipped to `true`.
3. From the next audio callback onward, `processBlock()` calls
   `getNextAudioBlock()` on every slot **once per host callback**, each
   pulling exactly `numSamples` samples. Because every slot is advanced by
   an identical, host-driven block size in lockstep, they never drift apart.

Pressing **Stop** halts playback and rewinds the shared position to zero, so
the next Play press restarts everything together from the beginning.
Pressing **Pause** halts playback without touching the position, so the next
Play press resumes from exactly where it left off. Clicking/tapping anywhere
on a track's waveform (`TrackRow::handleSeek()` → `seekTo()`) repositions
every loaded slot immediately, whether playback is running or stopped.

Volume and mute are applied independently of the sync mechanism:
`FilePlayer::fillBlock()` always pulls the next block from the source (so a
muted track's position keeps advancing right along with everything else),
applies the current gain, and only *then* silences the output if the track
is muted — so unmuting a track never leaves it out of sync with the others.

## High/low-DPI and multi-monitor support

TrackDeck's on-screen size stays visually consistent whether it's opened on
a standard-DPI display, a high-DPI/Retina one, or dragged between two
monitors with different scaling — there shouldn't be a jarring jump in size
(including font size) in any of those cases. This mostly comes from a few
deliberate choices rather than one single mechanism:

- **Every size is a logical pixel, never a physical one.** `Theme.h`,
  `PluginEditor.cpp`'s `resized()`, and the initial `setSize (760, 600)` in
  the constructor all use JUCE's normal DPI-independent sizing units — the
  same units `Font` sizes and `Component` bounds always use. Nothing in this
  codebase reads a physical pixel count or a raw DPI value from the OS and
  multiplies a size by it; that mapping is JUCE's job, not ours.
- **JUCE applies the actual scaling.** It maps those logical sizes to
  physical pixels per-monitor by applying an `AffineTransform::scale()` to
  the whole editor component tree — see `AudioProcessorEditor::setScaleFactor()`,
  which `TrackDeckAudioProcessorEditor` overrides (in `PluginEditor.cpp`)
  purely as a defensive hook that forces a fresh `resized()` + `repaint()`
  after the base class applies the transform. The scaling itself needs no
  code from us.
- **Nothing is a bitmap.** Every icon (Play/Pause/Stop, the waveform's
  peaks, the delete "X") is a vector `juce::Path`, and all text is drawn
  through JUCE's real `Font`/`GlyphArrangement` — there are no raster image
  assets anywhere in this plugin. That means there's nothing that could look
  soft, blurry, or pixelated at a non-standard scale factor the way a fixed
  bitmap icon set would; vector content and real text redraw crisply at
  whatever scale is applied.

### Where this is (and isn't) guaranteed

- **Standalone app**: fully under JUCE's control end to end, so this all
  works automatically — no manifest or extra CMake configuration needed.
- **VST3 in a host**: the host is what tells the plugin its content scale
  factor, via the standard VST3 `IPlugViewContentScaleSupport` mechanism;
  JUCE implements the plugin side of that protocol automatically, so no
  code here is host-specific. Every mainstream, actively-maintained host
  (Cubase, Studio One, Reaper, Bitwig, current Ableton Live, etc.) notifies
  plugins correctly, including when a window is dragged to a
  different-DPI monitor. A minority of older or less-maintained hosts may
  not send that notification on every monitor change — in that specific
  case, Windows/macOS may bitmap-stretch the whole plugin window instead of
  TrackDeck re-laying-out at the new scale, until the plugin window is
  closed and reopened. That's a host-side limitation of the VST3 windowing
  contract, not something a plugin can override — hosts, not plugins, own
  their top-level window's DPI-awareness policy.
- **Linux/X11** DPI detection is generally less consistent across
  distributions/desktop environments than Windows or macOS, since JUCE has
  to infer the display scale from X server settings that aren't always
  configured. If you hit obviously-wrong sizing specifically on Linux,
  that's almost always an X11/desktop-environment scaling configuration
  issue rather than something to fix in this plugin's code.

## Track configuration files (.tracks)

TrackDeck never writes anything to disk on its own. Every new instance —
standalone or freshly inserted in a host — starts with `players` full of
empty `FilePlayer`s and nothing else; there's no auto-restore of any kind in
the constructor. State is handled by two independent mechanisms:

1. **Host project state** — the normal `getStateInformation()` /
   `setStateInformation()` mechanism, exactly like any other plugin.
   Whatever you save in your DAW project comes back when you reopen that
   project. This is completely unaffected by (2) below.
2. **Manual `.tracks` files** — `saveTracksToFile()` / `loadTracksFromFile()`,
   triggered by the editor's **Save** and **Load** buttons. These write/read
   the same information (file paths, mute, volume, output routing) as a
   YAML file anywhere on disk, entirely independent of any DAW project.
   Nothing happens automatically here either — only when the user clicks
   Save or Load.

Both mechanisms share the same underlying `buildStateTree()`/
`applyStateTree()` ValueTree representation; only the serialisation differs
(XML via `juce::ValueTree::createXml()`/`fromXml()` for host state, hand-rolled
YAML for `.tracks` files — see below).

### The `.tracks` file format

A deliberately simple YAML subset — a top-level `tracks:` block sequence,
one entry per loaded track, skipping empty slots entirely:

```yaml
# TrackDeck track configuration - saved by TrackDeck, safe to hand-edit
tracks:
  - path: "C:/Music/backing/kick.wav"
    muted: false
    volume: 1.0000
    outputs: [0, 1]
  - path: "C:/Music/backing/click.wav"
    muted: true
    volume: 0.8000
    outputs: [2]
```

- `path` is quoted (backslashes and quotes escaped) since Windows paths
  contain colons and backslashes.
- `outputs` is a 0-based list — `[0, 1]` means outputs 1+2 in the UI's
  1-based display. A mono file has one entry, a stereo file has two
  (always adjacent, per the pairing model — see "Output routing" above).
- An empty configuration is written as `tracks: []`.

`TrackDeckAudioProcessor::stateTreeToYaml()`/`yamlToStateTree()` implement
this — intentionally **not** a general-purpose YAML parser, just enough to
correctly round-trip exactly what TrackDeck itself writes (plus tolerate
reasonable hand-editing: comments starting with `#`, blank lines, and
consistent 2-space indentation are all fine). It doesn't handle arbitrary
YAML features like multi-line strings, anchors, or flow mappings.

### Loading is a full replace, not a merge

`loadTracksFromFile()` clears every existing slot before applying whatever
the file describes — so loading a `.tracks` file with 3 tracks while 5 are
currently loaded results in exactly those 3, not 8. If the file can't be
read or doesn't contain a recognisable `tracks:` key, nothing is changed and
the call returns `false`; the editor shows a brief "Couldn't load" alert in
that case. A file that parses but references audio that no longer exists at
its saved path will still load the *track* (muted/volume/routing intact)
but simply fail to decode audio for it, the same as any other missing-file
case.

### Notifying the host

Both a manual `.tracks` load and the ordinary per-track actions (mute,
volume, routing, add/remove) call `notifyHostOfStateChange()` →
`AudioProcessor::updateHostDisplay()`, so compliant hosts show their
"project needs saving" indicator. Loading a `.tracks` file does **not**
automatically save the host project for you — if you want the newly-loaded
tracks to persist via the host's own project file too, save the project
separately, same as after any other plugin state change.

## Notes & caveats

- MP3 decoding uses JUCE's built-in `MP3AudioFormat` (decode-only, no
  encoding) — no extra third-party dependency needed.
- Files with a native sample rate different from the host's are resampled in
  real time via `ResamplingAudioSource`; very large sample-rate mismatches
  may cost more CPU per voice.
- Loaded file paths, mute flags, volume, and output routing are saved/restored
  via **both** the host project mechanism and manual `.tracks` files — see
  "Track configuration files". Either way, restoring depends on the audio
  files still being at the same paths; if a saved output routing can't be
  restored cleanly (e.g. a file was swapped for one with a different channel
  count), that slot silently falls back to the automatic defaults.
- Every new plugin instance starts with zero tracks loaded — there is no
  auto-restore of any kind. If you want tracks ready to go the moment you
  open a project, either save that state in the DAW project itself (via the
  host's normal save), or load a `.tracks` file each time.
- `maxTracks` (16, in `PluginProcessor.h`) is how many files can be loaded at
  once; `maxOutputChannels` (32) is how many physical mono outputs the
  plugin exposes. Raise either if you need more — just be aware very high
  counts can be awkward for some hosts to display/route, and every combo box
  in the UI lists all `maxOutputChannels` options, so a much larger number
  will make those dropdowns unwieldy.
