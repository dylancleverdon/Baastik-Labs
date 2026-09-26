# Test fixtures

Put Serum 2 presets **saved by Serum itself** here (`*.SerumPreset`). The
round-trip test checks that decoding and re-encoding each one reproduces
Serum's bytes exactly.

Only commit presets you made yourself (an Init preset plus a few single-change
presets is ideal). To test against presets from sample packs without committing
them, point `SERUM_FIXTURES` at their folder:

```sh
SERUM_FIXTURES="$HOME/Documents/Xfer/Serum 2 Presets/Presets" ./build/tests/baastik_tests
```
