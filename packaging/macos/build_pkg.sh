#!/bin/bash
# Builds Tutti-Installer.pkg from a staged payload folder (the same folder that becomes the
# update zip).
#
#   packaging/macos/build_pkg.sh <payload-dir> <version> <output-dir>
#
# <payload-dir> holds Tutti.app (with the Remote Script inside) and, when built, the Tutti
# Stand-in plugins. Everything installs into the current user's home folder: no admin password,
# and the app can update itself later without one.
set -euo pipefail

PAYLOAD="$1"
VERSION="$2"
OUT="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

ROOT="$WORK/root"
mkdir -p "$ROOT/Applications" "$ROOT/Library/Audio/Plug-Ins/VST3" "$ROOT/Library/Audio/Plug-Ins/Components"
ditto "$PAYLOAD/Tutti.app" "$ROOT/Applications/Tutti.app"
[ -d "$PAYLOAD/Tutti Stand-in.vst3" ] && ditto "$PAYLOAD/Tutti Stand-in.vst3" "$ROOT/Library/Audio/Plug-Ins/VST3/Tutti Stand-in.vst3"
[ -d "$PAYLOAD/Tutti Stand-in.component" ] && ditto "$PAYLOAD/Tutti Stand-in.component" "$ROOT/Library/Audio/Plug-Ins/Components/Tutti Stand-in.component"

# Bundles must not be "relocatable", or Installer may put them wherever it finds an older copy.
pkgbuild --analyze --root "$ROOT" "$WORK/components.plist"
i=0
while /usr/libexec/PlistBuddy -c "Print :$i" "$WORK/components.plist" >/dev/null 2>&1; do
    plutil -replace "$i.BundleIsRelocatable" -bool NO "$WORK/components.plist"
    plutil -replace "$i.BundleIsVersionChecked" -bool NO "$WORK/components.plist"
    plutil -replace "$i.BundleOverwriteAction" -string upgrade "$WORK/components.plist"
    i=$((i + 1))
done

pkgbuild --root "$ROOT" \
         --component-plist "$WORK/components.plist" \
         --identifier com.baastiklabs.tutti \
         --version "$VERSION" \
         --install-location / \
         --scripts "$HERE/scripts" \
         "$WORK/Tutti-component.pkg"

sed "s/@VERSION@/$VERSION/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"
mkdir -p "$OUT"
productbuild --distribution "$WORK/distribution.xml" \
             --package-path "$WORK" \
             --resources "$HERE/resources" \
             "$OUT/Tutti-Installer.pkg"
echo "Built $OUT/Tutti-Installer.pkg"
