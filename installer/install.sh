#!/bin/bash
# Installs Norg from the Terminal, skipping the "unidentified developer" prompt:
#
#   curl -fsSL https://github.com/dylancleverdon/Baastik-Labs/releases/download/norg-channel/install.sh | bash
#
# It downloads the same installer package as the download link and installs it into your user
# folders (no admin password needed).
set -euo pipefail

PKG_URL="https://github.com/dylancleverdon/Baastik-Labs/releases/download/norg-channel/Norg-Installer.pkg"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading Norg..."
curl -fL --progress-bar -o "$TMP/Norg-Installer.pkg" "$PKG_URL"

echo "Installing Norg into your user folders..."
/usr/sbin/installer -pkg "$TMP/Norg-Installer.pkg" -target CurrentUserHomeDirectory

echo
echo "Norg is installed. Open (or restart) your DAW and look for Norg by Baastik Labs."
echo "It will keep itself up to date in the background."
