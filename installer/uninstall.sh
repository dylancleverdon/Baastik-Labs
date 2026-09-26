#!/bin/bash
# Removes Norg and its background updater. Your saved Norg programs and sample libraries in
# ~/Library/Application Support/Norg are kept.
#
#   curl -fsSL https://github.com/dylancleverdon/Baastik-Labs/releases/download/norg-channel/uninstall.sh | bash
set -u

/bin/launchctl bootout "gui/$(id -u)/com.baastiklabs.norg.updater" 2>/dev/null || true
rm -f  "$HOME/Library/LaunchAgents/com.baastiklabs.norg.updater.plist"
rm -rf "$HOME/Library/Audio/Plug-Ins/Components/Norg.component" \
       "$HOME/Library/Audio/Plug-Ins/VST3/Norg.vst3" \
       "$HOME/Applications/Norg.app"

SUPPORT="$HOME/Library/Application Support/Norg"
rm -rf "$SUPPORT/NorgUpdater" "$SUPPORT/installed.json" "$SUPPORT/updater.json" \
       "$SUPPORT/previous" "$SUPPORT/staging"

pkgutil --volume "$HOME" --forget com.baastiklabs.norg >/dev/null 2>&1 || true
/usr/bin/killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Norg has been removed."
