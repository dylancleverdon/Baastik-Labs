#!/bin/bash
# Downloads free sampled pianos for Norg's Grand piano into Norg's samples folder.
#
#   curl -fsSL https://github.com/dylancleverdon/Baastik-Labs/releases/download/norg-channel/fetch-samples.sh | bash
#
# Libraries:
#   Salamander Grand Piano V3 - Alexander Holm, CC-BY 3.0 (Yamaha C5, 16 velocity layers, ~700 MB)
#
# Norg picks it up automatically: open the Piano section, choose Grand, and it plays the samples.
# The first time it loads, Norg converts the FLAC files into a fast-loading cache (about a minute).
set -euo pipefail

case "$(uname -s)" in
    Darwin) SAMPLES="$HOME/Library/Application Support/Norg/Samples" ;;
    *)      SAMPLES="${XDG_DATA_HOME:-$HOME/.local/share}/Norg/Samples" ;;
esac

fetch_library () {
    local name="$1" url="$2"
    local dest="$SAMPLES/$name"

    if [ -d "$dest" ] && ls "$dest"/*.sfz >/dev/null 2>&1; then
        echo "$name is already installed."
        return
    fi

    echo "Downloading $name..."
    local tmp
    tmp="$(mktemp -d)"
    curl -fL --progress-bar -o "$tmp/library.tar.gz" "$url"

    echo "Unpacking..."
    mkdir -p "$tmp/unpacked"
    tar -xzf "$tmp/library.tar.gz" -C "$tmp/unpacked"

    mkdir -p "$SAMPLES"
    rm -rf "$dest"
    mv "$tmp/unpacked/"* "$dest"
    rm -rf "$tmp"
    echo "Installed $name in $dest"
}

fetch_library "Salamander Grand Piano" \
    "https://github.com/sfzinstruments/SalamanderGrandPiano/archive/refs/heads/master.tar.gz"

echo
echo "Done. In Norg, turn the Piano section on and choose Grand."
echo "(Salamander Grand Piano by Alexander Holm, licensed CC-BY 3.0.)"
