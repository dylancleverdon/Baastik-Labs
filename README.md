# Baastik Labs

Audio plugins by Baastik Labs. Each plugin lives in `plugins/<Name>` and has
its own version, build workflow, installers and update channel, so shipping one
never touches the others.

| Plugin | What it does | Download |
|---|---|---|
| [Serum Preset Generator](plugins/SerumPresetGenerator) | Generates Serum 2 presets: random, or guided by sound type and genre | [macOS](https://github.com/dylancleverdon/Baastik-Labs/releases/download/serum-preset-generator-latest/SerumPresetGenerator-macOS.pkg) · [Windows](https://github.com/dylancleverdon/Baastik-Labs/releases/download/serum-preset-generator-latest/SerumPresetGenerator-Windows.exe) |
| [Text To Synth](plugins/TextToSynth) | Claude plugin: describe a sound, get a Serum 2 preset, then refine it in plain English ("drier", "too harsh") | [macOS](https://github.com/dylancleverdon/Baastik-Labs/releases/download/text-to-synth-latest/TextToSynth-macOS.pkg) · [Windows](https://github.com/dylancleverdon/Baastik-Labs/releases/download/text-to-synth-latest/TextToSynth-Windows.exe) |

## Repo layout

```
plugins/<Name>/           one folder per plugin (JUCE plugin + its own core library)
libs/serum2/              Serum 2 preset file format (read/write/edit), no JUCE
modules/baastik_updater/  shared JUCE module: per-plugin update checks + installer launch
packaging/<plugin-id>/    installer definitions (macOS .pkg, Windows Inno Setup)
tools/serumtool/          command line: dump, pack, diff, scan and generate presets
tools/release/            release helpers (update manifests)
tests/                    Catch2 unit tests
.github/workflows/        one workflow per plugin
```

## Building

Requires CMake 3.24+ and a C++20 compiler. Dependencies (JUCE 8, nlohmann/json,
zstd, Catch2) are fetched automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build
```

`-DBAASTIK_BUILD_PLUGINS=OFF` builds only the libraries, `serumtool` and the tests
(no JUCE needed).

## Releases and updates

Each plugin publishes to its own GitHub release channel, `<plugin-id>-latest`,
which holds:

- the installers under stable names (the download links above never change),
- `update.json`, which the plugin reads to offer one-click updates,
- `content.json`, generator content (genres, recipes...) the plugin downloads on
  its own, with no reinstall.

See the plugin's README for how publishing works.

JUCE is used under its licensing terms (AGPLv3, or a JUCE commercial/Starter
license for closed-source distribution).
