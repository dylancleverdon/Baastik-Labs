# Text To Synth

**by Baastik Labs.** Describe a sound to Claude and get a Serum 2 preset. Then
shape it in plain English: *"drier"*, *"it's too harsh"*, *"more movement"*,
*"wider"*, *"go back to before it got harsh"*.

Text To Synth is a **Claude plugin**: an MCP server that Claude Desktop (and
Claude Code) run in the background. Claude does the listening-to-words part and
Text To Synth does the Serum part: it writes real `.SerumPreset` files,
schema-checked and in the exact format Serum saves.

## Install

One click, then restart Claude:

- **macOS:** [TextToSynth-macOS.pkg](https://github.com/dylancleverdon/Baastik-Labs/releases/download/text-to-synth-latest/TextToSynth-macOS.pkg)
- **Windows:** [TextToSynth-Windows.exe](https://github.com/dylancleverdon/Baastik-Labs/releases/download/text-to-synth-latest/TextToSynth-Windows.exe)

The installer puts Text To Synth in place and connects it to Claude Desktop
(and to Claude Code, if you have it) for you. From then on it **updates
itself**: when a new version is out, Claude tells you and can install it when
you say so. Vocabulary improvements arrive silently, with no reinstall.

## Use it

In Claude:

> Make me a dark, detuned reese bass for drum & bass, with a slow filter wobble.

> Drier. And it's a bit too harsh.

> Wider, and add some movement to the top end.

> Undo that last change.

Presets land in Serum 2's browser under **User › Text To Synth**. Serum doesn't
reload files by itself, so after each change **click the preset again**.
Every version is kept, so you can always go back.

Text To Synth never changes presets it didn't make. Ask it to tweak a factory
preset (give Claude the file path) and it saves an edited copy in the Text To
Synth folder.

## How it works

```
you ──words──▶ Claude ──edits (JSON)──▶ Text To Synth ──▶ .SerumPreset (+ history)
                  ▲                            │
                  └── description of the patch ┘
```

- `sound_design_guide` gives Claude the parameter names and a vocabulary that
  maps words to moves (e.g. *harsh* → resonance, drive, cutoff, high shelf),
  with step sizes calibrated on Serum 2's factory library.
- `create_preset` starts from a Baastik generator recipe (reese, supersaw, 808,
  pads, plucks...), Serum's init patch or an existing preset, then applies
  Claude's edits.
- `describe_preset` shows Claude the real state of the patch, so "too harsh" is
  judged against actual values.
- `edit_preset` applies edits (`set`/`add`/`scale` on things like
  `filter1.cutoff` or `fx.reverb.mix`, add/remove FX, mod routes, LFO
  shapes/rates, wavetables) all-or-nothing and saves a new version.
- `list_versions` / `restore_version` provide history and undo.
- `check_for_updates` / `install_update` run the updater.

Claude can't hear the result, so it tells you what it changed and asks how it
sounds. Your feedback drives the next step.

## Uninstall

- **macOS:** run *Uninstall Text To Synth* in
  `/Library/Application Support/Baastik Labs/Text To Synth`.
- **Windows:** Settings › Apps › Text To Synth › Uninstall.

Both remove the Claude connection. Your presets stay where they are.

## Develop

```sh
cmake -S . -B build -DBAASTIK_BUILD_PLUGINS=OFF && cmake --build build && ctest --test-dir build
# Try the server in Claude Code without installing (no updater in this build):
claude mcp add text-to-synth-dev -- "$PWD/build/plugins/TextToSynth/text-to-synth-dev"
```

- `core/`: JUCE-free engine (targets, edits, describe, history, MCP server,
  Claude registration). Tests are in `tests/test_text_to_synth.cpp`.
- `Source/`: the shipped app. It runs the server on a worker thread and the
  shared `baastik_updater` on the JUCE message thread.
- `Resources/content/vocabulary.json`: the word → parameter vocabulary. Changes
  here ship as a content-only hot update, and a test checks every name in it.
- Environment overrides: `TEXT_TO_SYNTH_PRESETS_DIR`, `TEXT_TO_SYNTH_DATA_DIR`,
  and `TEXT_TO_SYNTH_UPDATE_URL` (point the updater at a test `update.json`).
- `text-to-synth --register` / `--unregister` add or remove the Claude config
  entries (the installers run these).

To check edits against a full preset library without committing it:
`SERUM_FIXTURES="$HOME/Documents/Xfer/Serum 2 Presets/Presets" ./build/tests/baastik_tests`.

### Releases

`.github/workflows/text-to-synth.yml` builds and tests every change. Pushes to
`text-to-synth*`, `claude/text-to-synth*`, `release/text-to-synth` or `main`
publish to the `text-to-synth-latest` channel:

- code change: new signed installers, `update.json` and stable download names;
- vocabulary-only change: just `content.json`.

The `release` job uses the `text-to-synth` GitHub environment. Signing and
notarization use the same `MACOS_*` / `APPLE_*` secrets as the other plugins;
without them the macOS package is ad-hoc signed.

The Serum 2 format is unofficial and reverse-engineered (see
`docs/serum2-format.md`), so a Serum update can break it. The fixture tests are
there to catch that.
