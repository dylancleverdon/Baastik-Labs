# norg

**Norg** is a stage keyboard plugin by Baastik Labs, inspired by the red keyboards you see on every
stage. It has two modes:

- **Norg Stage**: Organ, Piano and Synth sections with Panel A/B, layers and splits.
- **Norg Electro**: Organ, Piano and Sample sections on an Electro-style panel.

It runs as an AU (Logic, GarageBand), a VST3 (Ableton Live and others) and a standalone app.

> Norg is an independent fan project. It is not affiliated with or endorsed by Clavia or Nord.

## Install (macOS)

1. Download **[Norg-Installer.pkg](https://github.com/dylancleverdon/Baastik-Labs/releases/download/norg-channel/Norg-Installer.pkg)** and open it.
2. The first time only, macOS asks you to approve it (Norg isn't registered with Apple's paid
   developer program): open **System Settings → Privacy & Security**, click **Open Anyway**, and the
   installer runs. No admin password is needed.
3. Open (or restart) your DAW and look for **Norg** by **Baastik Labs** in your instruments.

That's it. Right after installing, Norg's background helper downloads and prepares the sampled grand
piano (Salamander Grand, ~700 MB). The Piano section shows its progress and uses the modelled grand
until it's ready, then switches over by itself.

Everything installs into your own user folders:

| What | Where |
| --- | --- |
| AU | `~/Library/Audio/Plug-Ins/Components/Norg.component` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/Norg.vst3` |
| Standalone | `~/Applications/Norg.app` |
| Helper, settings, sample libraries | `~/Library/Application Support/Norg/` |

## Updates

You never need to reinstall or download anything by hand. The background helper checks for a new
Norg when you log in and then every hour, installs it quietly and shows a notification. Your DAW
keeps the version it already loaded and uses the new one the next time you open it. New or updated
sample libraries arrive the same way.

Click **System** on Norg's panel to see the installed version and what's new, turn automatic updates
on or off, **Check now**, or **Roll back** to the previous version (the build you rolled back from is
then skipped; newer builds still install).

The helper only installs files downloaded over HTTPS from this repository's `norg-*` releases, and
only if they match the SHA-256 checksum published with them. It logs to
`~/Library/Logs/Norg/updater.log`.

## Uninstall

Click **System** on Norg's panel, then **Uninstall...** and confirm. That removes the plugins, the
app, the background helper, the downloaded sample libraries and their caches. Your DAW projects
aren't touched (they keep their Norg settings, should you install again).

## Effects

Each panel has a Nord-style effects section: **Effect 1** (tremolo, pan, ring mod, wah, auto-wah),
**Effect 2** (phaser, flanger, chorus, vibe), **Amp/EQ** (Twin, Small and JC amps, 3-band EQ with a
sweepable mid), the **Rotary** speaker, and a **Delay** that syncs to your DAW's tempo (or tap
tempo). Each of those processes the section chosen with its **Source** button. **Comp** and
**Reverb** (room, stage, hall) work on the whole mix.

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
Nothing is published if any step fails, and no secrets or manual steps are involved.

Sample libraries are listed in `installer/sample-packs.json`. CI repackages each one into the
`norg-samples` release the first time (or when its version is bumped), and installed Norgs fetch
whatever they're missing.

## Credits

- Built with [JUCE](https://juce.com).
- Fonts: Archivo Black and Barlow Semi Condensed ([SIL Open Font License](Norg/Resources/fonts)).
- Salamander Grand Piano V3 by Alexander Holm ([CC-BY 3.0](https://creativecommons.org/licenses/by/3.0/)),
  SFZ mapping by kinwie via [sfzinstruments](https://github.com/sfzinstruments/SalamanderGrandPiano).
