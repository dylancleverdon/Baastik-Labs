"""Tutti's Live-side engine.

Runs on Live's main thread, driven by tick() (~10 times a second). It:
  * mirrors the shared document and binds each entity id to a Live object,
  * turns what the person does in Live into ops for the Tutti app,
  * applies everyone else's ops to Live (always on the tick, never inside a listener),
  * reports presence and runs Play together.

Every Live call is guarded: an API this Live version lacks, or a call Live refuses, marks the
entity "stale" (the save lane fixes it) instead of breaking anything."""

import os
import time

from . import livemodel as lm
from .mirror import Mirror

SCRIPT_VERSION = "8"   # the app asks people to restart Live when its script is newer
PROTOCOL = 1
APPLY_BUDGET = 400          # entity writes per tick, so a big join doesn't freeze Live
FULL_SCAN_EVERY = 4.0       # seconds: catch changes that have no listener (warp mode, gain...)
PRESENCE_EVERY = 0.25
SETTLE = 1.5                # seconds a newly opened Set must stay unchanged before Tutti uses it


def _format_of(folder):
    name = (folder or "").lower()
    if "vst3" in name:
        return "VST3"
    if "audio unit" in name or name in ("au", "audiounits"):
        return "AU"
    if "vst" in name:
        return "VST"
    return ""


def new_id():
    return "e" + "".join("%02x" % b for b in bytearray(os.urandom(8)))


def key_of(obj):
    ptr = getattr(obj, "_live_ptr", None)
    return ("p", ptr) if ptr is not None else ("i", id(obj))


def index_of(seq, obj):
    for i, item in enumerate(seq):
        if item == obj:
            return i
    return -1


class Engine(object):
    def __init__(self, song, application, Live, bridge, log=None, show_message=None, clock=time.time, settle_first=False):
        self.song = song
        self.app = application
        self.Live = Live
        self.bridge = bridge
        self._log = log or (lambda text: None)
        self._show = show_message or (lambda text: None)
        self.clock = clock

        self.mirror = Mirror()
        self.live = {}       # entity id -> Live object
        self.idx = {}        # key_of(Live object) -> entity id
        self.notes = {}      # clip id -> {Live note id: entity id}
        self.realized = set()
        self.bound = False
        self.session = None

        self.dirty = set()
        self.out_ops = []
        self.unsent = []
        self.queue = []      # incoming work: ("apply", ops) / ("reconcile", items)
        self.stale = {}
        self._listeners = []
        self._watched = set()
        self._last_full_scan = 0.0
        self._logged = {}          # repeating problems -> when we last logged them
        self._presence_state = None
        self._presence_sent = 0.0
        self.play_at = None
        self._automation = set()   # entities whose automation was written or switched on/off
        self.doc_token = new_id()  # changes whenever Live opens another Set
        self._settle_since = None   # a Set was just opened: wait until it stops changing
        self._settle_fp = None
        if settle_first:
            self._begin_settle()    # Live may start this script while it's still opening the Set
        self.caps = self._probe_caps()

    # ------------------------------------------------------------------ logging

    def log(self, text):
        self._log("Tutti: " + text)
        if self.bridge.connected:
            self.bridge.send({"type": "log", "text": text})

    # ------------------------------------------------------------------ capabilities

    def _probe_caps(self):
        song = self.song
        tracks = list(song.tracks)
        # Ask a plain audio/MIDI track: group, return and Main tracks refuse some questions.
        plain = [t for t in tracks if not lm.safe(lambda: t.is_foldable, False)]
        any_track = plain[0] if plain else (tracks[0] if tracks else song.master_track)
        slot = None
        for t in song.tracks:
            slots = list(getattr(t, "clip_slots", []))
            if slots:
                slot = slots[0]
                break
        caps = {
            "live": [self.app.get_major_version(), self.app.get_minor_version(), self.app.get_bugfix_version()],
            "notes": lm.has(self.Live.Clip, "MidiNoteSpecification"),
            "arrangement_clips": hasattr(type(any_track), "arrangement_clips") or lm.has(any_track, "arrangement_clips"),
            "arrangement_create": lm.has(any_track, "create_midi_clip"),
            "arrangement_audio": lm.has(any_track, "create_audio_clip"),
            "duplicate_to_arrangement": lm.has(any_track, "duplicate_clip_to_arrangement"),
            "delete_clip": lm.has(any_track, "delete_clip"),
            "insert_device": lm.has(any_track, "insert_device"),
            "delete_device": lm.has(any_track, "delete_device"),
            "session_audio": slot is not None and lm.has(slot, "create_audio_clip"),
            "data": lm.has(any_track, "set_data") and lm.has(any_track, "get_data"),
            "duplicate_track": lm.has(song, "duplicate_track"),
        }
        return caps

    def live_version(self):
        return ".".join(str(v) for v in self.caps["live"])

    # ------------------------------------------------------------------ tick

    def tick(self):
        self._check_document()
        for event in self.bridge.poll():
            if event[0] == "connected":
                if self._settle_since is None:
                    self._on_connected()  # (otherwise once the Set has settled)
            elif event[0] == "disconnected":
                self._show("Tutti: lost the Tutti app")
            elif event[0] == "message":
                try:
                    self._handle(event[1])
                except Exception as e:  # never let one bad message break the script
                    self.log("error handling %s: %r" % (event[1].get("type"), e))

        if self.bound:
            try:
                mine = set(self._automation)
                self._run_queue()
                self._automation = mine  # others' edits we just applied don't count as ours
                self._process_dirty()
                now = self.clock()
                if now - self._last_full_scan > FULL_SCAN_EVERY:
                    self._last_full_scan = now
                    self._mark_everything()
                    self._process_dirty()
                self._presence()
            except Exception as e:
                self.log("tick error: %r" % (e,))
            self._flush_ops()
            if self._automation and self.bridge.connected:
                # Automation only travels with a save: tell the app so it can save for the person.
                self.bridge.send({"type": "automation", "ids": sorted(self._automation)})
                self._automation = set()
        self._play_together()
        try:
            self.bridge.flush()
        except Exception:
            pass

    def shutdown(self):
        self._unwatch_all()
        self.bridge.close()

    _last_doc_check = 0.0
    _last_fingerprint = None

    def _begin_settle(self):
        """Live is opening a Set: don't tell the app about it (so it doesn't read or bind it)
        until it has stopped changing; a half-loaded Set gets bound wrongly."""
        self._settle_since = self.clock()
        self._settle_fp = None

    def _settled(self):
        if self._settle_since is None:
            return True
        now = self.clock()
        try:
            current = self.app.get_document()
            # Live hands out a new wrapper object each time: compare with ==, never "is".
            fp = (current == self.song, self.fingerprint())
        except Exception:
            fp = None
        if fp != self._settle_fp or (fp is not None and not fp[0]):
            self._settle_fp = fp
            self._settle_since = now
            if fp is not None and not fp[0]:
                self._last_doc_check = 0.0  # Live moved on to yet another Set: pick it up now
            return False
        if now - self._settle_since < SETTLE:
            return False
        self._settle_since = None
        return True

    def _doc_is_current(self):
        try:
            return self.app.get_document() is self.song or self.app.get_document() == self.song
        except Exception:
            return True

    def _check_document(self):
        """Live may load another Set without restarting the script: notice it and start over."""
        if self._settle_since is not None:
            if self._settled():
                self._last_fingerprint = self.fingerprint()
                self._last_doc_check = self.clock()
                if self.bridge.connected:
                    self._on_connected()
            return
        now = self.clock()
        if now - self._last_doc_check < 1.0:
            return
        self._last_doc_check = now
        try:
            current = self.app.get_document()
        except Exception:
            current = None
        if current is not None and current != self.song:
            self.log("a different Set was opened")
            self.unbind()
            self.song = current
            self.doc_token = new_id()
            self.caps = self._probe_caps()
            self._begin_settle()
            return
        if not self.bound and self.bridge.connected:
            fp = self.fingerprint()
            if self._last_fingerprint is not None and fp != self._last_fingerprint:
                self._on_connected()  # tell the app what's open now
            self._last_fingerprint = fp

    _plugins = None

    def installed_plugins(self):
        """Names of the third-party plugins Live knows on this computer (for the Stand-in)."""
        if self._plugins is not None:
            return self._plugins
        names = set()
        formats = {}
        try:
            stack = [(self.app.browser.plugins, 0, "")]
            while stack and len(names) < 5000:
                item, depth, fmt = stack.pop()
                for child in item.children:
                    if getattr(child, "is_folder", False) or (len(child.children) > 0 and depth < 5):
                        # The top folders are the formats ("VST3", "VST", "Audio Units").
                        stack.append((child, depth + 1, fmt or _format_of(child.name)))
                    elif getattr(child, "is_loadable", True):
                        names.add(child.name)
                        formats.setdefault(child.name, set()).add(fmt or "?")
        except Exception:
            pass
        self._plugins = sorted(names)
        self._plugin_formats = dict((k, sorted(v)) for k, v in formats.items())
        return self._plugins

    _plugin_formats = None

    def plugin_formats(self):
        """{plugin name: ["VST3", "AU", ...]} for the plugins Live knows here."""
        self.installed_plugins()
        return self._plugin_formats or {}

    def _on_connected(self):
        self.bridge.send({
            "type": "hello", "role": "live", "proto": PROTOCOL, "script": SCRIPT_VERSION,
            "live": self.live_version(), "caps": self.caps, "bound": self.bound, "session": self.session,
            "doc": self.doc_token, "plugin_formats": self.plugin_formats(),
            "fingerprint": self.fingerprint(), "unsent": len(self.unsent), "plugins": self.installed_plugins(),
        })
        if self.unsent:
            self.bridge.send({"type": "ops", "ops": self.unsent})
            self.unsent = []
        self._show("Tutti: connected")

    def _failed(self, what, error):
        """Reading or binding the Set failed: stop half-way states from retrying forever and tell
        the app, which tells the person."""
        self.log("couldn't %s the Set: %r" % (what, error))
        try:
            self.unbind()
        except Exception:
            pass
        self.bridge.send({"type": what + "_failed", "error": "%s: %s" % (type(error).__name__, error)})

    def _log_limited(self, key, text, every=300.0):
        """Logs a repeating problem once every few minutes instead of every scan."""
        now = self.clock()
        last = self._logged.get(key)
        if last is not None and now - last < every:
            return
        self._logged[key] = now
        self.log(text)

    def fingerprint(self):
        names = [t.name for t in list(self.song.tracks)[:64]]
        return {"tracks": names, "n": len(self.song.tracks), "returns": len(self.song.return_tracks),
                "scenes": len(self.song.scenes), "tempo": lm.num(self.song.tempo)}

    def _flush_ops(self):
        if not self.out_ops:
            return
        if self.bridge.connected:
            self.bridge.send({"type": "ops", "ops": self.out_ops})
        else:
            self.unsent.extend(self.out_ops)
        self.out_ops = []

    def _emit(self, op):
        self.out_ops.append(op)

    # ------------------------------------------------------------------ messages from the app

    def _handle(self, m):
        t = m.get("type")
        if t in ("bind", "full", "capture") and (self._settle_since is not None or not self._doc_is_current()):
            # Live is still opening a Set: a fresh hello follows once it's ready, and the app
            # asks again then.
            self.log("ignoring %s: Live is still opening a Set" % t)
            if not self._doc_is_current():
                self._last_doc_check = 0.0
            return
        if t == "capture":
            self.session = m.get("session")
            try:
                doc, path_map, notes_map = self.capture()
            except Exception as e:
                self._failed("capture", e)
                return
            self.bridge.send({"type": "captured", "doc": doc, "map": path_map, "notes": notes_map})
        elif t == "bind":
            self.session = m.get("session")
            try:
                stats = self.bind(m.get("doc") or {"e": []}, m.get("map") or {}, m.get("notes") or {})
            except Exception as e:
                self._failed("bind", e)
                return
            self.bridge.send({"type": "bound", "stats": stats, "fingerprint": self.fingerprint()})
        elif t == "full":
            try:
                stats = self.full(m.get("doc") or {"e": []})
            except Exception as e:
                self._failed("bind", e)
                return
            self.bridge.send({"type": "bound", "stats": stats, "full": True})
        elif t == "unbind":
            self.unbind()
        elif t == "apply":
            self.queue.append(("apply", m.get("ops") or []))
        elif t == "reconcile":
            self.queue.append(("reconcile", m.get("items") or []))
        elif t == "structure":
            path_map, notes_map = self.structure_map()
            self.bridge.send({"type": "structure", "map": path_map, "notes": notes_map, "req": m.get("req")})
        elif t == "goto":
            # Jump to where a friend is: select their track and, when stopped, move the playhead.
            obj = self.live.get(m.get("track"))
            if obj is not None and self.mirror.type_of(m.get("track")) == "track":
                lm.safe(lambda: setattr(self.song.view, "selected_track", obj))
            if not lm.safe(lambda: self.song.is_playing, False):
                lm.safe(lambda: setattr(self.song, "current_song_time", max(0.0, float(m.get("time") or 0.0))))
        elif t == "play_at":
            self.play_at = (float(m.get("pos", 0.0)), float(m.get("at", 0.0)))
        elif t == "stop_play":
            self.play_at = None
            if self.song.is_playing:
                self.song.stop_playing()
        elif t == "idea":
            self.make_idea(m.get("track"), m.get("name"), m.get("color"))
        elif t == "swap_file":
            self.swap_file(m.get("id"), m.get("path"))
        elif t == "show":
            self._show("Tutti: " + str(m.get("text", "")))
        elif t == "ping":
            self.bridge.send({"type": "pong", "t": m.get("t")})

    # ------------------------------------------------------------------ binding registry

    def id_of(self, obj):
        if obj is None:
            return None
        return self.idx.get(key_of(obj))

    def _bind(self, eid, obj):
        old = self.live.get(eid)
        if old is not None:
            self.idx.pop(key_of(old), None)
        self.live[eid] = obj
        self.idx[key_of(obj)] = eid
        self.realized.add(eid)
        self._watch(eid, obj)

    def _unbind_subtree(self, eid):
        for i in self.mirror.subtree(eid):
            obj = self.live.pop(i, None)
            if obj is not None:
                self.idx.pop(key_of(obj), None)
            self.notes.pop(i, None)
            self.stale.pop(i, None)
            self.realized.discard(i)

    def _reset_registry(self):
        self._unwatch_all()
        self.live = {}
        self.idx = {}
        self.notes = {}
        self.realized = set()
        self.stale = {}
        self.dirty = set()
        self.queue = []

    # ------------------------------------------------------------------ listeners

    WATCH = {
        "song": (("tempo", "props"), ("signature_numerator", "props"), ("signature_denominator", "props"),
                 ("swing_amount", "props"), ("tracks", "struct"), ("return_tracks", "struct"),
                 ("scenes", "struct"), ("cue_points", "struct")),
        "track": (("name", "props"), ("color", "props"), ("mute", "props"), ("devices", "devices"),
                  ("arrangement_clips", "clips"), ("clip_slots", "clips")),
        "device": (("name", "props"), ("parameters", "props")),
        "chain": (("name", "props"), ("devices", "devices")),
        "clip": (("name", "props"), ("color", "props"), ("muted", "props"), ("looping", "props"),
                 ("loop_start", "props"), ("loop_end", "props"), ("start_marker", "props"),
                 ("end_marker", "props"), ("warping", "props"), ("notes", "notes")),
        "scene": (("name", "props"), ("color", "props"), ("tempo", "props")),
        "cue": (("name", "props"), ("time", "props")),
    }

    def _listen(self, obj, prop, callback):
        add = getattr(obj, "add_%s_listener" % prop, None)
        if add is None:
            return
        wk = (key_of(obj), prop)
        if wk in self._watched:
            return
        try:
            add(callback)
            self._listeners.append((obj, prop, callback))
            self._watched.add(wk)
        except Exception:
            pass

    def _unwatch_all(self):
        for obj, prop, callback in self._listeners:
            try:
                getattr(obj, "remove_%s_listener" % prop)(callback)
            except Exception:
                pass
        self._listeners = []
        self._watched = set()

    def _watch(self, eid, obj):
        etype = self.mirror.type_of(eid)
        no_arrangement = etype == "track" and lm.track_kind(obj, self.song) in lm.NO_ARRANGEMENT
        for prop, what in self.WATCH.get(etype, ()):
            if prop == "arrangement_clips" and no_arrangement:
                continue
            self._listen(obj, prop, self._marker(eid, what))
        if etype == "track":
            mixer = obj.mixer_device
            mark = self._marker(eid, "props")
            auto = self._automation_marker(eid)
            self._listen(mixer.volume, "value", mark)
            self._listen(mixer.panning, "value", mark)
            self._listen(mixer.volume, "automation_state", auto)
            self._listen(mixer.panning, "automation_state", auto)
            self._listen(mixer, "sends", mark)
            for send in lm.safe(lambda: list(mixer.sends), []):
                self._listen(send, "value", mark)
                self._listen(send, "automation_state", auto)
            for slot in lm.safe(lambda: list(obj.clip_slots), []):
                self._listen(slot, "has_clip", self._marker(eid, "clips"))
        elif etype == "device":
            mark = self._marker(eid, "props")
            auto = self._automation_marker(eid)
            for p in lm.safe(lambda: list(obj.parameters), []):
                self._listen(p, "value", mark)
                self._listen(p, "automation_state", auto)
            if lm.is_plugin(obj) and lm.has(obj, "selected_preset_index"):
                self._listen(obj, "selected_preset_index", mark)
            if lm.safe(lambda: obj.can_have_chains, False):
                self._listen(obj, "chains", self._marker(eid, "devices"))
        elif etype == "clip":
            self._listen(obj, "is_recording", self._marker(self.mirror.parent(eid), "clips"))

    def _automation_marker(self, eid):
        def mark():
            self._automation.add(eid)
        return mark

    def _marker(self, eid, what):
        def mark():
            self.dirty.add((eid, what))
        return mark

    def _mark_everything(self):
        for eid in list(self.live):
            etype = self.mirror.type_of(eid)
            if etype in ("song", "track", "device", "clip", "scene", "chain", "cue"):
                self.dirty.add((eid, "props"))
            if etype in ("song",):
                self.dirty.add((eid, "struct"))
            if etype in ("track", "chain"):
                self.dirty.add((eid, "devices"))
            if etype == "track":
                self.dirty.add((eid, "clips"))

    # ------------------------------------------------------------------ capture (starting a session)

    def capture(self):
        """Makes a fresh document from the Set as it is (Player 1 starting a session)."""
        self._reset_registry()
        self.mirror.reset()
        self.bound = True
        self.mirror.set("song", lm.read_song(self.song))
        self._bind("song", self.song)
        sink = []
        self._sync_song_structure(sink)
        self.out_ops = []
        path_map, notes_map = self.structure_map()
        if self.caps.get("data"):
            for eid, obj in self.live.items():
                if self.mirror.type_of(eid) == "track":
                    try:
                        obj.set_data("tutti_id", eid)
                    except Exception:
                        pass
        self.dirty = set()
        return self.mirror.to_json(), path_map, notes_map

    def unbind(self):
        self._reset_registry()
        self.mirror.reset()
        self.bound = False
        self.session = None

    # ------------------------------------------------------------------ binding (join, update, restart)

    def bind(self, doc, path_map, notes_map):
        """Binds the open Set to a document, then makes Live match it."""
        self._reset_registry()
        self.mirror.load(doc.get("e", []))
        self.bound = True
        stats = {"matched": 0, "created": 0, "removed": 0, "new": 0, "stale": 0}

        self._bind("song", self.song)
        claimed = set(["song"])

        def candidate(path, obj, etype, kind=None):
            eid = None
            if etype == "track" and self.caps.get("data"):
                eid = lm.safe(lambda: obj.get_data("tutti_id", None))
            if not eid:
                eid = path_map.get(path)
            return eid

        def try_bind(path, obj, etype, kind=None):
            eid = candidate(path, obj, etype, kind)
            if eid and eid not in claimed and self.mirror.type_of(eid) == etype:
                if kind is None or self.mirror.props(eid).get("kind") == kind:
                    self._bind(eid, obj)
                    claimed.add(eid)
                    stats["matched"] += 1
                    return eid
            if eid and not self.mirror.has(eid):
                return ("gone", eid)
            return None

        gone = []
        song = self.song

        # Scenes first: session clips point at them.
        for i, scene in enumerate(song.scenes):
            r = try_bind("S%d" % i, scene, "scene")
            if isinstance(r, tuple):
                gone.append(("scene", scene))
        self._match_leftovers("song", "scene", list(song.scenes), claimed, stats,
                              lambda o, p: True)

        collections = [("T", list(song.tracks), None), ("R", list(song.return_tracks), "return")]
        for prefix, tracks, forced_kind in collections:
            for i, track in enumerate(tracks):
                kind = forced_kind or lm.track_kind(track)
                r = try_bind("%s%d" % (prefix, i), track, "track", kind)
                if isinstance(r, tuple):
                    gone.append(("track", track))
        self._match_leftovers("song", "track", list(song.tracks), claimed, stats,
                              lambda o, p: p.get("kind") == lm.track_kind(o) and p.get("kind") != "return")
        self._match_leftovers("song", "track", list(song.return_tracks), claimed, stats,
                              lambda o, p: p.get("kind") == "return")
        master_id = None
        for eid in self.mirror.children("song", "track"):
            if self.mirror.props(eid).get("kind") == "master":
                master_id = eid
        if master_id is None:
            master_id = path_map.get("M")
        if master_id and self.mirror.has(master_id):
            self._bind(master_id, song.master_track)
            claimed.add(master_id)

        # Inside each bound track.
        for prefix, tracks in (("T", list(song.tracks)), ("R", list(song.return_tracks)), ("M", [song.master_track])):
            for i, track in enumerate(tracks):
                tid = self.id_of(track)
                if tid is None:
                    continue
                base = prefix if prefix == "M" else "%s%d" % (prefix, i)
                self._bind_devices(base, tid, list(track.devices), path_map, claimed, stats, gone)
                self._bind_clips(base, tid, track, path_map, notes_map, claimed, stats, gone)

        cues = list(getattr(song, "cue_points", []))
        for i, cue in enumerate(cues):
            try_bind("Q%d" % i, cue, "cue")
        self._match_leftovers("song", "cue", cues, claimed, stats,
                              lambda o, p: lm.same(p.get("time"), lm.num(o.time)))

        # Things the shared song no longer has.
        for etype, obj in gone:
            if self._delete_live_object(etype, obj):
                stats["removed"] += 1

        # Make Live match the document: create what's missing, set what differs.
        stats["created"] += self._realize_all()
        self._write_all_props()
        # Anything left unbound in Live is new here: share it.
        before = len(self.out_ops)
        self._sync_song_structure(None)
        stats["new"] = len(self.out_ops) - before
        stats["stale"] = len(self.stale)
        self.dirty = set()
        self.log("bound: %s" % stats)
        return stats

    def _match_leftovers(self, parent, etype, objs, claimed, stats, compatible):
        """Pairs unbound Live objects with unclaimed entities of the same kind, in order."""
        free = [e for e in self.mirror.ordered(parent, etype) if e not in claimed]
        for obj in objs:
            if self.id_of(obj) is not None:
                continue
            for eid in free:
                if compatible(obj, self.mirror.props(eid)):
                    self._bind(eid, obj)
                    claimed.add(eid)
                    free.remove(eid)
                    stats["matched"] += 1
                    break

    def _bind_devices(self, base, parent_id, devices, path_map, claimed, stats, gone):
        for j, device in enumerate(devices):
            path = "%s/D%d" % (base, j)
            eid = path_map.get(path)
            if eid and eid not in claimed and self.mirror.type_of(eid) == "device":
                self._bind(eid, device)
                claimed.add(eid)
                stats["matched"] += 1
            elif eid and not self.mirror.has(eid):
                gone.append(("device", device))
        self._match_leftovers(parent_id, "device", devices, claimed, stats,
                              lambda o, p: p.get("cls") == lm.safe(lambda: o.class_name))
        for j, device in enumerate(devices):
            did = self.id_of(device)
            if did is None or not lm.safe(lambda: device.can_have_chains, False):
                continue
            for k, chain in enumerate(lm.safe(lambda: list(device.chains), [])):
                cpath = "%s/D%d/C%d" % (base, j, k)
                cid = path_map.get(cpath)
                if cid and cid not in claimed and self.mirror.type_of(cid) == "chain":
                    self._bind(cid, chain)
                    claimed.add(cid)
                self._match_leftovers(did, "chain", [chain], claimed, stats, lambda o, p: True)
                cid = self.id_of(chain)
                if cid is not None:
                    self._bind_devices(cpath, cid, list(chain.devices), path_map, claimed, stats, gone)

    def _bind_clips(self, base, tid, track, path_map, notes_map, claimed, stats, gone):
        arrangement = self._arrangement_clips(track)
        for j, clip in enumerate(arrangement):
            eid = path_map.get("%s/A%d" % (base, j))
            if eid and eid not in claimed and self.mirror.type_of(eid) == "clip":
                self._bind(eid, clip)
                claimed.add(eid)
                stats["matched"] += 1
            elif eid and not self.mirror.has(eid):
                gone.append(("clip", clip))
        self._match_leftovers(tid, "clip", arrangement, claimed, stats,
                              lambda o, p: p.get("where") == "a" and lm.same(p.get("start"), lm.num(o.start_time)))

        scenes = list(self.song.scenes)
        session = []
        for i, slot in enumerate(lm.safe(lambda: list(track.clip_slots), [])):
            if not slot.has_clip:
                continue
            clip = slot.clip
            session.append(clip)
            eid = path_map.get("%s/L%d" % (base, i))
            if eid and eid not in claimed and self.mirror.type_of(eid) == "clip":
                self._bind(eid, clip)
                claimed.add(eid)
                stats["matched"] += 1
            elif eid and not self.mirror.has(eid):
                gone.append(("clip", clip))
            elif self.id_of(clip) is None and i < len(scenes):
                scene_id = self.id_of(scenes[i])
                for cid in self.mirror.children(tid, "clip"):
                    p = self.mirror.props(cid)
                    if cid not in claimed and p.get("where") == "s" and p.get("scene") == scene_id:
                        self._bind(cid, clip)
                        claimed.add(cid)
                        stats["matched"] += 1
                        break

        for clip in arrangement + session:
            cid = self.id_of(clip)
            if cid is not None and clip.is_midi_clip:
                self._bind_notes(cid, clip, notes_map.get(cid))

    def _bind_notes(self, cid, clip, snapshot_order):
        live_notes = lm.read_notes(clip)
        mapping = {}
        doc_notes = {n: self.mirror.props(n) for n in self.mirror.children(cid, "note")}
        free = dict(doc_notes)
        remove = []
        if snapshot_order:
            ordered = sorted(live_notes.items(), key=lambda kv: lm.note_key(kv[1]))
            for (nid, props), eid in zip(ordered, snapshot_order):
                if eid in free:
                    mapping[nid] = eid
                    del free[eid]
                elif eid not in doc_notes:
                    remove.append(nid)  # deleted from the shared song since the snapshot
        by_key = {}
        for eid, p in free.items():
            by_key.setdefault(lm.note_key(p), []).append(eid)
        for nid, props in live_notes.items():
            if nid in mapping or nid in remove:
                continue
            candidates = by_key.get(lm.note_key(props))
            if candidates:
                eid = candidates.pop(0)
                mapping[nid] = eid
                free.pop(eid, None)
        if remove:
            try:
                clip.remove_notes_by_id(remove)
            except Exception as e:
                self.log("couldn't remove notes: %r" % (e,))
        self.notes[cid] = mapping

    # ------------------------------------------------------------------ full resync (after a gap or split)

    def full(self, doc):
        """Takes the given document as the truth, keeping existing bindings."""
        old_live = dict(self.live)
        self.mirror.load(doc.get("e", []))
        stats = {"removed": 0, "created": 0}
        for eid, obj in old_live.items():
            if not self.mirror.has(eid) and eid != "song":
                etype = None
                # The type is gone with the entity; work it out from the object.
                for t, attr in (("track", "mixer_device"), ("device", "parameters"), ("clip", "is_midi_clip"),
                                ("cue", "jump"), ("scene", "tempo")):
                    if lm.has(obj, attr):
                        etype = t
                        break
                self.live.pop(eid, None)
                self.idx.pop(key_of(obj), None)
                self.realized.discard(eid)
                if etype and self._delete_live_object(etype, obj):
                    stats["removed"] += 1
        for cid in list(self.notes):
            if not self.mirror.has(cid):
                self.notes.pop(cid, None)
        stats["created"] = self._realize_all()
        self._write_all_props()
        self._sync_song_structure(None)
        self.dirty = set()
        return stats

    # ------------------------------------------------------------------ applying others' edits

    def _run_queue(self):
        budget = APPLY_BUDGET
        while self.queue and budget > 0:
            kind, items = self.queue[0]
            take = items[:budget]
            rest = items[budget:]
            if kind == "apply":
                self._apply_ops(take)
            else:
                self._reconcile(take)
            budget -= len(take)
            if rest:
                self.queue[0] = (kind, rest)
            else:
                self.queue.pop(0)

    def _apply_ops(self, ops):
        note_adds = {}
        for op in ops:
            k = op.get("k")
            eid = op.get("id")
            try:
                if k == "c":
                    existed = self.mirror.has(eid)
                    self.mirror.apply_op(op)
                    if op.get("t") == "note" and not existed:
                        note_adds.setdefault(op["p"], []).append(eid)
                    elif eid not in self.live:
                        self._realize(eid)
                    else:
                        self._write_props(eid, op.get("v") or {})
                elif k == "s":
                    if not self.mirror.has(eid):
                        continue
                    self.mirror.set(eid, op.get("v") or {})
                    if eid in self.live:
                        self._write_props(eid, op.get("v") or {})
                    else:
                        self._realize(eid)
                elif k == "r":
                    self._remove_entity(eid)
            except Exception as e:
                self._mark_stale(eid, "couldn't apply: %r" % (e,))
        for cid, ids in note_adds.items():
            self._add_notes(cid, ids)

    def _reconcile(self, items):
        for item in items:
            eid = item.get("id")
            state = item.get("state")
            try:
                if state is None:
                    self._remove_entity(eid)
                    continue
                for e in state:
                    i = e["id"]
                    if self.mirror.has(i):
                        self.mirror.replace(i, e.get("v") or {})
                        if i in self.live:
                            self._write_props(i, e.get("v") or {})
                        elif e["t"] == "note":
                            self._add_notes(e["p"], [i])
                        else:
                            self._realize(i)
                    else:
                        self.mirror.create(i, e["t"], e["p"], e.get("v") or {})
                        if e["t"] == "note":
                            self._add_notes(e["p"], [i])
                        else:
                            self._realize(i)
            except Exception as ex:
                self._mark_stale(eid, "couldn't reconcile: %r" % (ex,))

    def _mark_stale(self, eid, reason):
        self.stale[eid] = reason
        self.log("stale %s: %s" % (eid, reason))
        if self.bridge.connected:
            self.bridge.send({"type": "stale", "ids": [eid], "reason": reason})

    def _remove_entity(self, eid):
        if not self.mirror.has(eid):
            return
        etype = self.mirror.type_of(eid)
        obj = self.live.get(eid)
        if etype == "note":
            cid = self.mirror.parent(eid)
            mapping = self.notes.get(cid, {})
            local = [nid for nid, e in mapping.items() if e == eid]
            clip = self.live.get(cid)
            if clip is not None and local:
                try:
                    clip.remove_notes_by_id(local)
                except Exception as e:
                    self._mark_stale(cid, "couldn't remove a note: %r" % (e,))
            for nid in local:
                mapping.pop(nid, None)
        elif obj is not None:
            if not self._delete_live_object(etype, obj):
                self._mark_stale(eid, "couldn't remove from Live")
        self._unbind_subtree(eid)
        self.mirror.remove(eid)

    def _delete_live_object(self, etype, obj):
        song = self.song
        try:
            if etype == "track":
                i = index_of(song.tracks, obj)
                if i >= 0:
                    song.delete_track(i)
                    return True
                i = index_of(song.return_tracks, obj)
                if i >= 0:
                    song.delete_return_track(i)
                    return True
            elif etype == "device":
                parent = obj.canonical_parent if lm.has(obj, "canonical_parent") else None
                holders = [parent] if parent is not None else list(song.tracks) + list(song.return_tracks) + [song.master_track]
                for holder in holders:
                    i = index_of(getattr(holder, "devices", []), obj)
                    if i >= 0 and lm.has(holder, "delete_device"):
                        holder.delete_device(i)
                        return True
            elif etype == "clip":
                parent = getattr(obj, "canonical_parent", None)
                if parent is not None and lm.has(parent, "has_clip"):
                    parent.delete_clip()
                    return True
                track = parent
                if track is not None and lm.has(track, "delete_clip"):
                    track.delete_clip(obj)
                    return True
            elif etype == "scene":
                i = index_of(song.scenes, obj)
                if i >= 0 and len(song.scenes) > 1:
                    song.delete_scene(i)
                    return True
            elif etype == "cue":
                if not song.is_playing:
                    keep = song.current_song_time
                    song.current_song_time = obj.time
                    song.set_or_delete_cue()
                    song.current_song_time = keep
                    return True
        except Exception as e:
            self.log("couldn't delete %s: %r" % (etype, e))
        return False

    # --- creating things in Live

    def _realize_all(self):
        created = 0
        for eid in self.mirror.subtree("song"):
            if eid in self.live:
                continue
            etype = self.mirror.type_of(eid)
            if etype == "note":
                continue  # notes are added per clip below
            if self.mirror.parent(eid) in self.live and self._realize(eid, recurse=False):
                created += 1
        for cid in list(self.live):
            if self.mirror.type_of(cid) == "clip" and self.mirror.props(cid).get("midi"):
                mapped = set(self.notes.get(cid, {}).values())
                missing = [n for n in self.mirror.children(cid, "note") if n not in mapped]
                if missing:
                    self._add_notes(cid, missing)
                    created += len(missing)
        return created

    def _write_all_props(self):
        for eid in list(self.live):
            if self.mirror.has(eid):
                try:
                    self._write_props(eid, self.mirror.props(eid))
                except Exception as e:
                    self._mark_stale(eid, "couldn't set: %r" % (e,))
        for cid in list(self.notes):
            clip = self.live.get(cid)
            if clip is None:
                continue
            self._write_note_props(cid, clip, {nid: self.mirror.props(eid) for nid, eid in self.notes[cid].items()
                                               if self.mirror.has(eid)})

    def _realize(self, eid, recurse=True):
        """Creates the entity in Live under its (already realized) parent."""
        etype = self.mirror.type_of(eid)
        parent = self.mirror.parent(eid)
        props = self.mirror.props(eid)
        if parent not in self.live:
            return False
        holder = self.live[parent]
        obj = None
        try:
            if etype == "track":
                obj = self._create_track(eid, props)
            elif etype == "device":
                obj = self._create_device(eid, holder, parent, props)
            elif etype == "clip":
                obj = self._create_clip(holder, props)
            elif etype == "scene":
                obj = self._create_scene(eid, props)
            elif etype == "cue":
                obj = self._create_cue(props)
            elif etype == "chain":
                self._mark_stale(eid, "rack chains need an Update")
                return False
            else:
                return False
        except Exception as e:
            self._mark_stale(eid, "couldn't create %s: %r" % (etype, e))
            return False
        if obj is None:
            return False
        self._bind(eid, obj)
        self.stale.pop(eid, None)
        try:
            self._write_props(eid, props)
        except Exception as e:
            self._mark_stale(eid, "couldn't set: %r" % (e,))
        if etype == "clip" and props.get("midi"):
            self.notes.setdefault(eid, {})
            notes = self.mirror.children(eid, "note")
            if notes:
                self._add_notes(eid, notes)
        if recurse:
            for child in self.mirror.children(eid):
                if self.mirror.type_of(child) not in ("note",) and child not in self.live:
                    self._realize(child)
        return True

    def _sibling_index(self, parent, etype, eid, where=None):
        """Where an entity goes among its realized siblings, by pos."""
        ordered = self.mirror.ordered(parent, etype, where)
        index = 0
        for other in ordered:
            if other == eid:
                break
            if other in self.live:
                index += 1
        return index

    def _create_track(self, eid, props):
        kind = props.get("kind")
        song = self.song
        if kind in ("midi", "audio"):
            index = self._sibling_index("song", "track", eid,
                                        lambda p: p.get("kind") in ("midi", "audio", "group"))
            index = min(index, len(song.tracks))
            if kind == "midi":
                song.create_midi_track(index)
            else:
                song.create_audio_track(index)
            return song.tracks[index]
        if kind == "return":
            song.create_return_track()
            return song.return_tracks[len(song.return_tracks) - 1]
        if kind == "master":
            return song.master_track
        self._mark_stale(eid, "group tracks need an Update")
        return None

    def _create_device(self, eid, holder, parent, props):
        if props.get("plugin"):
            self._mark_stale(eid, "plugins need an Update")
            return None
        insert = getattr(holder, "insert_device", None)
        if insert is None:
            self._mark_stale(eid, "this Live can't add devices for Tutti; needs an Update")
            return None
        index = self._sibling_index(parent, "device", eid)
        devices = list(holder.devices)
        index = min(index, len(devices))
        name = props.get("disp") or props.get("name")
        result = insert(name, index)
        devices = list(holder.devices)
        if result is not None and index_of(devices, result) >= 0:
            return result
        return devices[index] if index < len(devices) else None

    def _create_clip(self, track, props):
        midi = props.get("midi", True)
        if props.get("where") == "s":
            scene_id = props.get("scene")
            scene = self.live.get(scene_id)
            if scene is None:
                return None
            i = index_of(self.song.scenes, scene)
            slots = list(track.clip_slots)
            if i < 0 or i >= len(slots):
                return None
            slot = slots[i]
            if slot.has_clip:
                slot.delete_clip()
            if midi:
                slot.create_clip(float(props.get("len", 4.0)))
            else:
                path = props.get("path")
                if not path or not lm.has(slot, "create_audio_clip"):
                    return None
                slot.create_audio_clip(path)
            return slot.clip

        start = float(props.get("start", 0.0))
        length = float(props.get("len", 4.0))
        if midi:
            if lm.has(track, "create_midi_clip"):
                clip = track.create_midi_clip(start, length)
                return clip if clip is not None else self._arrangement_clip_at(track, start)
            return self._via_session(track, start, lambda slot: slot.create_clip(length))
        path = props.get("path")
        if not path:
            return None  # waits until the audio (or its preview) is here
        if lm.has(track, "create_audio_clip"):
            clip = track.create_audio_clip(path, start)
            return clip if clip is not None else self._arrangement_clip_at(track, start)
        return self._via_session(track, start, lambda slot: slot.create_audio_clip(path))

    def _via_session(self, track, start, make):
        """Older Lives: make the clip in an empty session slot, copy it to the timeline, clean up."""
        if not lm.has(track, "duplicate_clip_to_arrangement"):
            return None
        slots = list(track.clip_slots)
        slot = None
        for s in reversed(slots):
            if not s.has_clip:
                slot = s
                break
        if slot is None:
            return None
        make(slot)
        clip = track.duplicate_clip_to_arrangement(slot.clip, start)
        slot.delete_clip()
        return clip if clip is not None else self._arrangement_clip_at(track, start)

    def _arrangement_clip_at(self, track, start):
        for clip in self._arrangement_clips(track):
            if abs(clip.start_time - start) < 1e-3 and self.id_of(clip) is None:
                return clip
        return None

    def _create_scene(self, eid, props):
        index = self._sibling_index("song", "scene", eid)
        index = min(index, len(self.song.scenes))
        self.song.create_scene(index)
        return self.song.scenes[index]

    def _create_cue(self, props):
        song = self.song
        if song.is_playing:
            return None  # retried when the transport stops
        t = float(props.get("time", 0.0))
        keep = song.current_song_time
        song.current_song_time = t
        song.set_or_delete_cue()
        song.current_song_time = keep
        for cue in song.cue_points:
            if abs(cue.time - t) < 1e-6 and self.id_of(cue) is None:
                return cue
        return None

    def _add_notes(self, cid, note_ids):
        clip = self.live.get(cid)
        if clip is None or not note_ids:
            return
        mapping = self.notes.setdefault(cid, {})
        already = set(mapping.values())
        todo = [n for n in note_ids if n not in already and self.mirror.has(n)]
        if not todo:
            return
        specs = [lm.note_spec(self.Live, self.mirror.props(n)) for n in todo]
        try:
            ids = clip.add_new_notes(tuple(specs))
        except Exception as e:
            self._mark_stale(cid, "couldn't add notes: %r" % (e,))
            return
        if ids is not None and len(ids) == len(todo):
            for nid, eid in zip(ids, todo):
                mapping[nid] = eid
        else:
            # Older API without returned ids: match by content.
            self._bind_notes(cid, clip, None)

    # --- setting props

    def _write_props(self, eid, props):
        etype = self.mirror.type_of(eid)
        obj = self.live.get(eid)
        if obj is None:
            return
        if etype == "song":
            lm.write_song(self.song, props)
        elif etype == "track":
            lm.write_track(obj, props)
        elif etype == "device":
            lm.write_device(obj, props)
        elif etype == "clip":
            self._write_clip(eid, obj, props)
        elif etype == "scene":
            lm.write_scene(obj, props)
        elif etype == "cue":
            if "name" in props and obj.name != props["name"]:
                obj.name = props["name"]
        elif etype == "note":
            cid = self.mirror.parent(eid)
            clip = self.live.get(cid)
            if clip is not None:
                local = [nid for nid, e in self.notes.get(cid, {}).items() if e == eid]
                if local:
                    self._write_note_props(cid, clip, {local[0]: self.mirror.props(eid)})

    def _write_clip(self, eid, clip, props):
        full = self.mirror.props(eid)
        if full.get("where") == "a":
            moved = "start" in props and not lm.same(lm.num(clip.start_time), full.get("start"))
            resized = "len" in props and not lm.same(lm.num(clip.end_time - clip.start_time), full.get("len"))
            new_file = "path" in props and full.get("path") and lm.safe(lambda: clip.file_path) != full.get("path")
            if moved or new_file:
                self._recreate_clip(eid)
                return
            if resized and not full.get("loop"):
                # Unlooped clips get their length from the markers.
                try:
                    clip.end_marker = clip.start_marker + float(full["len"])
                except Exception:
                    self._mark_stale(eid, "clip length needs an Update")
        elif "path" in props and full.get("path") and lm.safe(lambda: clip.file_path) != full.get("path"):
            self._recreate_clip(eid)
            return
        lm.write_clip(clip, props, self.Live)

    def _recreate_clip(self, eid):
        """Moves a clip or changes its audio file: Live's API can't, so make a new one in place."""
        old = self.live.get(eid)
        track_id = self.mirror.parent(eid)
        track = self.live.get(track_id)
        if old is None or track is None:
            return
        props = self.mirror.props(eid)
        if props.get("where") == "a" and not (lm.has(track, "delete_clip")):
            self._mark_stale(eid, "moving clips needs an Update in this Live")
            return
        # The clip in Live has the real warp markers (from the song file); the rebuilt one gets
        # Live's tempo guess, so keep these to put back.
        markers = lm.read_warp_markers(old) if not lm.safe(lambda: old.is_midi_clip, True) else []
        self.live.pop(eid, None)
        self.idx.pop(key_of(old), None)
        self.notes.pop(eid, None)
        self.realized.discard(eid)
        try:
            if props.get("where") == "a":
                track.delete_clip(old)
            else:
                parent = getattr(old, "canonical_parent", None)
                if parent is not None and lm.has(parent, "delete_clip"):
                    parent.delete_clip()
        except Exception as e:
            self.log("couldn't remove the old clip: %r" % (e,))
        if not self._realize(eid, recurse=False):
            self._mark_stale(eid, "couldn't rebuild the clip")
            return
        new = self.live.get(eid)
        if markers and new is not None:
            lm.safe(lambda: setattr(new, "warping", True) if props.get("warp", True) else None)
            if not lm.write_warp_markers(new, markers, self.Live):
                self._mark_stale(eid, "couldn't restore the clip's warp markers")

    def _write_note_props(self, cid, clip, wanted):
        """wanted: {Live note id: props}. Applies only notes that differ."""
        if not wanted:
            return
        current = {}
        getter = getattr(clip, "get_all_notes_extended", None)
        notes = getter() if getter is not None else clip.get_notes_extended(0, 128, -1e6, 2e6)
        for n in notes:
            current[n.note_id] = n
        changed = []
        for nid, props in wanted.items():
            n = current.get(nid)
            if n is None or props is None:
                continue
            have = lm.note_props(n)
            if any(not lm.same(have.get(k), v) for k, v in props.items() if k in have):
                lm.write_note(n, props)
                changed.append(n)
        if changed:
            try:
                clip.apply_note_modifications(tuple(changed) if not isinstance(notes, list) else changed)
            except Exception as e:
                self._mark_stale(cid, "couldn't change notes: %r" % (e,))

    # ------------------------------------------------------------------ noticing local edits

    def _process_dirty(self):
        if not self.dirty:
            return
        work = self.dirty
        self.dirty = set()
        order = {"struct": 0, "devices": 1, "clips": 2, "props": 3, "notes": 4}
        for eid, what in sorted(work, key=lambda w: order.get(w[1], 9)):
            if eid not in self.live or not self.mirror.has(eid):
                continue
            try:
                if what == "struct":
                    self._sync_song_structure(None)
                elif what == "devices":
                    self._sync_devices(eid)
                elif what == "clips":
                    self._sync_clips(eid)
                elif what == "props":
                    self._sync_props(eid)
                elif what == "notes":
                    self._sync_notes(eid)
            except Exception as e:
                self._log_limited(("scan", what, eid, repr(e)), "scan error (%s %s): %r" % (what, eid, e))

    def _sync_props(self, eid):
        etype = self.mirror.type_of(eid)
        obj = self.live[eid]
        if etype == "song":
            props = lm.read_song(obj)
        elif etype == "track":
            props = lm.read_track(obj, self.mirror.props(eid).get("kind"))
            props.pop("kind", None)
        elif etype == "device":
            props = lm.read_device(obj)
            if self.mirror.props(eid).get("plugin") and "plugin" not in props:
                props["plugin"] = self.mirror.props(eid)["plugin"]
        elif etype == "clip":
            if lm.safe(lambda: obj.is_recording, False):
                return
            where = self.mirror.props(eid).get("where", "a")
            props = lm.read_clip(obj, where)
            if where == "s":
                props["scene"] = self.mirror.props(eid).get("scene")
        elif etype == "scene":
            props = lm.read_scene(obj)
        elif etype == "cue":
            props = lm.read_cue(obj)
        elif etype == "chain":
            props = {"name": obj.name}
        else:
            return
        self._diff_and_emit(eid, props)

    def _diff_and_emit(self, eid, props):
        current = self.mirror.props(eid)
        changes = {}
        for k, v in props.items():
            if k == "pos":
                continue
            if v is None and k not in current:
                continue
            if k not in current or not lm.same(current[k], v):
                changes[k] = v
        if changes:
            self.mirror.set(eid, changes)
            self._emit({"k": "s", "id": eid, "v": changes})

    def _new_pos(self, before, after):
        if before is not None and after is not None:
            return (before + after) / 2.0
        if before is not None:
            return before + 1.0
        if after is not None:
            return after - 1.0
        return 0.0

    def _sync_ordered(self, parent, etype, objs, read, adopt, compatible, where=None):
        """Diffs an ordered Live collection against the mirror. Emits creates, removes and moves."""
        known = self.mirror.ordered(parent, etype, where)
        known_set = set(known)
        seen = set()
        unbound = []
        for obj in objs:
            eid = self.id_of(obj)
            if eid is not None and eid in known_set:
                seen.add(eid)
            else:
                unbound.append(obj)
        missing = [e for e in known if e not in seen and e in self.live]
        # A Live object that came back under a new wrapper: same kind in the same place.
        for obj in list(unbound):
            for eid in missing:
                if compatible(obj, self.mirror.props(eid)):
                    self._bind(eid, obj)
                    seen.add(eid)
                    missing.remove(eid)
                    unbound.remove(obj)
                    break
        for eid in missing:
            self._emit({"k": "r", "id": eid})
            self._unbind_subtree(eid)
            self.mirror.remove(eid)
        # New objects (in Live order) and moved ones.
        sequence = []
        for obj in objs:
            sequence.append((self.id_of(obj), obj))
        positions = [float(self.mirror.props(e).get("pos", 0.0)) if e is not None and self.mirror.has(e) else None
                     for e, _ in sequence]
        last = None
        for i, (eid, obj) in enumerate(sequence):
            if eid is None or not self.mirror.has(eid):
                after = None
                for j in range(i + 1, len(sequence)):
                    if positions[j] is not None and (last is None or positions[j] > last):
                        after = positions[j]
                        break
                pos = self._new_pos(last, after)
                new = adopt(parent, obj, pos)
                positions[i] = pos
                last = pos
                sequence[i] = (new, obj)
                continue
            pos = positions[i]
            if last is not None and pos <= last:
                # Moved in Live: give it a key after the previous one.
                after = None
                for j in range(i + 1, len(sequence)):
                    if positions[j] is not None and positions[j] > last:
                        after = positions[j]
                        break
                pos = self._new_pos(last, after)
                positions[i] = pos
                self.mirror.set(eid, {"pos": pos})
                self._emit({"k": "s", "id": eid, "v": {"pos": pos}})
            last = pos

    def _sync_song_structure(self, _sink):
        song = self.song
        self._diff_and_emit("song", lm.read_song(song))
        self._sync_ordered("song", "scene", list(song.scenes), lm.read_scene, self._adopt_scene,
                           lambda o, p: True)
        self._sync_ordered("song", "track", list(song.tracks), None, self._adopt_track,
                           lambda o, p: p.get("kind") == lm.track_kind(o),
                           where=lambda p: p.get("kind") in ("midi", "audio", "group"))
        self._sync_ordered("song", "track", list(song.return_tracks), None, self._adopt_return,
                           lambda o, p: p.get("kind") == "return",
                           where=lambda p: p.get("kind") == "return")
        if self.id_of(song.master_track) is None:
            self._adopt_master()
        self._sync_cues()
        for eid in list(self.live):
            if self.mirror.type_of(eid) == "track":
                self._sync_devices(eid)
                self._sync_clips(eid)

    def _sync_cues(self):
        cues = list(getattr(self.song, "cue_points", []))
        known = set(self.mirror.children("song", "cue"))
        seen = set()
        for cue in cues:
            eid = self.id_of(cue)
            if eid is not None and eid in known:
                seen.add(eid)
                continue
            eid = new_id()
            props = lm.read_cue(cue)
            self.mirror.create(eid, "cue", "song", props)
            self._bind(eid, cue)
            self._emit({"k": "c", "id": eid, "t": "cue", "p": "song", "v": props})
        for eid in known - seen:
            if eid in self.live:
                self._emit({"k": "r", "id": eid})
                self._unbind_subtree(eid)
                self.mirror.remove(eid)

    def _sync_devices(self, holder_id):
        holder = self.live[holder_id]
        devices = lm.safe(lambda: list(holder.devices), [])
        self._sync_ordered(holder_id, "device", devices, None, self._adopt_device,
                           lambda o, p: p.get("cls") == lm.safe(lambda: o.class_name))
        for device in devices:
            did = self.id_of(device)
            if did is not None and lm.safe(lambda: device.can_have_chains, False):
                chains = lm.safe(lambda: list(device.chains), [])
                self._sync_ordered(did, "chain", chains, None, self._adopt_chain, lambda o, p: True)
                for chain in chains:
                    cid = self.id_of(chain)
                    if cid is not None:
                        self._sync_devices(cid)

    def _live_arrangement_clips(self, track):
        """Every arrangement clip on the track, recording ones too. Main, group and return tracks
        have none, and Live raises if you ask them, so don't."""
        if lm.track_kind(track, self.song) in lm.NO_ARRANGEMENT:
            return []
        return lm.safe(lambda: list(track.arrangement_clips), [])

    def _arrangement_clips(self, track):
        clips = self._live_arrangement_clips(track)
        return sorted([c for c in clips if not lm.safe(lambda: c.is_recording, False)],
                      key=lambda c: c.start_time)

    def _sync_clips(self, tid):
        track = self.live[tid]
        # Clips still recording aren't shared yet; hear about it when they finish.
        for clip in self._live_arrangement_clips(track):
            if lm.safe(lambda: clip.is_recording, False):
                self._listen(clip, "is_recording", self._marker(tid, "clips"))
        for slot in lm.safe(lambda: list(track.clip_slots), []):
            if slot.has_clip and lm.safe(lambda: slot.clip.is_recording, False):
                self._listen(slot.clip, "is_recording", self._marker(tid, "clips"))
        known = self.mirror.children(tid, "clip")
        seen = set()
        live_clips = []
        for clip in self._arrangement_clips(track):
            live_clips.append(("a", clip, None))
        scenes = list(self.song.scenes)
        for i, slot in enumerate(lm.safe(lambda: list(track.clip_slots), [])):
            if slot.has_clip and not lm.safe(lambda: slot.clip.is_recording, False):
                scene_id = self.id_of(scenes[i]) if i < len(scenes) else None
                live_clips.append(("s", slot.clip, scene_id))
        unbound = []
        for where, clip, scene_id in live_clips:
            eid = self.id_of(clip)
            if eid is not None and eid in known:
                seen.add(eid)
                p = self.mirror.props(eid)
                if where == "s" and p.get("scene") != scene_id:
                    self.mirror.set(eid, {"scene": scene_id})
                    self._emit({"k": "s", "id": eid, "v": {"scene": scene_id}})
                self._sync_props(eid)
            else:
                unbound.append((where, clip, scene_id))
        missing = [e for e in known if e not in seen and e in self.live]
        for item in list(unbound):
            where, clip, scene_id = item
            for eid in missing:
                p = self.mirror.props(eid)
                if p.get("where") != where:
                    continue
                if where == "a" and lm.same(p.get("start"), lm.num(clip.start_time)) or \
                        where == "s" and p.get("scene") == scene_id:
                    self._bind(eid, clip)
                    self.notes.pop(eid, None)
                    if clip.is_midi_clip:
                        self._bind_notes(eid, clip, None)
                    seen.add(eid)
                    missing.remove(eid)
                    unbound.remove(item)
                    self._sync_props(eid)
                    self._sync_notes(eid)
                    break
        for eid in missing:
            self._emit({"k": "r", "id": eid})
            self._unbind_subtree(eid)
            self.mirror.remove(eid)
        for where, clip, scene_id in unbound:
            self._adopt_clip(tid, clip, where, scene_id)

    def _sync_notes(self, cid):
        clip = self.live.get(cid)
        if clip is None or not lm.safe(lambda: clip.is_midi_clip, False):
            return
        live_notes = lm.read_notes(clip)
        mapping = self.notes.setdefault(cid, {})
        for nid, props in live_notes.items():
            eid = mapping.get(nid)
            if eid is not None and self.mirror.has(eid):
                self._diff_and_emit(eid, props)
            else:
                eid = new_id()
                mapping[nid] = eid
                self.mirror.create(eid, "note", cid, props)
                self._emit({"k": "c", "id": eid, "t": "note", "p": cid, "v": props})
        for nid in [n for n in mapping if n not in live_notes]:
            eid = mapping.pop(nid)
            if self.mirror.has(eid):
                self._emit({"k": "r", "id": eid})
                self.mirror.remove(eid)

    # --- adopting new Live objects (they become shared)

    def _adopt(self, eid, etype, parent, obj, props):
        self.mirror.create(eid, etype, parent, props)
        self._bind(eid, obj)
        self._emit({"k": "c", "id": eid, "t": etype, "p": parent, "v": dict(props)})

    def _adopt_scene(self, parent, obj, pos):
        eid = new_id()
        props = lm.read_scene(obj)
        props["pos"] = pos
        self._adopt(eid, "scene", "song", obj, props)
        return eid

    def _adopt_track(self, parent, track, pos, eid=None):
        eid = eid or self._take_pending_idea_id(track) or new_id()
        props = lm.read_track(track, lm.track_kind(track))
        props["pos"] = pos
        self._adopt(eid, "track", "song", track, props)
        for i, device in enumerate(lm.safe(lambda: list(track.devices), [])):
            self._adopt_device(eid, device, float(i))
        for clip in self._arrangement_clips(track):
            self._adopt_clip(eid, clip, "a", None)
        scenes = list(self.song.scenes)
        for i, slot in enumerate(lm.safe(lambda: list(track.clip_slots), [])):
            if slot.has_clip:
                self._adopt_clip(eid, slot.clip, "s", self.id_of(scenes[i]) if i < len(scenes) else None)
        if self.caps.get("data"):
            try:
                track.set_data("tutti_id", eid)
            except Exception:
                pass
        return eid

    def _adopt_return(self, parent, track, pos):
        eid = new_id()
        props = lm.read_track(track, "return")
        props["pos"] = pos
        self._adopt(eid, "track", "song", track, props)
        for i, device in enumerate(lm.safe(lambda: list(track.devices), [])):
            self._adopt_device(eid, device, float(i))
        return eid

    def _adopt_master(self):
        track = self.song.master_track
        eid = "master"
        if self.mirror.has(eid):
            self._bind(eid, track)
            return eid
        props = lm.read_track(track, "master")
        self._adopt(eid, "track", "song", track, props)
        for i, device in enumerate(lm.safe(lambda: list(track.devices), [])):
            self._adopt_device(eid, device, float(i))
        return eid

    def _adopt_device(self, parent, device, pos):
        eid = new_id()
        props = lm.read_device(device)
        props["pos"] = pos
        self._adopt(eid, "device", parent, device, props)
        if lm.safe(lambda: device.can_have_chains, False):
            for k, chain in enumerate(lm.safe(lambda: list(device.chains), [])):
                self._adopt_chain(eid, chain, float(k))
        return eid

    def _adopt_chain(self, parent, chain, pos):
        eid = new_id()
        self._adopt(eid, "chain", parent, chain, {"name": chain.name, "pos": pos})
        for i, device in enumerate(lm.safe(lambda: list(chain.devices), [])):
            self._adopt_device(eid, device, float(i))
        return eid

    def _adopt_clip(self, tid, clip, where, scene_id):
        eid = new_id()
        props = lm.read_clip(clip, where)
        if where == "s":
            props["scene"] = scene_id
        self._adopt(eid, "clip", tid, clip, props)
        if clip.is_midi_clip:
            mapping = self.notes.setdefault(eid, {})
            for nid, nprops in lm.read_notes(clip).items():
                note_id = new_id()
                mapping[nid] = note_id
                self.mirror.create(note_id, "note", eid, nprops)
                self._emit({"k": "c", "id": note_id, "t": "note", "p": eid, "v": nprops})
        return eid

    # ------------------------------------------------------------------ structure map (snapshots)

    def structure_map(self):
        path_map = {}
        notes_map = {}
        song = self.song

        def add(path, obj):
            eid = self.id_of(obj)
            if eid is not None:
                path_map[path] = eid
            return eid

        for i, scene in enumerate(song.scenes):
            add("S%d" % i, scene)
        for prefix, tracks in (("T", list(song.tracks)), ("R", list(song.return_tracks)), ("M", [song.master_track])):
            for i, track in enumerate(tracks):
                base = prefix if prefix == "M" else "%s%d" % (prefix, i)
                if add(base, track) is None:
                    continue
                self._map_devices(base, lm.safe(lambda: list(track.devices), []), path_map)
                for j, clip in enumerate(self._arrangement_clips(track)):
                    cid = add("%s/A%d" % (base, j), clip)
                    if cid and clip.is_midi_clip:
                        notes_map[cid] = self._note_order(cid, clip)
                for j, slot in enumerate(lm.safe(lambda: list(track.clip_slots), [])):
                    if slot.has_clip:
                        cid = add("%s/L%d" % (base, j), slot.clip)
                        if cid and slot.clip.is_midi_clip:
                            notes_map[cid] = self._note_order(cid, slot.clip)
        for i, cue in enumerate(getattr(song, "cue_points", [])):
            add("Q%d" % i, cue)
        return path_map, notes_map

    def _map_devices(self, base, devices, path_map):
        for j, device in enumerate(devices):
            path = "%s/D%d" % (base, j)
            if self.id_of(device) is None:
                continue
            path_map[path] = self.id_of(device)
            if lm.safe(lambda: device.can_have_chains, False):
                for k, chain in enumerate(lm.safe(lambda: list(device.chains), [])):
                    cpath = "%s/C%d" % (path, k)
                    if self.id_of(chain) is not None:
                        path_map[cpath] = self.id_of(chain)
                        self._map_devices(cpath, lm.safe(lambda: list(chain.devices), []), path_map)

    def _note_order(self, cid, clip):
        mapping = self.notes.get(cid, {})
        live_notes = lm.read_notes(clip)
        ordered = sorted(live_notes.items(), key=lambda kv: lm.note_key(kv[1]))
        return [mapping.get(nid) for nid, _ in ordered if mapping.get(nid)]

    # ------------------------------------------------------------------ presence and play together

    def _presence(self):
        song = self.song
        now = self.clock()
        view = song.view
        track_id = self.id_of(lm.safe(lambda: view.selected_track))
        clip_id = self.id_of(lm.safe(lambda: view.detail_clip))
        playing = bool(song.is_playing)
        state = (track_id, clip_id, playing, bool(lm.safe(lambda: song.record_mode, False)))
        if state == self._presence_state and not (playing and now - self._presence_sent >= PRESENCE_EVERY):
            return
        self._presence_state = state
        self._presence_sent = now
        if self.bridge.connected:
            self.bridge.send({"type": "presence", "track": track_id, "clip": clip_id, "playing": playing,
                              "rec": state[3] and playing, "time": lm.num(song.current_song_time),
                              "view": lm.safe(lambda: self.app.view.focused_document_view, "")})

    def _play_together(self):
        if self.play_at is None:
            return
        pos, at = self.play_at
        if self.clock() * 1000.0 + 50.0 < at:  # within half a tick: go now
            return
        self.play_at = None
        try:
            self.song.current_song_time = pos
            self.song.continue_playing()
        except Exception as e:
            self.log("couldn't start playback: %r" % (e,))

    # ------------------------------------------------------------------ suggestion mode

    _pending_idea = None

    def _take_pending_idea_id(self, track):
        if self._pending_idea is not None and self._pending_idea[1] == track:
            eid = self._pending_idea[0]
            self._pending_idea = None
            return eid
        return None

    def make_idea(self, track_id, name, color):
        track = self.live.get(track_id)
        if track is None or not self.caps.get("duplicate_track"):
            self.bridge.send({"type": "idea_created", "orig": track_id, "track": None,
                              "error": "This Live can't copy tracks for Tutti."})
            return
        index = index_of(self.song.tracks, track)
        self.song.duplicate_track(index)
        copy = self.song.tracks[index + 1]
        if name:
            copy.name = name
        if color is not None:
            copy.color = int(color)
        copy.mute = True
        eid = new_id()
        self._pending_idea = (eid, copy)
        self._sync_song_structure(None)
        self._pending_idea = None
        self._flush_ops()  # the copy's creates must reach the app before we name it
        self.bridge.send({"type": "idea_created", "orig": track_id, "track": eid if self.mirror.has(eid) else None})

    # ------------------------------------------------------------------ preview -> full quality

    def swap_file(self, eid, path):
        if not self.mirror.has(eid) or not path:
            return
        self.mirror.set(eid, {"path": path})
        if eid in self.live:
            self._recreate_clip(eid)
        else:
            self._realize(eid)
