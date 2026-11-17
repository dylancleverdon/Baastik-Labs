# Baastik Labs repo: Tutti

This repository holds more than one program, each on its own long-lived branch with unrelated
history (`norg`, `cymatica`, `text-to-synth`, ...). **Tutti** (edit one Ableton Live Set together,
live) lives only on the `tutti` branch.

## Branch policy (important)
- Tutti's code lives on `tutti`. Never merge it into another program's branch or `main`, and never
  touch other programs' branches from a Tutti session.
- Work on your `claude/*` session branch, then push it to `tutti` to ship.
- **Every push to `tutti` becomes an update** on everyone's computer: `.github/workflows/tutti.yml`
  builds, tests and publishes `tutti-v<version>` plus the rolling `tutti-channel` release. Installed
  copies pick it up within an hour. Only push to `tutti` when the build is green.
- Releases are prerelease-only and never marked "Latest", so other programs' releases are unaffected.

## Build and test
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # add -DFETCHCONTENT_SOURCE_DIR_JUCE=... to reuse a JUCE checkout
cmake --build build -j
ctest --test-dir build --output-on-failure        # C++ core tests
python3 -m pytest remote_script/tests -q          # Remote Script tests (fake Live)
python3 tools/e2e/run_e2e.py build                # several headless peers + fake Live scripts
```
- `-DTUTTI_BUILD_APP=OFF -DTUTTI_BUILD_PLUGIN=OFF` builds only the core, CLI and tests.
- Linux needs the ALSA/X11/freetype/curl `-dev` packages (see `.github/workflows/tutti.yml`).
- `build/app/Tutti_artefacts/Release/Tutti --screenshot <dir>` renders every screen to PNGs
  (use `xvfb-run -a` on Linux).
- `build/tools/cli/tutti-peer` is a headless Tutti (no GUI) used by the end-to-end tests.

## Architecture (details in `docs/DESIGN.md`)
- `remote_script/Tutti/`: Python Remote Script, the only code that talks to Live's API. Keep it
  Python 3.7 compatible. Live forbids changing the Set inside listener callbacks: queue and apply
  on the tick. Probe API features with `hasattr` and degrade to the save lane, never crash Live.
  Tests use the fake `Live` package in `remote_script/tests/fakelive/`, which enforces Live's rules.
- `core/`: testable C++ logic (document, ops, sequencer, replica, session/roles/failover, protocol,
  .als handling, store, versions, updater decisions, suggestion queue). An INTERFACE library:
  each consumer compiles it with its own JUCE modules.
- `app/`: the Tutti app (JUCE GUI + tray). `Engine` wires core to sockets, files and the UI, all on
  the message thread; IO threads post back to it.
- `plugin/`: Tutti Stand-in (VST3/AU) for shared bounces.
- The entity schema (types and prop names) is shared by Python and C++: change both together and
  update the table in `docs/DESIGN.md`.
- Personal state (solo, arm, monitoring, loop region, view...) is never synced.

## Rules
- Users never do anything by hand beyond the one-time setup in `docs/SETUP.md` (install Tailscale,
  run the installer, pick Tutti as a Control Surface). No secrets, no terminal steps.
- The updater installs only from this repo's `tutti-*` releases over HTTPS and only if the SHA-256
  matches the manifest. Never update while a session is running.
- Tutti never modifies a user's original project: sessions work on copies under `Music/Tutti`.

## Versioning
`VERSION` holds MAJOR.MINOR; CI appends its run number. The session protocol has its own
`kProtocolVersion` in `core/include/tutti/Protocol.h`: bump it whenever messages change
incompatibly (peers with different protocol versions refuse to share a session and the older one
checks for updates).
