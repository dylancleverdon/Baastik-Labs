# Baastik Labs repo: Norg

This repository holds more than one program. **Norg** (a Nord-style stage keyboard plugin) lives
only on the long-lived `norg` branch.

## Branch policy (important)
- Norg's code lives on `norg`. **Never merge Norg into `main`**, and never touch `main` or other
  programs' branches from a Norg session.
- Work on your `claude/*` session branch, then push or merge into `norg` to ship.
- **Every push to `norg` becomes an update** on the user's Mac: `norg-release.yml` builds, tests,
  validates (auval + pluginval) and publishes `norg-v<version>` plus the rolling `norg-channel`
  release. Installed copies pick it up within an hour. Only push to `norg` when the build is green.
- Releases are prerelease-only and never marked "Latest", so other programs' releases are unaffected.

## Build and test
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release        # add -DFETCHCONTENT_SOURCE_DIR_JUCE=... to reuse a JUCE checkout
cmake --build build -j
ctest --test-dir build --output-on-failure
```
Linux needs the ALSA/X11/freetype `-dev` packages (see `.github/workflows/norg-build.yml`).

- `build/Norg/tools/norg-render --out demo.wav [--set param_id=value ...] [--midi file.mid]` renders
  audio through the real processor.
- `xvfb-run -a build/Norg/tools/norg-render --screenshot panel.png [--set mode=1]` renders the UI.
- pluginval: `pluginval --strictness-level 8 --validate build/Norg/Norg_artefacts/Release/VST3/Norg.vst3`.

## Architecture
- `Norg/Source/params/ParamList.def` is the single list of parameters. **IDs are permanent**: never
  rename or reuse one. New parameters get the `versionHint` of the phase adding them. Panel-scoped
  parameters get a `b_`-prefixed Panel B copy automatically.
- Engines render from a `ParamSnapshot` (captured once per block), not from live parameters, so an
  engine fading out after a program change keeps its old sound.
- `engine/NorgEngine` = one panel's complete instrument; `engine/Section.h` is the interface each
  section (organ, piano, synth, sample) implements. No allocation, locks or I/O on the audio thread.
- UI: `ui/MainPanel` is laid out at a fixed logical size (1400x640) and scaled by the editor.
  Colours, fonts and drawing helpers are in `ui/NorgTheme.*` and `ui/NorgLookAndFeel.*`.
- Updates: `Norg/Updater` (the `NorgUpdater` helper) installs builds only from this repo's `norg-*`
  releases over HTTPS, and only when they match the manifest's SHA-256. It also installs the sample
  packs listed in `installer/sample-packs.json` (published by CI to the `norg-samples` release),
  pre-transcoding them into the shared sample cache (`engine/sfz/SampleCache`). The user never has
  to do anything by hand: keep it that way (no secrets, no terminal steps). Installer files are in
  `installer/`.

## Versioning
`VERSION` holds MAJOR.MINOR; CI appends its run number. Plugin state carries `schemaVersion`;
bump it and add an upgrade step in `NorgProcessor::setStateInformation` when the state format changes.
