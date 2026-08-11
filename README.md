# TrackDeck (JUCE VST3 plugin)

A JUCE plugin that loads several audio files, plays them all in perfect
sample-sync, and exposes **one mono plugin output bus per physical output**
(32 by default), with each loaded file's channel(s) independently routable
to any of them — so a stereo file can feed two separate physical outputs and
a mono file feeds just one, all selectable and changeable from the UI.

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
| Per-track volume, saved with the rest of the settings | Each `TrackRow` has a `juce::Slider` spanning the full width of the right-hand column, with mute/output/delete stacked below it. `FilePlayer::fillBlock()` applies the gain every block (0.0-1.0 linear), independent of mute, so unmuting always reflects the current fader position. The value is included in `buildStateTree()`/`applyStateTree()`, so it's saved and restored exactly like file paths, mute state, and output routing — through both the host project mechanism and the on-disk persisted-settings file |
| Timeline + waveform per track | Each `TrackRow` renders its file's waveform via `juce::AudioThumbnail`; all rows share one time-to-pixel scale (based on the longest loaded file) and are stacked with no gaps, so their individual playhead lines visually merge into a single timeline cursor spanning every track |
| Click-anywhere-to-seek | `TrackRow::mouseDown`/`mouseDrag` → `handleSeek()` converts the click's x position into a time and calls `TrackDeckAudioProcessor::seekTo()`, which repositions every loaded slot immediately — works whether stopped or already playing |
| Large, low-light-friendly transport | Three `juce::ShapeButton`s (Play/Pause/Stop) at 100x100px with bold icon shapes and saturated colours (green/amber/red), no border — the Play button's fill brightens while playing instead — plus a large (56pt) bold digital time readout |
| Resizable window, waveform stretches to fit | `setResizable()`/`setResizeLimits()` in the editor constructor; each `TrackRow`'s waveform area (`getWaveArea()`) is computed as the row's full width minus fixed-pixel margins for the track-number label (left) and mute/remove/output controls (right), so only the waveform grows or shrinks as the window is resized horizontally |
| Settings remembered across DAW restarts, not just within a saved project | Every load/remove/mute/volume/routing change calls `TrackDeckAudioProcessor::savePersistedSettings()`, which writes loaded file paths, mute flags, volume, and output routing to a small XML file outside any DAW project (see "Persistent settings" below). The constructor calls `loadPersistedSettings()` to auto-restore that file, so even a freshly-inserted instance in a brand-new project remembers what was loaded last |

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
  Source/
    FilePlayer.h         # decodes + resamples one file, handles mute/position
    Theme.h               # ALL colours/fonts/spacing constants - edit this to restyle the UI
    PluginProcessor.h/.cpp
    PluginEditor.h/.cpp
  JUCE/                   # you add this (see Build steps)
```

### Restyling the UI

Every colour, font size, and layout spacing constant the editor uses lives in
`Source/Theme.h`, grouped into namespaces by the UI element they affect
(`Theme::Window`, `Theme::TrackList`, `Theme::TrackRow`, `Theme::Waveform`,
`Theme::MuteButton`, `Theme::DeleteButton`, `Theme::ChannelDropdown`,
`Theme::AddTrackButton`, `Theme::Transport`, `Theme::HelpText`). To change how
the plugin looks — recolour a button, adjust the gap between tracks, swap the
waveform colour — edit the relevant value in that one file and rebuild; you
never need to touch `PluginEditor.cpp` for a pure styling change. See the
comments at the top of `Theme.h` for the colour-format convention used.

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
   - **Standalone app** (a self-contained window, no DAW needed):
     `build/TrackDeck_artefacts/Release/Standalone/TrackDeck.exe` (Windows) /
     `.app` (macOS) / no extension (Linux)

### Installing the VST3 so your DAW can find it

Copy (or symlink) the whole `TrackDeck.vst3` **bundle/folder**
(don't cherry-pick files out of it) into your system's VST3 folder:

| OS | VST3 folder |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3` |
| macOS | `~/Library/Audio/Plug-Ins/VST3` (per-user) or `/Library/Audio/Plug-Ins/VST3` (all users) |
| Linux | `~/.vst3` (per-user) or `/usr/lib/vst3` (all users) |

Then open your DAW and trigger a plugin rescan (most DAWs do this
automatically on startup, or have a "Rescan plugins" option in preferences).
It should show up as **"TrackDeck"**.

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

## ASIO support on Windows

**Do you actually need this?** If you're using the plugin as a **VST3
inside a DAW** (Cubase, Reaper, Ableton, Studio One, Bitwig, etc.), the
answer is almost certainly **no** — the DAW itself owns the audio device and
its own ASIO driver connection; the plugin just sends/receives audio inside
the host's own audio engine and has no say in what driver the host uses.

This section only matters if you run the **Standalone** app (built alongside
the VST3) directly, without a host, and you want its own audio settings
dialog to be able to open an ASIO driver — useful for lower latency than
Windows' default DirectSound/WASAPI, or because your interface only ships
an ASIO driver.

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
4. **Re-run CMake's configure step** (from the Developer Command Prompt),
   pointing it at the SDK and turning the option on:
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

### Turning ASIO back off

Just reconfigure without the option (or with it explicitly `OFF`):
```
cmake -B build -DTRACKDECK_ENABLE_ASIO=OFF
cmake --build build --config Release
```

## Troubleshooting

- **`add_subdirectory(JUCE)` / "JUCE does not exist" error from CMake** — you
  skipped or the `git clone` step failed. Make sure there's a `JUCE` folder
  directly inside `TrackDeck/` containing JUCE's own
  `CMakeLists.txt`.
- **Windows: "cl.exe not found" or similar compiler errors** — you're using a
  plain Command Prompt/PowerShell instead of the "Developer Command Prompt
  for VS 2022" / "x64 Native Tools Command Prompt for VS 2022", or the
  "Desktop development with C++" workload wasn't installed in Visual Studio.
- **Linux: CMake errors about missing X11/ALSA/WebKit, etc.** — you're
  missing one of the `apt install` packages listed above; the error message
  usually names the missing library, which maps closely to the package name.
- **DAW doesn't see the plugin after copying the `.vst3`** — double-check you
  copied the whole `.vst3` bundle (it's actually a folder on Windows/Linux,
  and a macOS "package" that looks like a single file in Finder but is really
  a folder), that it's in the right VST3 folder for your OS, and that you
  triggered a rescan in the DAW's plugin manager.
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

## Persistent settings

Loaded files, mute state, volume, and output routing are remembered two ways at once:

1. **Host project state** — the normal `getStateInformation()` /
   `setStateInformation()` mechanism, same as any plugin. Whatever you save
   in your DAW project comes back when you reopen that project.
2. **Standalone of any project** — every load, remove, mute toggle, or output
   routing change also writes the same information to a small XML file
   outside any DAW project:

   | OS | Settings file location |
   |---|---|
   | Windows | `%APPDATA%\TrackDeck\last_session.xml` |
   | macOS | `~/Library/Application Support/TrackDeck/last_session.xml` |
   | Linux | `~/.config/TrackDeck/last_session.xml` (or wherever your distro maps JUCE's `userApplicationDataDirectory`) |

   A brand-new plugin instance loads that file automatically as it's
   constructed (`loadPersistedSettings()`), before the host gets a chance to
   apply any project-specific state — so even inserting a *fresh* copy of the
   plugin, in a DAW that was fully closed and reopened, with no project saved
   at all, comes back with whatever was loaded last. If the host *does* then
   restore its own saved project state, that takes over as expected (and is
   itself written back out to the same file, keeping the two in sync).

   Because this file lives outside your DAW project, it's shared across every
   project/session you open the plugin in — loading a file in one project and
   later opening a different (or blank) project will show that same
   last-used setup until you change it. Delete the file (or its parent
   folder) to reset the plugin back to a blank slate.

## Notes & caveats

- MP3 decoding uses JUCE's built-in `MP3AudioFormat` (decode-only, no
  encoding) — no extra third-party dependency needed.
- Files with a native sample rate different from the host's are resampled in
  real time via `ResamplingAudioSource`; very large sample-rate mismatches
  may cost more CPU per voice.
- Loaded file paths, mute flags, volume, and output routing are saved/restored
  via **both** the host project mechanism and the standalone settings file
  described above — see "Persistent settings". Either way, restoring depends
  on the audio files still being at the same paths; if a saved output
  routing can't be restored cleanly (e.g. a file was swapped for one with a
  different channel count), that slot silently falls back to the automatic
  defaults.
- `maxTracks` (16, in `PluginProcessor.h`) is how many files can be loaded at
  once; `maxOutputChannels` (32) is how many physical mono outputs the
  plugin exposes. Raise either if you need more — just be aware very high
  counts can be awkward for some hosts to display/route, and every combo box
  in the UI lists all `maxOutputChannels` options, so a much larger number
  will make those dropdowns unwieldy.
