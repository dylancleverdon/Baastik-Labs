#!/usr/bin/env bash
# Builds the macOS installer (.pkg) for Serum Preset Generator.
#
#   build_pkg.sh <artefacts dir> <version> <output.pkg>
#
# Installs the VST3 and AU into /Library/Audio/Plug-Ins and the standalone
# app into /Applications/Baastik Labs. Signs and notarizes when the Apple
# credentials below are set; otherwise ad-hoc signs (the installer then needs
# right-click > Open the first time).
#
# Optional environment:
#   MACOS_APP_IDENTITY        "Developer ID Application: Name (TEAMID)"
#   MACOS_INSTALLER_IDENTITY  "Developer ID Installer: Name (TEAMID)"
#   APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD   for notarization
set -euo pipefail

ARTEFACTS="$1"
VERSION="$2"
OUTPUT="$3"
NAME="Serum Preset Generator"
ID="com.baastiklabs.serumpresetgenerator"
HERE="$(cd "$(dirname "$0")" && pwd)"

WORK="$(mktemp -d)"
ROOT="$WORK/root"
mkdir -p "$ROOT/Library/Audio/Plug-Ins/VST3" "$ROOT/Library/Audio/Plug-Ins/Components" "$ROOT/Applications/Baastik Labs"
cp -R "$ARTEFACTS/VST3/$NAME.vst3" "$ROOT/Library/Audio/Plug-Ins/VST3/"
cp -R "$ARTEFACTS/AU/$NAME.component" "$ROOT/Library/Audio/Plug-Ins/Components/"
cp -R "$ARTEFACTS/Standalone/$NAME.app" "$ROOT/Applications/Baastik Labs/"

sign() {
    if [[ -n "${MACOS_APP_IDENTITY:-}" ]]; then
        codesign --force --deep --timestamp --options runtime --sign "$MACOS_APP_IDENTITY" "$1"
    else
        codesign --force --deep --sign - "$1"
    fi
}
sign "$ROOT/Library/Audio/Plug-Ins/VST3/$NAME.vst3"
sign "$ROOT/Library/Audio/Plug-Ins/Components/$NAME.component"
sign "$ROOT/Applications/Baastik Labs/$NAME.app"

# Install exactly where we say, even if another copy exists elsewhere.
pkgbuild --analyze --root "$ROOT" "$WORK/components.plist"
i=0
while /usr/libexec/PlistBuddy -c "Print :$i" "$WORK/components.plist" >/dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$WORK/components.plist"
    i=$((i + 1))
done

pkgbuild --root "$ROOT" \
    --component-plist "$WORK/components.plist" \
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
