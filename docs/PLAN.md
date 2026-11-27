# CueMix plan

CueMix lets everyone in the studio have their own headphone mix and fine-tune it themselves,
without the engineer building sends for each person. It runs inside Ableton Live. **Mac first**
(the proof of concept), then Windows (the studio PC).

## The problem

The audio interface already has enough headphone outputs. What's missing is a fast, clear way to
build and adjust a separate mix for each person: drummer, singer, bass and so on. Today that means
juggling return tracks and sends in Live while everyone waits.

## Decisions so far

| Topic | Decision | Why |
|---|---|---|
| Host | Ableton Live (Mac now, Windows later) | The studio's DAW |
| Shape | Two plugins plus a small companion app | A plugin can't update itself while Live has it loaded |
| Audio path | Inside Live, through shared memory; no virtual audio cable | No added driver, clock or buffer, so no extra latency |
| Phones as controllers | No | Not wanted |
| Buffer size | Follows Live's setting (small for tracking, large for mixing) | The plugins add at most one buffer on top of Live's own |
| Platform code | JUCE (AU + VST3 on Mac, VST3 on Windows), C++20, CMake | Same stack as Norg and Tutti in this repo |
| Branch | `cuemix`, its own history (nothing from Norg or other programs) | Repo convention: one long-lived branch per program |

## How it works

```
Live track "Drums"  ─► [CueMix Send] ─┐   (audio passes through untouched)
Live track "Vox"    ─► [CueMix Send] ─┤
Live track "Click"  ─► [CueMix Send] ─┼──► shared memory bus (32 channels)
Live track "Bass"   ─► [CueMix Send] ─┘            │
                                                   ▼
                         Live MIDI track: [CueMix] instrument, 8 stereo outputs
                           Cue 1 ─► Live "Audio To: Ext. Out 3/4" ─► headphones 1
                           Cue 2 ─► (audio track: Audio From CueMix | Cue 2) ─► Ext. Out 5/6
                           ...
                                                   ▲
                         CueMix app (menu bar / tray): updates, suggestions, logs,
                         later a big mixing window for a second screen
```

### CueMix Send (audio effect)
- Put it on every track or group that should be available in the cue mixes (Drums, Vox, Click,
  Bass, Keys...). It passes audio through unchanged.
- Live's devices sit before the track fader, so cue mixes are **pre-fader**: the engineer's main
  mix never changes what the band hears.
- Takes its name from the track (VST3/AU track info where Live provides it). The name can be
  edited in the plugin.
- Has a stable channel id saved in the Set, so each person's mix settings come back next session.
  A duplicated track gets a new id automatically.
- Mono or stereo.

### CueMix (instrument, multi-output)
- An instrument because Live only gives instruments multiple outputs. It ignores MIDI.
- 8 stereo outputs named "Cue 1" to "Cue 8", each routed in Live to an interface output pair.
- For each mix and each channel: level fader (−∞ to +10 dB), pan (balance), mute, solo.
- For each mix: name ("Drummer"), master fader, mute, a safety limiter (−1 dBFS, on by default,
  no added latency) and an output meter.
- **Mix view**: pick a person's mix and get one fader strip per channel with meters.
  **Matrix view**: every channel against every mix on one screen, for quick overall changes.
- **Presets per person**: save a mix and load it in another Set. Presets match channels by name,
  so "Vox" finds "Vox" again.
- **Latency readout**: buffer size in samples and ms, plus the measured cue delay (0 or 1 buffer).
  A warning appears when recording at a large buffer ("1024 samples = 21 ms: the singer will
  hear an echo").
- Its settings are saved in the Live Set.

### The shared bus
- One block of shared memory (`/cuemix.v1` on Mac; `Local\cuemix.v1` on Windows) holding 32
  channel slots. Each slot has a name, stable id, sample rate and a 16384-frame stereo ring buffer.
- It's shared memory rather than a variable in the plugin because the Send and the mixer are
  separate plugin binaries, and Live may load plugins in separate processes.
- Lock-free: the audio thread never locks, allocates or does I/O.
- **Timing**: Live may run a Send before or after the mixer in the same audio callback, and the
  order can change. The mixer starts each channel on the newest audio it can see. If the order
  flips, that channel is silent for one buffer, then settles exactly one buffer behind, which is
  always available. The cue delay is therefore **zero or one buffer, never more, and never
  drifts**. At 128 samples / 48 kHz, one buffer is 2.7 ms.
- Slots left behind by a crashed Live are reclaimed (the owner process is gone).
- New channels start silent until the mixer has assigned their gains (about 30 ms), so nothing
  blasts into anyone's ears.

### Latency budget (Live at 48 kHz)

| Live buffer | One buffer | Live round trip (approx.) | CueMix adds |
|---|---|---|---|
| 64 | 1.3 ms | 3–5 ms | 0–1.3 ms |
| 128 | 2.7 ms | 5–8 ms | 0–2.7 ms |
| 256 | 5.3 ms | 10–13 ms | 0–5.3 ms |
| 1024 (mixing) | 21 ms | not for tracking | n/a |

Tip for singers: hearing your own voice through the interface's direct monitoring is 0 ms. The
rest of the cue mix still comes from CueMix.

## Companion app: CueMix (menu bar on Mac, tray on Windows)

It starts at login and keeps running when its window is closed. It contains no audio processing.

### Auto-updater (modelled on Tutti's)
- The rolling `cuemix-channel` release holds `cuemix-update.json`: version, build, notes, and for
  each platform the payload URL, SHA-256 and size, plus the suggestion box endpoint.
- Checks at launch and every hour. Downloads only from
  `https://github.com/dylancleverdon/Baastik-Labs/releases/download/cuemix-`, over HTTPS, and
  installs only if the SHA-256 matches.
- What's new, an auto-update on/off switch, and **Roll back** (the previous build is kept, and the
  build rolled back from is then skipped).
- Never installs while Live is running with CueMix loaded. It waits, then offers "Update when Live
  closes".
- **Windows protection against deleting itself**:
  - A running `.exe` can't be overwritten but can be renamed. The app moves itself aside to
    `CueMix.old.exe`, puts the new one in place, relaunches with `--updated`, and deletes the
    `.old.exe` on the next launch. The updater never deletes the program that is running.
  - The new app waits (up to 30 s) for the old one's single-instance lock, so it never mistakes the
    quitting app for a second copy.
  - `Common Files\VST3` only lets admins add or remove folders, so the plugin bundles are updated
    **file by file inside the existing bundle**. The installer makes those bundles writable for
    Users. Files Live still has open are moved aside, not deleted, and cleaned up later.
  - Freshly downloaded files can be briefly locked by Windows Defender: retry with backoff.
  - Every install is a transaction: back up, replace, verify. If anything fails, everything is put
    back and the same download is retried ten minutes later.
  - Never deletes whole folders recursively outside its own staging and backup folders.
- On Mac it updates `~/Library/Audio/Plug-Ins/{Components,VST3}` and `~/Applications/CueMix.app`
  the same way (backup, replace, verify, roll back on failure).
- Logs to `updater.log` in CueMix's logs folder.
- CI self-update test: an old build (build 0) of the commit under test updates itself from the
  published `cuemix-channel`. It doesn't gate publishing, so a broken release can always be replaced.

### Suggestions section (with logs)
- A suggestion box in the app (and a "Suggest" button in the plugin that opens it): name,
  kind (idea / bug / other), text, and an **Attach logs** checkbox.
- Posts JSON `{id, from, kind, text, version, os, live, logs?}` to a small Google Apps Script web
  app (`suggestions/Code.gs`) bound to a Google Sheet ("suggestions sheet cuemix"). The script
  appends a row. When logs are attached, it saves them as a file in a "CueMix logs" Drive folder
  next to the sheet and puts the link in the row.
- The endpoint URL ships in the update manifest (`suggestions/endpoint.txt` → manifest), so it can
  be set or changed without a new build.
- Suggestions made offline, or before the endpoint exists, wait in a local queue and are sent
  later. Each has an id, so a row is never added twice.
- Logs are capped (most recent ~1 MB, compressed). The script caps text length.
- One-time setup by the sheet owner in the browser (about two minutes, the same steps as Tutti's
  `suggestions/README.md`). No secrets in the app, no terminal steps.

### Logging
- Plugins and app write to `~/Library/Logs/CueMix/` (Mac) and `%LOCALAPPDATA%\CueMix\Logs`
  (Windows), rotated at 1 MB.
- The audio thread never writes logs. It updates counters (underruns, resyncs, sample-rate
  mismatches) that the message thread logs once a second if they changed.
- The log records the buffer size, sample rate, Live version, the channel list and every update step.

### Later: a mixing window in the app
- Same mixer, in a large window for a touchscreen or second monitor, talking to the CueMix plugin
  through a control area added to the shared bus. MIDI controller mapping for faders.

## Installing
- **Mac**: `CueMix-Installer.pkg` (ad-hoc signed, like Norg's). It installs the AU and VST3 into
  `~/Library/Audio/Plug-Ins` and the app into `~/Applications`, and starts the app at login. The
  first time, macOS may ask to allow it under System Settings > Privacy & Security ("Open
  Anyway"). After that everything updates by itself.
- **Windows**: Inno Setup installer (like Tutti's `installer.iss`). It installs the VST3 into
  `Common Files\VST3` (writable for Users, see above), the app into Program Files and the logon task.
- Live setup steps go in `docs/SETUP.md`: rescan plugins, put Sends on tracks, add CueMix on a MIDI
  track, route Cue 1–8 to the interface outputs. A template Live Set with that routing ready can
  follow.

## CI and releases (`.github/workflows/cuemix.yml`)
- Pushes to `cuemix` and to session branches run: build (macOS universal; Windows from phase 3),
  unit tests, `auval` (aufx Send, aumu mixer), pluginval strictness 8 on both plugins, the pkg
  build and an installer smoke test.
- Pushes to `cuemix` that pass publish `cuemix-v<version>` and update `cuemix-channel`.
  Prerelease only, never "Latest", so other programs' releases are unaffected.
- `VERSION` holds MAJOR.MINOR; CI appends the run number.

## Phases

1. **Proof of concept, Mac (in progress)**
   - [x] Shared bus: shared memory, slots, name seqlock, crashed-owner recovery
   - [x] Send side: claim/release, duplicate-id handling, lock-free ring writes
   - [ ] Mix engine: read cursors (0-or-1-buffer policy), gain ramps, masters, limiter, meters
     (header written; still to do: change the limiter release to
     `gain = min(wanted, gain + (1 - gain) * releaseCoef)`, add `<algorithm>`/`<cmath>`, write
     `MixEngine.cpp`)
   - [ ] CueMix Send and CueMix plugins (JUCE), state save/restore
   - [ ] Mix view, matrix view, latency readout, presets
   - [ ] Tests (Catch2): bus across two mappings, order flips, underrun/resync, limiter ceiling,
     gains/solo, state round trip
   - [ ] Screenshot tool for the UIs, pluginval on Linux in development
   - [ ] CI on macOS: build, tests, auval, pluginval, pkg; publish on `cuemix`
   - [ ] Try it in Live on the Mac (BlackHole 16ch as a fake multi-output interface works for
     checking routing without the studio)
2. **Companion app, updater, suggestions, logs (Mac)**
   - App shell (menu bar), single instance, start at login
   - Updater with manifest, SHA-256, backup and roll back, what's new, self-update CI test
   - Suggestions box plus `suggestions/Code.gs`, offline queue, log attachments
   - Logging in plugins and app
3. **Windows**
   - Windows CI job (VST3 + app), Inno Setup installer
   - Updater's Windows rules (rename-aside, file-by-file bundle updates, Defender retries)
   - Test on the studio PC with the real interface
4. **Studio polish**
   - Talkback (a mic channel that ducks every mix while held)
   - Cue-only reverb per mix (never recorded)
   - Groups ("all drums" on one fader), "more me" knob
   - Mixing window in the app, MIDI controller mapping, template Live Set

## Open questions
- Which audio interface does the studio have? If its own mixer software (RME TotalMix, UA Console,
  MOTU CueMix) can be controlled remotely, a later phase could drive it for zero-latency mixes.
- How many people track at once (how many of the 8 mixes are needed, and is 32 channels enough)?
- Who will own the Google Sheet for suggestions?
