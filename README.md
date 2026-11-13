# Cymatica

**Cymatica** turns whatever your Mac is playing into physics: liquids, ink, ripples, sparks and
more, rendered at your display's full Retina resolution (with HDR on XDR displays). It listens to
**BlackHole 2ch**, set up exactly like [cava](https://github.com/karlstav/cava), and its bars use a
port of cava's own algorithm.

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
