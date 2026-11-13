# Cymatica

**Cymatica** turns whatever your Mac is playing into physics: liquids, ink, ripples, sparks and
more, rendered at your display's full Retina resolution (with HDR on XDR displays). It listens to
**BlackHole 2ch**, set up exactly like [cava](https://github.com/karlstav/cava), and its bars use a
port of cava's own algorithm.

## Scenes

| Scene | What's simulated | What the music does |
|---|---|---|
| **Liquid** | 3D fluid (MLS-MPM, up to 250k particles) in a glass tank, rendered as a refracting surface | Bass sloshes the tank, kicks fire geysers, mids stir a whirlpool |
| **Ripples** | Wave-equation water in a pool with real-time caustics | Bands drop ripples from the centre (bass) to the rim (highs), Faraday waves |
| **Ink** | 2D Navier–Stokes fluid with vorticity confinement | Each band is a jet of ink, kicks splash, bass swirls |
| **Singularity** | Ray-traced Schwarzschild black hole, lensed starfield, accretion disk with Doppler beaming | Bass heats the disk, kicks send shockwaves and fire jets |
| **Ferrofluid** | Magnetic fluid with Rosensweig spikes (spring physics) | Every spike rides its own frequency band; kicks make them jump |
| **Chladni** | Cymatics: up to 2M sand grains on a vibrating plate | The dominant note picks the standing-wave mode |
| **Stardust** | A galaxy of up to 4M stars around moving gravity wells | Bass is gravity, treble is turbulence, kicks are supernovas |
| **Bars** | cava's bars (Classic), plus springy Jelly bars, caps and sparks | cava's own algorithm, 1:1 |
| **Phosphor** | An oscilloscope drawn by a simulated CRT beam | Lissajous / goniometer / waveform / polar |

Everything is customizable: 23 themes (Synthwave, Vaporwave, Catppuccin, Dracula, Nord, Gruvbox,
Tokyo Night, your cava colours...), every scene setting can follow the music (bass, kicks, beat...),
a look panel (glow, trails, kaleidoscope, chromatic aberration, CRT scanlines, Synesthesia colours
by musical key), presets you can save and share, auto-pilot that changes scenes on the beat, HDR on
XDR displays, render scale up to 2x supersampling, live-wallpaper mode and 8K screenshots.

## Install (macOS)

```
curl -fsSL https://github.com/dylancleverdon/Baastik-Labs/releases/download/cymatica-channel/install.sh | bash
```

That installs `~/Applications/Cymatica.app` (no admin password) and opens it. Cymatica keeps itself
up to date: it checks at launch and every hour, downloads in the background and shows **Restart now**
(or installs when you quit). **Updates** in the panel lets you check now, turn automatic updates off
or roll back.

### Audio: same setup as cava
1. `brew install blackhole-2ch`
2. In **Audio MIDI Setup**, create a **Multi-Output Device** with your speakers (first) and
   BlackHole 2ch, and choose it as your Mac's output.
3. Cymatica opens BlackHole 2ch automatically. macOS asks once for microphone access (BlackHole counts
   as a microphone; audio never leaves your Mac). Without BlackHole, a built-in demo track plays.

Already use cava? **Audio → Import my cava config** picks up your source, bar settings and colours.

## Keys
H hide panel · ←/→ scenes · ↑/↓ presets · T theme · R randomize · M mutate · F fullscreen ·
D auto-pilot · S screenshot · I info · Space freeze · 1–9 jump to a scene

## Uninstall
```
curl -fsSL https://github.com/dylancleverdon/Baastik-Labs/releases/download/cymatica-channel/uninstall.sh | bash
```

## Building from source
Rust 1.85+. `cargo run -p cymatica-app --bin cymatica`. See `CLAUDE.md` for the architecture and
release flow.

## Credits
- Bars use a port of cava's cavacore (MIT, © 2015 Karl Stavestrand), see `THIRD_PARTY_NOTICES.md`.
- Built with wgpu, winit, egui, cpal and realfft.
