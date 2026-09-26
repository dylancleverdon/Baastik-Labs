# Serum Preset Generator

A VST3/AU plugin (and standalone app) that writes new **Serum 2** presets
(`.SerumPreset`). Pick a sound type and a genre, or go fully random, choose which
parts of the patch to randomize, and drag the result straight into Serum.

## Install

- **macOS:** [SerumPresetGenerator-macOS.pkg](https://github.com/dylancleverdon/Baastik-Labs/releases/download/serum-preset-generator-latest/SerumPresetGenerator-macOS.pkg)
- **Windows:** [SerumPresetGenerator-Windows.exe](https://github.com/dylancleverdon/Baastik-Labs/releases/download/serum-preset-generator-latest/SerumPresetGenerator-Windows.exe)

Run the installer and restart your DAW. The plugin shows up under **Baastik Labs**
as an effect; put it on any track (it passes audio through untouched).

Until the installers are signed with an Apple Developer ID / Windows code-signing
certificate, the OS will warn the first time:
- macOS: open **System Settings › Privacy & Security** and click **Open Anyway**
  (or right-click the .pkg › Open on older macOS).
- Windows SmartScreen: **More info › Run anyway**.

After that, updates install from inside the plugin.

## Using it

1. **Mode**
   - **Random**: everything randomized across its full range.
   - **Guided**: follows the **Sound type** and **Genre** you choose.
   - **Mutate**: makes variations of the selected preset.
2. **Sound type**: Bass, 808 & Sub, Lead, Pad, Pluck, Synth Keys, Drums & Perc,
   FX & Experimental, plus acoustic-style Electric Piano, Organ, Mallets & Bells,
   Brass, Winds & Flutes, Strings, Guitar & Plucked and Piano. "Any" lets the
   genre decide.
3. **Genre**
   - Electronic: Dubstep, Riddim, Drum & Bass, House, Techno, Trance, Future Bass,
     Synthwave, Lo-Fi Hip Hop, Hyperpop, Trap, Ambient, Psytrance.
   - Acoustic & Band: Jazz, Soul, Neo Soul, Jazz Fusion, Funk, 90s R&B, Gospel,
     Blues, Bossa Nova.
4. **Chaos** loosens the rules: 0% stays on-profile, 100% allows anything.
5. **Randomize**: untick a part (Oscillators, Noise + Sub, Filters, Envelopes,
   LFOs, Mod Matrix, Macros, FX, Global) to keep it from the selected preset. For
   example, generate until the oscillators sound right, untick them, then keep
   rolling FX and modulation.
6. **Generate**. Presets are saved to `Documents/Xfer/Serum 2 Presets/Presets/User/Baastik`
   (change it at the bottom). Drag one from the list into Serum, or find it in
   Serum's browser under User › Baastik.

The summary pane lists everything that was generated (oscillators, filter, FX
chain, every mod route and macro) and the seed, so any result can be reproduced.

## Updates

- **Plugin updates:** when a new version is published, the plugin shows a banner
  on open. Click **Update now** to download and launch the installer, then
  restart your DAW. Turn this off or check manually from **Settings**.
- **Content updates:** new or tweaked genres, categories and recipes download
  automatically in the background, with no reinstall.

Updates come only from this plugin's own channel
(`serum-preset-generator-latest`). Releases of other Baastik plugins never
affect it.

### Publishing (for developers)

The workflow `.github/workflows/serum-preset-generator.yml` only runs when this
plugin's files (or the shared libs it uses) change. It publishes when you push
to one of this plugin's branches (`serum-preset-generator*`,
`claude/serum-preset-generator*`, `release/serum-preset-generator`) or `main`,
or when you run it manually:

- **Code changed:** version `major.minor` from `VERSION`, plus the run number.
  Builds a macOS .pkg (VST3 + AU + app, universal) and a Windows installer
  (VST3 + app), creates the release `serum-preset-generator-v<version>`, and
  refreshes the channel.
- **Only `Resources/content` changed:** publishes just `content.json`, and
  plugins pick it up silently.

Optional signing (repository secrets): `MACOS_CERTS_P12` (base64 .p12 with
Developer ID Application + Installer certificates), `MACOS_CERTS_PASSWORD`,
`MACOS_APP_IDENTITY`, `MACOS_INSTALLER_IDENTITY`, and for notarization
`APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`. The publish job runs in the
`serum-preset-generator` GitHub environment, where you can add approval rules.

## How generation works

Everything musical is data in [`Resources/content`](Resources/content):

| File | Role |
|---|---|
| `base.json` | Every knob with its widest sensible range (what Random mode uses) |
| `categories/*.json` | Role rules: envelopes, octaves, mono/poly, filter use... |
| `genres/*.json` | Flavor: FX, wavetable families, warp, LFO rates, plus `perCategory` tweaks |
| `recipes/*.json` | Sound-design templates (Moog bass, Rhodes EP, supersaw, drawbar organ, 808...) with fixed mod routes |
| `macros.json` | Macro themes (Tone, Space, Growl...) and what they control |
| `names.json` | Word lists for preset names |

Knob layers are applied in this order: base, then genre, then category, then
genre-per-category, then recipe (later layers win). Knob values look like this:

```json
"amp.attack": {"range": [0.3, 2.5], "log": true},
"filter.type": {"choose": {"MgL24": 3, "L12": 1}},
"fx.FXReverb": 0.9,
"osc0.wavetable": "rhodes_mk1"
```

Slot-specific knobs (`osc1.octave`) override generic ones (`osc.octave`). The unit
test `test_content.cpp` checks every value against Serum's real parameter names,
so typos fail CI.

The command-line tool does the same from a terminal:

```sh
serumtool list
serumtool gen --genre rnb --category bass --count 10 --out ~/Desktop/rnb-bass
serumtool gen --mode mutate --base "BA Smooth Bass.SerumPreset" --chaos 0.3 --count 5
serumtool dump preset.SerumPreset preset.json      # inspect
serumtool diff a.SerumPreset b.SerumPreset         # what changed
serumtool scan "~/Documents/Xfer/Serum 2 Presets"  # wavetables your presets use
```
