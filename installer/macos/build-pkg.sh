#!/bin/bash
# Builds Norg-Installer.pkg from a staged payload folder.
#
#   installer/macos/build-pkg.sh <payload-dir> <version> <output-dir>
#
# <payload-dir> holds Norg.component, Norg.vst3, Norg.app, NorgUpdater and release.json
# (the same folder that becomes the update zip).
set -euo pipefail

PAYLOAD="$1"
VERSION="$2"
OUT="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

ROOT="$WORK/root"
mkdir -p "$ROOT/Library/Audio/Plug-Ins/Components" "$ROOT/Library/Audio/Plug-Ins/VST3" \
         "$ROOT/Applications" "$ROOT/Library/Application Support/Norg"

ditto "$PAYLOAD/Norg.component" "$ROOT/Library/Audio/Plug-Ins/Components/Norg.component"
ditto "$PAYLOAD/Norg.vst3"      "$ROOT/Library/Audio/Plug-Ins/VST3/Norg.vst3"
ditto "$PAYLOAD/Norg.app"       "$ROOT/Applications/Norg.app"
ditto "$PAYLOAD/NorgUpdater"    "$ROOT/Library/Application Support/Norg/NorgUpdater"
cp "$PAYLOAD/release.json"      "$ROOT/Library/Application Support/Norg/installed.json"

# Bundles must not be "relocatable", or Installer may put them wherever it finds an older copy.
pkgbuild --analyze --root "$ROOT" "$WORK/components.plist"
i=0
while /usr/libexec/PlistBuddy -c "Print :$i" "$WORK/components.plist" >/dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$WORK/components.plist"
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsVersionChecked false" "$WORK/components.plist"
    /usr/libexec/PlistBuddy -c "Set :$i:BundleOverwriteAction upgrade" "$WORK/components.plist"
    i=$((i + 1))
done

pkgbuild --root "$ROOT" \
         --component-plist "$WORK/components.plist" \
         --identifier com.baastiklabs.norg \
         --version "$VERSION" \
         --install-location / \
         --scripts "$HERE/scripts" \
         "$WORK/Norg-component.pkg"

sed "s/@VERSION@/$VERSION/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"

mkdir -p "$OUT"
productbuild --distribution "$WORK/distribution.xml" \
             --package-path "$WORK" \
             --resources "$HERE/resources" \
             "$OUT/Norg-Installer.pkg"

echo "Built $OUT/Norg-Installer.pkg"
