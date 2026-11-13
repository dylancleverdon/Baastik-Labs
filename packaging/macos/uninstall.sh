#!/bin/bash
# Removes Cymatica, its settings, presets, logs and local signing identity.
#
#   curl -fsSL https://github.com/dylancleverdon/Baastik-Labs/releases/download/cymatica-channel/uninstall.sh | bash
set -u
osascript -e 'tell application id "com.baastiklabs.cymatica" to quit' >/dev/null 2>&1 || true
pkill -x Cymatica >/dev/null 2>&1 || true
SUPPORT="$HOME/Library/Application Support/Cymatica"
KC="$SUPPORT/signing/cymatica-signing.keychain-db"
if [ -f "$KC" ]; then
  security delete-keychain "$KC" >/dev/null 2>&1 || true
fi
rm -rf "$HOME/Applications/Cymatica.app" "/Applications/Cymatica.app" 2>/dev/null
rm -rf "$SUPPORT" "$HOME/Library/Logs/Cymatica"
tccutil reset Microphone com.baastiklabs.cymatica >/dev/null 2>&1 || true
echo "Cymatica has been removed. (Screenshots in ~/Pictures/Cymatica were kept.)"
