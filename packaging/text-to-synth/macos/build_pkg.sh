#!/usr/bin/env bash
# Builds the macOS installer (.pkg) for Text To Synth.
#
#   build_pkg.sh <TextToSynth binary> <version> <output.pkg>
#
# Installs the server into /Library/Application Support/Baastik Labs/Text To
# Synth and registers it with Claude Desktop (and Claude Code, if installed)
# for the logged-in user. Signs and notarizes when the Apple credentials below
# are set; otherwise ad-hoc signs (the installer then needs right-click > Open
# the first time).
#
# Optional environment:
#   MACOS_APP_IDENTITY        "Developer ID Application: Name (TEAMID)"
#   MACOS_INSTALLER_IDENTITY  "Developer ID Installer: Name (TEAMID)"
#   APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD   for notarization
set -euo pipefail

BINARY="$1"
VERSION="$2"
OUTPUT="$3"
ID="com.baastiklabs.texttosynth"
HERE="$(cd "$(dirname "$0")" && pwd)"

WORK="$(mktemp -d)"
ROOT="$WORK/root"
DEST="$ROOT/Library/Application Support/Baastik Labs/Text To Synth"
mkdir -p "$DEST"
cp "$BINARY" "$DEST/TextToSynth"
cp "$HERE/uninstall.command" "$DEST/Uninstall Text To Synth.command"
chmod 755 "$DEST/TextToSynth" "$DEST/Uninstall Text To Synth.command"

if [[ -n "${MACOS_APP_IDENTITY:-}" ]]; then
    codesign --force --timestamp --options runtime --identifier "$ID" --sign "$MACOS_APP_IDENTITY" "$DEST/TextToSynth"
else
    codesign --force --identifier "$ID" --sign - "$DEST/TextToSynth"
fi

pkgbuild --root "$ROOT" \
    --identifier "$ID" \
    --version "$VERSION" \
    --scripts "$HERE/scripts" \
    --install-location / \
    "$WORK/component.pkg"

sed "s/@VERSION@/$VERSION/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"
productbuild --distribution "$WORK/distribution.xml" \
    --resources "$HERE/resources" \
    --package-path "$WORK" \
    "$WORK/unsigned.pkg"

if [[ -n "${MACOS_INSTALLER_IDENTITY:-}" ]]; then
    productsign --timestamp --sign "$MACOS_INSTALLER_IDENTITY" "$WORK/unsigned.pkg" "$OUTPUT"
    if [[ -n "${APPLE_ID:-}" && -n "${APPLE_TEAM_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" ]]; then
        xcrun notarytool submit "$OUTPUT" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
            --password "$APPLE_APP_PASSWORD" --wait
        xcrun stapler staple "$OUTPUT"
    fi
else
    cp "$WORK/unsigned.pkg" "$OUTPUT"
fi
echo "Built $OUTPUT"
