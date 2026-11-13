#!/bin/bash
# Assembles Cymatica.app from release binaries.
#   build-app.sh <version> <build> <binary> <out-dir>
# <binary> is the (universal) cymatica executable. Produces <out-dir>/Cymatica.app, ad-hoc signed.
set -euo pipefail
VERSION="$1"; BUILD="$2"; BIN="$3"; OUT="$4"
HERE="$(cd "$(dirname "$0")" && pwd)"
APP="$OUT/Cymatica.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/Cymatica"
chmod +x "$APP/Contents/MacOS/Cymatica"
sed -e "s/@VERSION@/$VERSION/g" -e "s/@BUILD@/$BUILD/g" "$HERE/Info.plist.in" > "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"
cp "$HERE/sign-local.sh" "$HERE/uninstall.sh" "$APP/Contents/Resources/"
chmod +x "$APP/Contents/Resources/"*.sh

# icon: AppIcon.png (1024x1024) -> AppIcon.icns
ICONSET="$(mktemp -d)/AppIcon.iconset"
mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
  sips -z $s $s "$HERE/AppIcon.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  d=$((s * 2))
  sips -z $d $d "$HERE/AppIcon.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"

codesign --force --sign - --identifier com.baastiklabs.cymatica "$APP"
codesign --verify --strict "$APP"
echo "built $APP ($VERSION, build $BUILD)"
