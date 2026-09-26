#!/bin/sh
# Removes Text To Synth: unregisters it from Claude, then deletes the app.
# Your presets in Serum's User/Text To Synth folder are kept.
DIR="/Library/Application Support/Baastik Labs/Text To Synth"
"$DIR/TextToSynth" --unregister
echo "Removing $DIR (asks for your password)..."
sudo rm -rf "$DIR"
sudo pkgutil --forget com.baastiklabs.texttosynth >/dev/null 2>&1
echo "Text To Synth is uninstalled. Restart Claude to finish."
