# Baastik Labs repo: Cymatica

This repository holds more than one program, each on its own branch with its own history.
**Cymatica** (a physics-driven music visualizer for macOS) lives only on the long-lived `cymatica`
branch.

## Branch policy (important)
- Cymatica's code lives on `cymatica`. Never merge it into other programs' branches (`norg`,
  `text-to-synth`, ...), and never touch those branches from a Cymatica session.
- Work on your `claude/*` session branch (CI runs there), then push or merge into `cymatica` to ship.
- **Every push to `cymatica` becomes an update** on the user's Mac: `cymatica-release.yml` builds,
  tests, self-tests every scene on Metal, runs the real app window for 27 s (`CYMATICA_SMOKE_TEST`),
  runs the install and updater end-to-end tests, then publishes `cymatica-v<version>` plus the
  rolling `cymatica-channel` release. Installed copies pick it
  up within an hour. Only push to `cymatica` when the build is green.
- Releases are prerelease-only and never marked "Latest", so other programs' releases are unaffected.

## Build and test
```
cargo test --workspace                      # Linux needs: libasound2-dev pkg-config mesa-vulkan-drivers
cargo run -p cymatica-app --bin cymatica    # the app (demo track when there's no BlackHole)
cargo run -p cymatica-app --bin cymatica -- render --scene ink --start 20 --size 1920x1080 --out ink.png
cargo run -p cymatica-app --bin cymatica -- self-test --out sheet.png
CYMATICA_SMOKE_TEST=12 cargo run -p cymatica-app --bin cymatica  # real window, every scene, then quit
cargo +1.97.0 fmt --all --check && cargo +1.97.0 clippy --workspace --all-targets -- -D warnings  # CI's lint
```
Rendering works headlessly on a software Vulkan driver (lavapipe, or Chromium's SwiftShader via
`VK_ICD_FILENAMES`), so every scene can be rendered and looked at without a Mac.

## Architecture
- `crates/cymatica-audio`: capture (cpal; opens "BlackHole 2ch" by default, cava's `source`), a
  line-for-line port of cava's `cavacore.c` (golden-tested against the real C code in
  `tools/cava-golden`), and `Analyzer` → `AudioFrame` (spectrum, bands, onsets, tempo, chroma...).
  `engine::AudioEngine` runs it on a thread and serves time-stamped, interpolated frames.
- `crates/cymatica-engine`: wgpu renderer. Each scene implements `scene::Scene` and declares its
  parameters in a static `PARAMS` list. **Parameter IDs are permanent** (stored in presets/settings):
  never rename or reuse one. Shaders are WGSL in `src/shaders/`; every module is validated and
  translated to Metal in `tests/shaders.rs`. Scenes render HDR into `gpu::HDR_FORMAT`; `post.rs` adds
  trails, bloom, grading, tone mapping and output encoding (sRGB or EDR).
- `crates/cymatica-update`: manifest validation, Norg-style update decisions, download/verify/stage,
  install/rollback. Only this repo's `cymatica-*` release assets with a matching SHA-256 install.
- `crates/cymatica-app`: winit window, egui overlay UI, settings (`settings.json`), presets,
  auto-pilot, macOS glue (wallpaper mode, microphone permission), CLI (`render`, `self-test`,
  `update` test hooks).
- Packaging: `packaging/macos/` (bundle builder, install/uninstall scripts, `sign-local.sh` which
  signs with an identity created on the user's Mac so the microphone permission survives updates).
  macOS runs `sign-local.sh` with bash 3.2 (`set -u` + empty arrays need `${a[@]+"${a[@]}"}`); its
  keychain is only in the search list while codesign runs. The `signing` CI job tests it in ~1 min.

## Versioning
`VERSION` holds MAJOR.MINOR; CI appends its run number. `CFBundleVersion` is the build number the
updater compares.
