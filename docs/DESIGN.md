# Tutti design

Tutti lets a few friends (3-4, up to 6) edit **one Ableton Live 12 Set together, live**, from
their own computers (Macs and Windows PCs), the way people edit a Google Doc. This file records
the decisions behind it. Keep it current when a decision changes.

## Decisions (from the planning conversation)

| Topic | Decision |
| --- | --- |
| Build or buy | Build our own (SameSet exists; we want our own workflow, free). |
| How people work | Mostly together, live, on a call. Edits should show up within a second. |
| Live | Everyone has **Live 12 Suite**. Everyone in a session must run the same Live 12 minor version. |
| Computers | Macs with Apple Silicon (no Intel Macs) and Windows PCs. No Linux. |
| What syncs | MIDI, arranging, mixing/sound design, recording/samples. Mostly **Arrangement View**. |
| Plugins | People own **different** third-party plugins: tracks using a plugin someone lacks get a **shared bounce** (Tutti Stand-in plugin). |
| Conflicts | Everyone edits everything. One person is **Player 1** (the host). Player 1's edits win clashes. |
| Network | **Tailscale** (free plan: up to 6 users, non-commercial). Player 1's computer is the server. No cloud. |
| Audio files | Stored on the members' computers, sent peer to peer. **Preview first** (compressed), full quality in the background, swapped in place. |
| Switching Player 1 | Anyone can **ask**; Player 1 clicks Allow (or hands it over directly). |
| Player 1 drops out | **Next in line takes over** after ~10 s if they can still reach a majority of the group (2-person sessions: a Take over button instead). The old Player 1 rejoins as a guest. |
| Extras | See who's where; **Play together** (opt-in, never forced: everyone plays on their own while editing); version history; **suggestion mode** (idea copies of a track, Player 1 accepts/rejects). |
| Backups | Every member keeps each song, its versions and audio locally (no cloud). Optional extra copy of every version into a chosen folder while Player 1. |
| Opening songs | Tutti waits until Live has really loaded the file it opened (the Remote Script's `doc` token in its hello changes) before reading or binding, so it never binds an Untitled Set whose track names happen to match. |
| Updates | Silent, Norg-style: SHA-256 verified downloads from this repo's `tutti-*` releases, roll back, what's new, auto-update toggle. Never interrupts a session. |
| Feedback | A **suggestion box** in the app posts to the Google Sheet "suggestions sheet tutti" through a tiny Apps Script web app (`suggestions/Code.gs`). Queued offline until the endpoint is configured. |

## Pieces

```
Live 12 (each computer)                     Tutti app (each computer)                 other computers
┌──────────────────────────┐  localhost   ┌───────────────────────────────┐  Tailscale  ┌──────────┐
│ Tutti Remote Script (py) │◀────────────▶│ Engine: replica, sequencer (P1)│◀──────────▶│ Tutti app│
│  observes + applies       │   TCP JSON  │ session/roles, store, history  │  TCP JSON   └──────────┘
│ Tutti Stand-in (plugin)  │◀────────────▶│ updater, suggestion box, UI    │
└──────────────────────────┘              └───────────────────────────────┘
```

- `remote_script/Tutti` (Python, inside Live): the only part that talks to Live's API. It watches
  the Set, turns changes into ops, applies everyone else's ops, reports presence, and runs Play
  together. Live forbids changing the Set inside a listener callback, so all writes happen on the
  script's tick (`update_display`, ~10 Hz). Written for Python 3.7+ (Live 12 ships 3.11).
  Live also answers some questions with an error rather than a value (the Main, group and return
  tracks raise if asked for arrangement clips, even via `hasattr`), so the script never asks them,
  checks attributes with `livemodel.has`, and reports a failed read (`capture_failed`) instead of
  leaving the app waiting. The fake Live in the tests enforces the same rules. The app tells
  people to restart Live when Live still runs an older script than the one it ships.
- `app/` (C++/JUCE): a normal app (Dock icon on macOS, taskbar on Windows) plus a menu bar / tray
  icon. It starts at login and keeps running when its window is closed, so a session never ends
  because a window did; the Dock icon or opening Tutti again brings the window back. Hosts or joins
  sessions, keeps the document replica, sequences edits when it is Player 1, stores audio and
  versions, updates itself.
- `core/` (C++, juce_core + nlohmann/json): all logic that can be tested without Live, a GUI or a
  network: document, ops, sequencer, replica, session state machine, protocol framing, .als
  handling, store, versions, updater decisions, suggestion queue. Compiled into each consumer
  (INTERFACE library, like Norg's updater core) so JUCE modules are never linked twice.
- `plugin/` (C++/JUCE): **Tutti Stand-in**, a VST3/AU effect placed right after a plugin that some
  members don't own. On owners' computers it records that track while they play it; elsewhere it
  plays the shared recording in time and shows when it is out of date.

## The document

The shared state is a tree of entities: `{id, type, parent, props}`. IDs are random strings made
by whoever creates the entity (the Remote Script for Live objects). The root is `song`.

| Type | Parent | Props (synced) |
| --- | --- | --- |
| song | - | `tempo`, `sig_n`, `sig_d`, `swing` |
| track | song | `kind` (midi/audio/return/master/group), `pos`, `name`, `color`, `mute`, `vol`, `pan`, `send<i>` |
| device | track / chain | `cls`, `name`, `pos`, `on`, `p<i>` (parameter values), `plugin` (descriptor for plugins), `preset` (index, for plugins that list presets in Live) |
| chain | device | `pos`, `name`, `color`, `mute`, `vol`, `pan` |
| scene | song | `pos`, `name`, `color`, `tempo` |
| clip | track | `where` (arr/session), `start`, `len` (arrangement), `scene` (session), `name`, `color`, `muted`, `loop`, `ls`, `le`, `sm`, `em`, audio: `file` (sha256), `warp`, `wmode`, `wm` (warp markers: [beat, seconds]), `gain`, `pc`, `pf` |
| note | clip | `p` pitch, `t` start, `d` duration, `v` velocity, `m` mute, `pr` probability, `vd`, `rv` |
| cue | song | `time`, `name` |

- Order: `pos` is a fractional key (double; ties broken by id). Each replica inserts new tracks,
  devices and scenes where the sorted keys say, so order converges everywhere.
- **Personal, never synced:** solo, record-arm, monitoring, input routing, fold state, selection,
  playhead, loop region, metronome, punch/record modes, view. Soloing to listen never solos it for
  anyone else.
- Ops: `create(id, type, parent, props)`, `remove(id)` (removes the subtree), `set(id, {k: v})`.

## Sync: two lanes

1. **Instant lane** (ops through Player 1): everything Live's API can observe and replay. The
   Remote Script probes the API at startup (`caps`) and adapts: e.g. arrangement clip creation
   needs Live 12.1.10+, native device insertion 12.3+, audio clips from files 12.0.5+.
2. **Save lane** (⌘S): what the API can't reach: arrangement automation, groups, plugin
   presets/patches, adding plugins, track reordering, and any timeline edit a Live version can't
   replay. Saving sends the Set file to Player 1, who merges it (track-level three-way merge
   against the saver's base, Player 1 wins clashes) into a new snapshot. Everyone else sees
   "Update available"; their next ⌘S merges their own changes too, then Tutti reloads the Set for
   them, keeping their personal state (view, solo, arm, loop...). Every snapshot is a version, so
   nothing is ever lost.

### Sequencing and conflicts (Player 1 = host)

- Clients apply their own edits immediately (optimistic) and send them in batches with a client
  sequence number. Player 1 validates and assigns a global sequence number, then broadcasts.
- `set` on a missing entity, `create` under a missing parent: rejected, the author reconciles.
- Player 1 wins: a guest's `set` of a property Player 1 changed in the last 2 s is dropped, and a
  guest's `remove` of something Player 1 touched (itself or anything inside it) in the last 2 s is
  rejected. Otherwise order of arrival wins (last writer).
- While a client has a pending `set` for a property, it ignores remote sets of that property (its
  own will land later and win, or be rejected and reconciled).
- Reconcile = "make my Live match the confirmed document for these entities".

### Snapshots, binding and joining

- A snapshot is an `.als` + a **structure map** (Live structure path -> entity id, made by the
  saver's Remote Script, e.g. `T3`, `T3/D0`, `T3/A5`, `R1`, `S4`; note ids per clip in canonical
  order) + the document at a sequence number + content hashes of every referenced audio file.
- Files a Set needs are the ones under `SampleRef` (samples) and `MxPatchRef` (Max for Live
  devices). Other `FileRef`s only say which preset or groove something was loaded from; they're
  left alone.
- Joining: download the snapshot (audio previews first), rewrite file paths to the local store,
  open it in Live, then the Remote Script binds ids by path and reconciles the whole Set with the
  document (replaying everything since the snapshot).
- Tutti always works on its own copy of a song under `~/Music/Tutti` (Windows:
  `%USERPROFILE%\Music\Tutti`); it never modifies the original project Player 1 started from.

## Sessions and Player 1

- Every app listens on its Tailscale address (TCP 47380). Discovery uses `tailscale status
  --json` to list online peers, then asks each for the session it hosts. On macOS the tool is the
  Tailscale app's own executable, which only acts as the tool from a terminal, so Tutti sets
  `TAILSCALE_BE_CLI=1`; each check has an 8 s limit. If the tool can't answer, Tutti takes its
  address from the network interfaces (100.64.0.0/10) and dials the friends it has met before.
- Messages are length-prefixed JSON frames; binary file chunks use a separate frame kind.
- Session state: members, Player 1, **epoch** (bumped on every Player 1 change), line of
  succession (join order, editable by Player 1), last sequence number. Stale-epoch messages are
  ignored.
- Ask then approve: a guest sends `request_p1`; Player 1 sees Allow / Not now. Handoff: Player 1
  stops sequencing, flushes, sends `handoff` (new P1, epoch+1, last seq); the new P1 confirms and
  broadcasts `p1_changed`. Takes about a second; nobody's Live reloads.
- Failover: heartbeats every second. If Player 1 is silent for 10 s, the next live member in line
  asks the others; it takes over when it hears back from a majority of the session (counting
  itself). Each member keeps the full confirmed op log since the last snapshot, so the new P1
  first catches up from whoever has the most. Two-person sessions never take over automatically
  (a blip in your own internet would split the session): the remaining member gets Take over.
- A returning old Player 1 sees the higher epoch and rejoins as a guest. Its unconfirmed edits are
  resubmitted like any guest's; anything rejected is reconciled.

## Audio

- Content-addressed store (SHA-256) per computer. Files are added by clone/hard link when possible.
- Anyone who has a file can send it. Order: previews of files in view, previews, full files.
- Preview: Ogg Vorbis (~128 kbps, about 1/15 of a 24-bit WAV). The receiver decodes it to a WAV
  with exactly the original sample rate, channel count and length (MP3/Vorbis padding would
  otherwise shift edits), uses that until the full file arrives, then swaps it in place (Live's
  API can't change a clip's file, so the clip is recreated at the same spot with the same settings).

## Play together (opt-in)

Anyone can press Play together from a position. It is relayed through Player 1 with a start time
~600 ms ahead on Player 1's clock (members estimate their clock offset from heartbeats). Each
member's setting decides: join automatically, ask (default), or never. Ableton Link only works on
a local network, which is why Tutti times this itself.

## Suggestion mode

A member turns on Suggest for a track: Tutti duplicates it as "<name> (<who>'s idea)", in the
member's colour, muted for everyone (others audition with their personal solo). Player 1 accepts
(the idea replaces the original part) or rejects (the copy is removed).

## Shared bounces (Tutti Stand-in)

Each member's Remote Script reports which plugins Live knows (from its browser). A member who owns
a plugin some others lack puts the Stand-in right after it (it's a third-party plugin to Live, so
it travels with the next save). Each Stand-in has a random **Link** id in its second parameter
(`p1` of its device entity), which is how the app matches the plugin instance on this computer to
the device in the shared song. Owners' Stand-ins record (with song position and tempo) while they
play; each stretch of playback becomes a segment in the device's `bounce` prop
(`{file, start, len, tempo, fp, by, byName}`), shared like any audio file. Non-owners' Stand-ins
play the segments in time. `fp` fingerprints the track's clips, notes and the devices before the
Stand-in; when it no longer matches, the segment still plays but shows as out of date until an
owner plays that part again.

## Updates

`tutti-channel` release holds `tutti-update.json` (version, notes, per-platform payload URL,
SHA-256, size, suggestion box endpoint). The app checks at launch and hourly, downloads only from
`https://github.com/dylancleverdon/Baastik-Labs/releases/download/tutti-`, verifies SHA-256, and
installs when no session is running (relaunching itself). The previous version is kept for Roll
back (the rolled-back-from build is then skipped). The Remote Script is installed or updated into
Live's User Library by the app; Live picks it up on its next launch.

- Relaunch: the old Tutti closes its sockets, starts the new one with `--updated` and quits. A
  Tutti started with `--updated` waits (up to 30 s) for the old one's single-instance lock, then
  takes it, so it never mistakes the quitting Tutti for a running one.
- Windows: normal users can't add or remove folders in `Common Files\VST3`, so the Stand-in is
  updated file by file inside its bundle (the installer makes the bundle writable for Users);
  that also works while Live has the plugin open. If an install fails, everything is put back and
  the same download is retried ten minutes later. Progress is logged to `updater.log`.
- CI's self-update tests build an old Tutti (build 0) from the commit under test and let it update
  itself from the published `tutti-channel` release on macOS and Windows. They don't gate
  publishing, so a broken published build can always be replaced.

## Suggestion box

`suggestions/Code.gs` is an Apps Script web app bound to the sheet. The app POSTs JSON
`{from, kind, text, version, os, live}`; the script appends a row. The endpoint URL ships in the
update manifest (`suggestions.endpoint`), so it can be set without a new build. Until it is set,
suggestions wait in a local queue and are sent later.
