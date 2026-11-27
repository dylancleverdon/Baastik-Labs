# Baastik Labs repo: CueMix

This repository holds more than one program, each on its own long-lived branch with unrelated
history (`norg`, `tutti`, `cymatica`, `text-to-synth`, ...). **CueMix** (personal headphone mixes
inside Ableton Live: a Send plugin per track, a multi-output mixer instrument, and a companion app
with updater and suggestion box) lives only on the `cuemix` branch.

## Branch policy (important)
- CueMix's code lives on `cuemix`. Never merge it into another program's branch or `main`, and
  never touch other programs' branches from a CueMix session.
- Work on your session branch, then push it to `cuemix` to ship. Once CI publishing exists, every
  push to `cuemix` becomes an update on users' computers.

## Status
Phase 1 (Mac proof of concept) is in progress. The full plan, decisions and phases are in
`docs/PLAN.md`: read it first.

## Rules
- The audio thread never locks, allocates or does I/O. The shared bus layout is versioned: change
  `kBusLayoutVersion` and the bus name together with any layout change.
- Users never do anything by hand beyond running the installer and the Live setup in
  `docs/SETUP.md`. No secrets, no terminal steps.
- The updater installs only from this repo's `cuemix-*` releases over HTTPS and only if the
  SHA-256 matches. It never deletes the running program (on Windows it renames itself aside).
