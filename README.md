# norg

**Norg** is a stage keyboard plugin by Baastik Labs, inspired by the red keyboards you see on every
stage. It has two modes:

- **Norg Stage**: Organ, Piano and Synth sections with Panel A/B, layers and splits.
- **Norg Electro**: Organ, Piano and Sample sections on an Electro-style panel.

It runs as an AU (Logic, GarageBand), a VST3 (Ableton Live and others) and a standalone app.

> Norg is an independent fan project. It is not affiliated with or endorsed by Clavia or Nord.

## Install (macOS)

1. Download **[Norg-Installer.pkg](https://github.com/dylancleverdon/Baastik-Labs/releases/download/norg-channel/Norg-Installer.pkg)**.
2. Open it. The first time, macOS says it can't verify the developer (Norg isn't notarized by Apple).
   Open **System Settings → Privacy & Security**, scroll down and click **Open Anyway**, then
   run the installer. No admin password is needed.
3. Open (or restart) your DAW and look for **Norg** by **Baastik Labs** in your instruments.

Or install from Terminal, which skips step 2:

```
curl -fsSL https://github.com/dylancleverdon/Baastik-Labs/releases/download/norg-channel/install.sh | bash
```

Everything installs into your own user folders:

| What | Where |
| --- | --- |
| AU | `~/Library/Audio/Plug-Ins/Components/Norg.component` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/Norg.vst3` |
| Standalone | `~/Applications/Norg.app` |
| Updater and settings | `~/Library/Application Support/Norg/` |

## Updates

You never need to reinstall. A small background updater checks for a new Norg when you log in and
then every hour. When there is one it installs it quietly and shows a notification. Your DAW keeps
the version it already loaded, and uses the new one the next time you open it.

Click **System** on Norg's panel to:

- see the installed version and what's new,
- turn automatic updates on or off,
- **Check now**,
- **Roll back** to the previous version. The build you rolled back from is then skipped, but newer
  builds still install.

Only updates signed with Norg's release key and published from this repository's `norg-*`
releases are accepted. The updater logs to `~/Library/Logs/Norg/updater.log`.

## Uninstall

```
curl -fsSL https://github.com/dylancleverdon/Baastik-Labs/releases/download/norg-channel/uninstall.sh | bash
```

This removes the plugins, app and updater. Your saved programs and sample libraries are kept.

## Building from source

Requires CMake 3.22+ and a C++20 compiler (Xcode on macOS). JUCE and Catch2 are fetched
automatically.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Add `-DNORG_COPY_AFTER_BUILD=ON` to copy the built plugins into your plug-in folders.

`build/Norg/tools/norg-render --out demo.wav` renders a demo phrase through the plugin. Add
`--set <param>=<value>` to change parameters, or `--midi file.mid` to play a MIDI file.

## Releasing

Norg lives on the `norg` branch. Every push to `norg` runs `.github/workflows/norg-release.yml`:
build, tests, `auval`, pluginval, installer and updater smoke tests, then it publishes
`norg-v<version>` and refreshes the `norg-channel` release that the install link and updater read.
Nothing is published if any step fails.

Publishing needs one repository secret, `NORG_UPDATE_SIGNING_KEY` (the Ed25519 seed matching
`Norg/Updater/update-public-key.txt`).

## Credits

- Built with [JUCE](https://juce.com).
- Fonts: Archivo Black and Barlow Semi Condensed ([SIL Open Font License](Norg/Resources/fonts)).
- Update signatures: [Monocypher](https://monocypher.org) (BSD-2-Clause / CC0).
