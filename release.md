# TrackDeck 0.2.1

Thanks for downloading TrackDeck! Grab the file that matches your operating system and plugin format below, then follow the install steps for that format.

# What's changed in this version

- Added the ability to save and load .tracks files, a simple YAML format containing configuration for the player

# Downloading and installing the plugin

Every download is a `.zip` (VST3 downloads end in `.vst3` but are still a zip archive) — extract it first, then move the extracted plugin into the folder listed for your OS and format.

## VST3

- **Windows**: Unzip, then copy the `TrackDeck.vst3` folder into `C:\Program Files\Common Files\VST3`
- **macOS**: Unzip, then copy `TrackDeck.vst3` into `/Library/Audio/Plug-Ins/VST3` (all users) or `~/Library/Audio/Plug-Ins/VST3` (just you)
- **Linux**: Unzip, then copy `TrackDeck.vst3` into `/usr/lib/vst3` (all users) or `~/.vst3` (just you)

Restart your DAW and rescan plugins if it doesn't show up automatically.

## CLAP

- **Windows**: Unzip, then copy `TrackDeck.clap` into `C:\Program Files\Common Files\CLAP`
- **macOS**: Unzip, then copy `TrackDeck.clap` into `/Library/Audio/Plug-Ins/CLAP` (all users) or `~/Library/Audio/Plug-Ins/CLAP` (just you)
- **Linux**: Unzip, then copy `TrackDeck.clap` into `/usr/lib/clap` (all users) or `~/.clap` (just you)

Make sure your DAW supports the CLAP format — most modern hosts do, but check its docs if it doesn't show up after rescanning.

## AU (macOS only)

- Unzip, then copy `TrackDeck.component` into `/Library/Audio/Plug-Ins/Components` (all users) or `~/Library/Audio/Plug-Ins/Components` (just you)
- Restart your DAW and rescan Audio Units if it doesn't appear right away

## Standalone

No install needed — this runs on its own, outside a DAW, useful for quick testing.

- **Windows**: Unzip, then double-click `TrackDeck.exe`
- **macOS**: Unzip, then double-click `TrackDeck.app` (you may need to right-click → Open the first time, since the app isn't notarized)
- **Linux**: Unzip, mark the file executable if needed (`chmod +x TrackDeck`), then run `./TrackDeck`

# Having trouble?

Open an issue on this repo with your OS, DAW, and plugin format, and we'll take a look.
