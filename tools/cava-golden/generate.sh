#!/bin/bash
# Regenerates crates/cymatica-audio/tests/data/cava_golden.bin by running cava's cavacore.c
# (pinned commit below) on the harness's deterministic signal.
#
#   tools/cava-golden/generate.sh [path/to/cava-source-dir]
#
# Pass a directory that already holds cavacore.c/cavacore.h (from the pinned commit) or let the
# script download them. Needs a C compiler and FFTW (apt-get install libfftw3-dev / brew install fftw).
set -euo pipefail
CAVA_COMMIT=505cbf6dc19be0e0aaf082b5b35f1031102c5e97
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/../../crates/cymatica-audio/tests/data/cava_golden.bin"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

SRC="${1:-}"
if [ -z "$SRC" ]; then
  SRC="$WORK/src"
  mkdir -p "$SRC"
  for f in cavacore.c cavacore.h; do
    curl -fsSL -o "$SRC/$f" "https://raw.githubusercontent.com/karlstav/cava/$CAVA_COMMIT/$f"
  done
fi

cc -O2 -o "$WORK/harness" -I"$SRC" "$HERE/harness.c" "$SRC/cavacore.c" \
  $(pkg-config --cflags --libs fftw3 2>/dev/null || echo -lfftw3) -lm
mkdir -p "$(dirname "$OUT")"
"$WORK/harness" "$OUT"
echo "wrote $OUT ($(wc -c < "$OUT") bytes)"
