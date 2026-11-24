# Tutti

**Tutti** lets a few friends work on **the same Ableton Live song at the same time**, each on
their own computer, the way people edit a Google Doc together. Notes, clips, knobs, mixer moves,
new tracks and recordings show up for everyone within a second.

> *Tutti*: Italian for "all together", the marking in sheet music where everyone plays.

## Install

| | |
| --- | --- |
| **Mac** (Apple Silicon) | [Tutti-Installer.pkg](https://github.com/dylancleverdon/Baastik-Labs/releases/download/tutti-channel/Tutti-Installer.pkg). The first time only, macOS asks you to approve it: **System Settings > Privacy & Security > Open Anyway**. No admin password needed. |
| **Windows** | [Tutti-Installer.exe](https://github.com/dylancleverdon/Baastik-Labs/releases/download/tutti-channel/Tutti-Installer.exe). If Windows says it protected your PC, click **More info > Run anyway**. It asks for admin once, to put Tutti's helper plugin where Live finds plugins. |

Then, once (step by step, with troubleshooting: [docs/SETUP.md](docs/SETUP.md)):

1. **Tailscale.** Install [Tailscale](https://tailscale.com/download) and sign in. One person
   invites the others to their Tailscale network (free for up to 6 people). Tutti uses it to
   reach your friends privately; your songs never go through anyone's server.
2. **Live.** In Live 12, open **Settings > Link, Tempo & MIDI** and pick **Tutti** as a Control
   Surface (leave Input and Output empty). If it isn't in the list, restart Live once.
3. Open Tutti (four overlapping circles, in the Dock or taskbar). It shows a tick next to both.

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
- **Save (Cmd+S / Ctrl+S)** shares the rest: automation, groups, adding plugins, plugin presets
  and patches, and timeline edits a Live version can't copy live. See [Saving](#saving).
- **Player 1** wins when two people change the same thing at the same moment. Anyone can **Ask
  to be Player 1**; Player 1 clicks **Allow**. If Player 1's computer drops out, the next person
  in line takes over after about 10 seconds (with two people, the other gets a **Take over**
  button).
- **Play together:** everyone who's in starts from the same bar at the same moment, for listening
  together on a call. It's opt-in: by default Tutti asks you first, and you can set it to join
  automatically or never.
- **Ideas:** select a track and click **Try an idea**: Tutti copies it in your colour, muted for
  everyone else. Others solo it to listen; Player 1 accepts (it replaces the original) or rejects.
- **Smoother sessions:**
  - **Always let in:** trust someone once and their joins are approved automatically
    (Settings lists them, with **Forget trusted friends**).
  - **Rejoining:** if your Tutti, computer or network drops out, it joins the session again by
    itself (unless you clicked **Leave session**).
  - **Continue <song>:** on the start screen, reopens the last song you were in as a new
    session, from your copy.
  - **Plugin check:** when you join (or an update arrives), Tutti lists plugins in the song
    that won't load here, including the wrong format (for example VST3 on one computer and
    only the Audio Unit on the other).
  - **Go to:** next to each person: jumps your Live to their track (and bar, when you're
    stopped).
  - **Recent:** the last few things that happened (saves, joins, new audio, plugins).
  - **Getting saves by itself:** when someone saves, Tutti loads the new version once you
    stop playing. If you have plugin changes that aren't shared yet, it saves yours first so
    both are combined. Automation you changed but didn't save is replaced, so save after
    automation edits (or turn this off in Settings).
- **Suggestion box:** in the app's footer. Ideas and problems go straight to the team's sheet.
  If something went wrong, click **Send logs**: it attaches Tutti's and Live's recent logs.
- **Menus:** on a Mac, Tutti's menu bar (Session, View) and its menu bar icon have the main
  actions; ⌘1, ⌘2, ⌘3 and ⌘, switch between Session, Versions, Suggestion box and Settings.

### Saving

- **Where:** always just **Cmd+S / Ctrl+S in Live**. During a session Live has Tutti's copy
  open, `Music/Tutti/<song>/<song>.als`, which Tutti sets up as a Live Project, so Live saves
  it in place and records into its `Samples/Recorded` folder. Don't use *Save As*. (If you do
  and save inside that folder, Tutti follows the new file.)
- **What it shares:** what doesn't travel live: adding plugins, plugin presets, automation,
  groups. Notes, clips, recordings, tracks and knobs already reach everyone without a save.
- **What happens:** Tutti sends your save to Player 1, who combines it with anyone else's
  (track by track) into the new version of the song.
- **Getting others' saves:** you see **"<name> saved changes"**. Click **Get the changes**, or
  save yourself first if you've changed automation or plugin settings (yours are combined in).
  Tutti then reopens the song in Live (it alternates between `<song>.als` and
  `<song> (Tutti).als`, because Live can't reload a file it has open). It never does this while
  you're playing or recording: the update waits until you stop.
- **Save & share:** Tutti can press Save in Live for you (the **Save & share now** button, also
  in the menus), and by default it does so by itself a few seconds after you add or tweak a
  plugin, while Live is in front and not playing. A save carries each plugin's whole current
  sound (a Serum patch included), so nobody needs the preset file. On a Mac this needs a
  one-time permission (macOS asks; or **Settings > Allow Tutti to press Save...**). Turn it off
  in **Settings > Save for me after plugin changes**.
- **Reminders:** after you add or change a plugin Tutti shows **"Not shared yet"**, and the
  others see **"<name> added <plugin>"**. Anyone who isn't Player 1 can also click **Reload the
  latest saved version** at any time.

### Your copy of each song

Everyone who has been in a session keeps their own copy in `Music/Tutti/<song>`, with every
version and all its audio, so the songs live on all your computers and there's no cloud to set
up. Open it in Live any time, even with Tutti closed. It's as up to date as your last save or
update; edits you make outside a session stay on your computer, and **Share a song...** with that
copy starts a new session from it. For an extra copy, **Settings > Backups** can copy every
version to a folder such as an external drive while you're Player 1.

### Plugins

Live's API can't read a plugin's internal patch, so plugin sounds travel with saves (which Tutti
can make for you, see [Saving](#saving)). Plugins that list their presets inside Live switch
presets live for everyone; knobs Live shows for a plugin move live too. Use the same plugin
format on Mac and Windows (VST3 on both). Everyone
should own the plugins a song uses; for plugins only some people own, Tutti's Stand-in plugin
plays a shared recording of that track for the people who don't.

## Updates

Tutti updates itself: it checks every hour, downloads only from this repository's `tutti-*`
releases, checks the download's SHA-256, and installs when you're not in a session. **Settings**
shows the version and what's new, and has **Check now**, **Roll back** and an auto-update switch.

## Uninstall

- **Mac:** quit Tutti (⌘Q), then delete `~/Applications/Tutti.app`,
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

Every push to the `tutti` branch builds for Apple Silicon Macs and Windows, runs the checks that
match what changed and, if they pass, publishes `tutti-v<version>` and refreshes the
`tutti-channel` release (prereleases, never marked "Latest"). Docs-only changes publish nothing.
