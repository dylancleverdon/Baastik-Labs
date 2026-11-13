#!/bin/bash
# Installs (or updates) Cymatica:
#
#   curl -fsSL https://github.com/dylancleverdon/Baastik-Labs/releases/download/cymatica-channel/install.sh | bash
#
# Downloads the latest build, checks its SHA-256 against the release manifest, installs it in
# ~/Applications (no admin password), signs it with an identity that only exists on this Mac (so
# the microphone permission survives updates), checks for BlackHole and opens Cymatica.
# Downloading with curl means macOS never shows the "unidentified developer" prompt.
set -euo pipefail

FEED="${CYMATICA_FEED:-https://github.com/dylancleverdon/Baastik-Labs/releases/download/cymatica-channel/cymatica-update.json}"
APPS="$HOME/Applications"
APP="$APPS/Cymatica.app"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

say() { printf '\033[1m%s\033[0m\n' "$*"; }

say "Downloading Cymatica..."
curl -fsSL -o "$TMP/manifest.json" "$FEED"
read -r ZIP_URL SHA VERSION < <(/usr/bin/python3 - "$TMP/manifest.json" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
print(m["zipUrl"], m["sha256"], m["version"])
PY
)
case "$ZIP_URL" in
  https://github.com/dylancleverdon/Baastik-Labs/releases/download/cymatica-*|file://*) ;;
  *) echo "Refusing to download from $ZIP_URL" >&2; exit 1 ;;
esac
curl -fL --progress-bar -o "$TMP/Cymatica.zip" "$ZIP_URL"
GOT="$(shasum -a 256 "$TMP/Cymatica.zip" | awk '{print $1}')"
if [ "$GOT" != "$SHA" ]; then
  echo "Checksum mismatch (expected $SHA, got $GOT). Nothing was installed." >&2
  exit 1
fi

say "Installing Cymatica $VERSION into $APPS..."
mkdir -p "$TMP/unpacked" "$APPS"
ditto -x -k "$TMP/Cymatica.zip" "$TMP/unpacked"
NEW="$TMP/unpacked/Cymatica.app"
[ -d "$NEW" ] || { echo "The download doesn't contain Cymatica.app" >&2; exit 1; }
# quit a running copy first
osascript -e 'tell application id "com.baastiklabs.cymatica" to quit' >/dev/null 2>&1 || true
pkill -x Cymatica >/dev/null 2>&1 || true
rm -rf "$APP"
ditto "$NEW" "$APP"
xattr -dr com.apple.quarantine "$APP" 2>/dev/null || true

SIGNED="$(/bin/bash "$APP/Contents/Resources/sign-local.sh" sign "$APP" 2>/dev/null || echo adhoc)"
if [ "$SIGNED" != "identity" ]; then
  echo "(Signed ad-hoc; macOS may ask for microphone access again after updates.)"
fi

echo
if system_profiler SPAudioDataType 2>/dev/null | grep -qi blackhole || ls /Library/Audio/Plug-Ins/HAL 2>/dev/null | grep -qi blackhole; then
  say "BlackHole is installed."
  echo "Cymatica listens to BlackHole 2ch, just like cava. Keep your Mac's output set to the"
  echo "Multi-Output Device that includes BlackHole (System Settings > Sound > Output)."
else
  say "BlackHole isn't installed yet."
  if command -v brew >/dev/null 2>&1; then
    echo "Install it with:  brew install blackhole-2ch"
  else
    echo "Get it from https://existential.audio/blackhole/"
  fi
  echo "Then create a Multi-Output Device (your speakers + BlackHole 2ch) in Audio MIDI Setup and"
  echo "select it as your output, exactly like for cava. Until then Cymatica plays a demo track."
fi
echo
say "Cymatica $VERSION is installed. It keeps itself up to date."
if [ -z "${CYMATICA_NO_LAUNCH:-}" ]; then
  open "$APP"
fi
