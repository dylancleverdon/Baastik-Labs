# Tutti

**Tutti** lets a few friends work on **the same Ableton Live song at the same time**, each on
their own computer, the way people edit a Google Doc together. Notes, clips, knobs, mixer moves,
new tracks and recordings show up for everyone within a second.

> *Tutti*: Italian for "all together", the marking in sheet music where everyone plays.

## Install

| | |
| --- | --- |
| **Mac** | [Tutti-Installer.pkg](https://github.com/dylancleverdon/Baastik-Labs/releases/download/tutti-channel/Tutti-Installer.pkg). The first time only, macOS asks you to approve it: **System Settings > Privacy & Security > Open Anyway**. No admin password needed. |
| **Windows** | [Tutti-Installer.exe](https://github.com/dylancleverdon/Baastik-Labs/releases/download/tutti-channel/Tutti-Installer.exe). If Windows says it protected your PC, click **More info > Run anyway**. It asks for admin once, to put Tutti's helper plugin where Live finds plugins. |

Then, once:

1. **Tailscale.** Install [Tailscale](https://tailscale.com/download) and sign in. One person
   invites the others to their Tailscale network (free for up to 6 people). Tutti uses it to
   reach your friends privately; your songs never go through anyone's server.
2. **Live.** In Live 12, open **Settings > Link, Tempo & MIDI** and pick **Tutti** as a Control
   Surface (leave Input and Output empty). If it isn't in the list, restart Live once.
3. Open Tutti (the four circles in your menu bar or tray). It shows a tick next to both.

Everyone in a session needs the **same Live 12 update**, and the same third-party plugins where
possible (see "Plugins" below).

## Use it

- **Start a session:** *Share a song...* and pick a Live Set. Tutti makes its own copy (your
  original is never touched) in `Music/Tutti` and opens it in Live. You're **Player 1**.
- **Join:** friends' sessions appear in Tutti automatically. Click **Join**; Player 1 lets you
  in. Tutti downloads the song (compressed previews of the audio first, so you can start in
  seconds, then full quality in the background) and opens it in your Live.
- **Edit.** Anything Live lets Tutti see is shared instantly: notes, clips (Session and
  Arrangement), tracks, devices and their knobs, mixer, tempo, scenes, locators, recordings and
  samples. Solo, record-arm, your loop region and your view stay yours.
- **Save (Cmd+S / Ctrl+S)** shares the rest: automation, groups, plugin presets and patches,
  and timeline edits a Live version can't copy live. Everyone else gets them on their next save
  ("Update available").
- **Player 1** wins when two people change the same thing at the same moment. Anyone can **Ask
  to be Player 1**; Player 1 clicks **Allow**. If Player 1's computer drops out, the next person
  in line takes over after about 10 seconds (with two people, the other gets a **Take over**
  button).
- **Play together:** everyone who's in starts from the same bar at the same moment, for listening
  together on a call. It's opt-in: by default Tutti asks you first, and you can set it to join
  automatically or never.
- **Ideas:** select a track and click **Try an idea**: Tutti copies it in your colour, muted for
  everyone else. Others solo it to listen; Player 1 accepts (it replaces the original) or rejects.
- **Suggestion box:** in the app's footer. Ideas and problems go straight to the team's sheet.

### Plugins

Live's API can't read a plugin's internal patch, so plugin sounds travel with saves. Everyone
should own the plugins a song uses; for plugins only some people own, Tutti's Stand-in plugin
plays a shared recording of that track for the people who don't.

## Updates

Tutti updates itself: it checks every hour, downloads only from this repository's `tutti-*`
releases, checks the download's SHA-256, and installs when you're not in a session. **Settings**
shows the version and what's new, and has **Check now**, **Roll back** and an auto-update switch.

## Uninstall

- **Mac:** quit Tutti from the menu bar, then delete `~/Applications/Tutti.app`,
  `~/Library/Application Support/Tutti`, `~/Library/LaunchAgents/com.baastiklabs.tutti.plist` and
  the `Tutti` folder in Live's `User Library/Remote Scripts`.
- **Windows:** Settings > Apps > Tutti > Uninstall.

Your songs in `Music/Tutti` are kept either way.

## How it works

```
Live 12 + Tutti Remote Script  <-localhost->  Tutti app  <-Tailscale->  friends' Tutti apps
(sees and applies edits)                      (sessions, Player 1, audio, updates)
```

Details and the reasoning behind every choice are in [docs/DESIGN.md](docs/DESIGN.md).

## Build from source

Requires CMake 3.22+, a C++20 compiler and Python 3. JUCE, Catch2 and nlohmann/json are fetched
automatically.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
python3 -m pytest remote_script/tests -q
python3 tools/e2e/run_e2e.py build
```

Every push to the `tutti` branch builds and tests on Linux, macOS and Windows and, if everything
passes, publishes `tutti-v<version>` and refreshes the `tutti-channel` release (prereleases,
never marked "Latest").
