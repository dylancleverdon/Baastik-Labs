#!/usr/bin/env python3
"""End-to-end test: several real tutti-peer processes on this machine, each with the real Remote
Script running against a fake Live. Exercises start, join with approval, snapshot download with
audio previews, live edits both ways, recordings, Player 1 handover and automatic takeover.

    python3 tools/e2e/run_e2e.py build      # path to the CMake build folder
"""

import gzip
import json
import math
import os
import queue
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "remote_script", "tests", "fakelive"))
sys.path.insert(0, os.path.join(ROOT, "remote_script"))

import Live  # noqa: E402  (the fake)
from Tutti.bridge import Bridge  # noqa: E402
from Tutti.engine import Engine  # noqa: E402
from Tutti import livemodel as lm  # noqa: E402


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def write_wav(path, seconds=1.5, rate=44100):
    frames = int(seconds * rate)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        data = bytearray()
        for i in range(frames):
            v = int(12000 * math.sin(2 * math.pi * 330 * i / rate))
            data += struct.pack("<hh", v, v)
        w.writeframes(bytes(data))
    return path


def automation(marker):
    """A stand-in for something only a save carries (an automation lane), tagged so we can find it."""
    return ('<AutomationEnvelopes><Envelopes><AutomationEnvelope Id="0"><EnvelopeTarget><PointeeId Value="7"/>'
            '</EnvelopeTarget><Automation><Events><FloatEvent Time="1" Value="0.5" Marker="%s"/></Events></Automation>'
            '</AutomationEnvelope></Envelopes></AutomationEnvelopes>' % marker)


def write_als(path, tracks, audio_files):
    """A minimal Live Set whose track names match the fake Live song."""
    parts = []
    for track in tracks:
        kind, name = track[0], track[1]
        extra = track[2] if len(track) > 2 else ""
        tag = "MidiTrack" if kind == "midi" else "AudioTrack"
        refs = ""
        if kind == "audio":
            for f in audio_files:
                refs += ('<AudioClip><SampleRef><FileRef><RelativePathType Value="0"/><RelativePath Value=""/>'
                         '<Path Value="%s"/><Type Value="1"/><OriginalFileSize Value="%d"/></FileRef></SampleRef></AudioClip>'
                         % (f, os.path.getsize(f)))
        parts.append('<%s Id="1"><Name><EffectiveName Value="%s"/><UserName Value="%s"/></Name>%s'
                     '<DeviceChain><MainSequencer>%s</MainSequencer></DeviceChain></%s>' % (tag, name, name, extra, refs, tag))
    xml = ('<?xml version="1.0" encoding="UTF-8"?>\n<Ableton MajorVersion="5" MinorVersion="12.0_12117" '
           'Creator="Ableton Live 12.3.2"><LiveSet><Tracks>%s</Tracks><MasterTrack><Name>'
           '<EffectiveName Value="Main"/></Name></MasterTrack></LiveSet></Ableton>' % "".join(parts))
    with gzip.open(path, "wb") as f:
        f.write(xml.encode("utf-8"))


class Peer(object):
    """One computer: a tutti-peer process plus its fake Live with the real Remote Script."""

    def __init__(self, name, exe, workdir, port, local_port, others):
        self.name = name
        self.port = port
        self.local_port = local_port
        self.data = os.path.join(workdir, name)
        os.makedirs(self.data, exist_ok=True)
        args = [exe, "--data", self.data, "--songs", os.path.join(self.data, "Songs"), "--port", str(port),
                "--local-port", str(local_port), "--bind", "127.0.0.1", "--name", name, "--no-tailscale"]
        for o in others:
            args += ["--peer", "127.0.0.1:%d" % o]
        self.proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                     text=True, bufsize=1)
        self.events = queue.Queue()
        self.opened = []
        threading.Thread(target=self._read, daemon=True).start()
        ready = self.wait_event("ready", 20)
        self.peer_id = ready["peer"]
        self.song = None
        self.engine = None

    def _read(self):
        for line in self.proc.stdout:
            try:
                ev = json.loads(line)
            except ValueError:
                continue
            if ev.get("event") == "open":
                self.opened.append(ev["path"])
            self.events.put(ev)

    def cmd(self, **kw):
        self.proc.stdin.write(json.dumps(kw) + "\n")
        self.proc.stdin.flush()

    def wait_event(self, kind, timeout=10):
        end = time.time() + timeout
        stash = []
        try:
            while time.time() < end:
                try:
                    ev = self.events.get(timeout=0.1)
                except queue.Empty:
                    continue
                if ev.get("event") == kind:
                    return ev
                stash.append(ev)
        finally:
            for ev in stash:
                self.events.put(ev)
        raise AssertionError("%s: no %s event" % (self.name, kind))

    def state(self):
        # Drop stale state events first.
        while True:
            try:
                self.events.get_nowait()
            except queue.Empty:
                break
        self.cmd(cmd="state")
        return self.wait_event("state", 10)

    plugins = ()

    def open_live(self, song):
        """Live opens a Set: a new Song and a fresh Remote Script instance."""
        if self.engine is not None:
            self.engine.shutdown()
        self.song = song
        app = Live.Application.Application(song, self.plugins)
        self.engine = Engine(song, app, Live, Bridge(port=self.local_port), clock=time.time)

    def tick(self):
        if self.engine is not None:
            self.engine.tick()

    def kill(self):
        self.proc.kill()
        if self.engine is not None:
            self.engine.shutdown()
            self.engine = None

    def quit(self):
        try:
            if self.proc.poll() is None:
                self.cmd(cmd="quit")
                self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()
        try:
            self.proc.stdin.close()
        except Exception:
            pass
        if self.engine is not None:
            self.engine.shutdown()


class FakeStandIn(object):
    """The Tutti Stand-in plugin as the Tutti app sees it: a local connection with a Link id."""

    def __init__(self, port, link):
        from Tutti import framing
        self.framing = framing
        self.sock = socket.create_connection(("127.0.0.1", port))
        self.sock.setblocking(False)
        self.decoder = framing.Decoder()
        self.link = link
        self.last = None
        self.send({"type": "hello", "role": "standin", "link": link})

    def send(self, message):
        self.sock.setblocking(True)
        self.sock.sendall(self.framing.encode(message))
        self.sock.setblocking(False)

    def poll(self):
        try:
            while True:
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                self.decoder.feed(chunk)
        except BlockingIOError:
            pass
        while True:
            m = self.decoder.next()
            if m is None:
                break
            if m.get("type") == "standin":
                self.last = m
        return self.last

    def close(self):
        self.sock.close()


def demo_song(audio_path):
    song = Live.Song.Song()
    drums = song.create_midi_track(0)
    drums.name = "Drums"
    bass = song.create_midi_track(1)
    bass.name = "Bass"
    vox = song.create_audio_track(2)
    vox.name = "Vox"
    bass.ui_add_device("Operator", params=[("Filter Freq", 0.6, 0.0, 1.0)])
    clip = drums.ui_draw_arrangement_clip(0.0, 4.0)
    for i in range(4):
        clip.ui_add_note(36, float(i), 0.25)
    vox.ui_record_audio_clip(8.0, audio_path, 4.0)
    return song


def clone(src):
    sys.path.insert(0, os.path.join(ROOT, "remote_script", "tests"))
    from test_engine import clone_song
    return clone_song(src)


class Harness(object):
    def __init__(self, exe):
        self.dir = tempfile.mkdtemp(prefix="tutti-e2e-")
        names = ["alice", "bob", "cara"]
        ports = [free_port() for _ in names]
        locals_ = [free_port() for _ in names]
        self.peers = {}
        for i, n in enumerate(names):
            self.peers[n] = Peer(n, exe, self.dir, ports[i], locals_[i], [p for j, p in enumerate(ports) if j != i])

    def run(self, seconds, until=None):
        end = time.time() + seconds
        while time.time() < end:
            for p in self.peers.values():
                if p.proc.poll() is None:
                    p.tick()
            if until is not None and until():
                return True
            time.sleep(0.02)
        return until is None

    def close(self):
        for p in self.peers.values():
            p.quit()
        shutil.rmtree(self.dir, ignore_errors=True)


def check(cond, what):
    if not cond:
        raise AssertionError(what)
    print("  ok: " + what)


def docs_equal(peers):
    states = [p.state() for p in peers]
    docs = [json.dumps(s["doc"], sort_keys=True) for s in states]
    return all(d == docs[0] for d in docs), states


def live_matches_doc(peer, state):
    """The fake Live of a peer holds the same tracks/clips/notes as the shared document."""
    ents = {e["id"]: e for e in state["doc"]["e"]}
    tracks = sorted([e for e in ents.values() if e["t"] == "track" and e["v"].get("kind") in ("midi", "audio")],
                    key=lambda e: (e["v"].get("pos", 0), e["id"]))
    live_names = [t.name for t in peer.song.tracks]
    if [t["v"].get("name") for t in tracks] != live_names:
        return False, "track names %s vs %s" % ([t["v"].get("name") for t in tracks], live_names)
    for t, lt in zip(tracks, peer.song.tracks):
        clips = [e for e in ents.values() if e["t"] == "clip" and e["p"] == t["id"] and e["v"].get("where") == "a"]
        if len(clips) != len(lt.arrangement_clips):
            return False, "clip count on %s" % lt.name
        for c in clips:
            notes = sorted(lm.note_key(e["v"]) for e in ents.values() if e["t"] == "note" and e["p"] == c["id"])
            live = [lc for lc in lt.arrangement_clips if abs(lc.start_time - c["v"].get("start", -1)) < 1e-6]
            if not live:
                return False, "clip at %s missing on %s" % (c["v"].get("start"), lt.name)
            if lt.has_midi_input and sorted(lm.note_key(p) for p in lm.read_notes(live[0]).values()) != notes:
                return False, "notes differ on %s" % lt.name
    return True, ""


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build")
    exe = os.path.join(build, "tools", "cli", "tutti-peer")
    if os.name == "nt":
        exe += ".exe"
    h = Harness(exe)
    alice, bob, cara = h.peers["alice"], h.peers["bob"], h.peers["cara"]
    try:
        print("0. Tutti installs its Remote Script over an older one in place")
        lib = os.path.join(h.dir, "Remote Scripts", "Tutti")
        os.makedirs(os.path.join(lib, "__pycache__"))
        for name, text in (("__init__.py", "old"), ("gone.py", "removed in the new version"),
                           ("__pycache__/engine.cpython-37.pyc", "stale")):
            with open(os.path.join(lib, name), "w") as f:
                f.write(text)
        src = os.path.join(ROOT, "remote_script", "Tutti")
        alice.cmd(cmd="install_script", **{"from": src, "to": lib})
        ev = alice.wait_event("installed")
        check(ev and ev["ok"], "the install reports success")
        check(open(os.path.join(lib, "__init__.py")).read() == open(os.path.join(src, "__init__.py")).read()
              and open(os.path.join(lib, "engine.py")).read() == open(os.path.join(src, "engine.py")).read(),
              "the new files replaced the old ones")
        check(not os.path.exists(os.path.join(lib, "gone.py")) and
              not os.path.exists(os.path.join(lib, "__pycache__", "engine.cpython-37.pyc")),
              "files the new version doesn't have, and old bytecode, are gone")

        print("1. Alice starts a session")
        take = write_wav(os.path.join(h.dir, "vox take 1.wav"))
        song = demo_song(take)
        als = os.path.join(h.dir, "Night Drive.als")
        write_als(als, [("midi", "Drums"), ("midi", "Bass"), ("audio", "Vox")], [take])
        alice.open_live(song)
        h.run(1.0)
        alice.cmd(cmd="start", als=als)
        check(h.run(15, lambda: len(alice.opened) > 0), "Alice's Tutti opened its copy of the song in Live")
        h.run(1.0)
        check(alice.state()["role"] != "player1", "Tutti waits for Live to load the copy, not the Set already open")
        alice.open_live(song)
        check(h.run(15, lambda: alice.state()["role"] == "player1"), "Alice is Player 1")
        snapshot_song = clone(alice.song)  # what the song file holds right now

        print("2. Bob and Cara join; Alice lets them in")
        h.run(2.0)
        for guest in (bob, cara):
            check(h.run(10, lambda: any(p["hosting"] for p in guest.state()["peers"])), guest.name + " sees the session")
            if guest is bob:
                # Bob already has a Set open whose tracks have the same names (an Untitled Set
                # often does): Tutti must wait for Live to open its file, not bind this one.
                guest.open_live(clone(snapshot_song))
                h.run(1.0)
            guest.cmd(cmd="join", peer=alice.peer_id)
            check(h.run(10, lambda: guest.peer_id in alice.state()["join_requests"]), "Alice is asked about " + guest.name)
            alice.cmd(cmd="approve", peer=guest.peer_id, allow=True)
            check(h.run(10, lambda: guest.state()["role"] == "guest"), guest.name + " is in")
            check(h.run(30, lambda: len(guest.opened) > 0), guest.name + "'s Tutti opened the song in Live")
            if guest is bob:
                h.run(2.0)
                check(not guest.state()["live_bound"], "Bob's Tutti doesn't bind the Set that was already open")
            guest.open_live(clone(snapshot_song))
            check(h.run(15, lambda: guest.state()["live_bound"]), guest.name + "'s Live is in sync")

        print("3. Edits flow both ways")
        alice.song.tracks[1].name = "Sub Bass"
        alice.song.tracks[0].arrangement_clips[0].ui_add_note(42, 0.5, 0.25)
        bob.song.tracks[1].devices[0].parameters[1].value = 0.25
        t = cara.song.create_midi_track(3)
        t.name = "Keys"
        c = t.create_midi_clip(4.0, 4.0)
        c.add_new_notes((Live.Clip.MidiNoteSpecification(60, 0.0, 1.0),))
        ok = h.run(10, lambda: [x.name for x in bob.song.tracks] == ["Drums", "Sub Bass", "Vox", "Keys"]
                   and [x.name for x in alice.song.tracks] == ["Drums", "Sub Bass", "Vox", "Keys"])
        check(ok, "everyone sees Sub Bass and Keys")
        check(abs(alice.song.tracks[1].devices[0].parameters[1].value - 0.25) < 1e-6, "Bob's knob reached Alice")
        h.run(1.0)
        same, states = docs_equal([alice, bob, cara])
        check(same, "everyone has the same document")
        for p, s in zip((alice, bob, cara), states):
            good, why = live_matches_doc(p, s)
            check(good, p.name + "'s Live matches the document " + why)

        print("4. Bob records; the others get a preview, then the full take")
        take2 = write_wav(os.path.join(h.dir, "bob take.wav"), seconds=2.0)
        bob.song.tracks[2].ui_record_audio_clip(16.0, take2, 4.0)

        def has_take(p, full):
            clips = [c for c in p.song.tracks[2].arrangement_clips if c.start_time == 16.0]
            if not clips:
                return False
            path = clips[0].file_path or ""
            return (not full) or ("(preview)" not in path and path.endswith("bob take.wav"))
        check(h.run(20, lambda: has_take(alice, False) and has_take(cara, False)), "the take appears for Alice and Cara")
        check(h.run(30, lambda: has_take(alice, True) and has_take(cara, True)), "and is swapped to full quality")

        print("4b. Saving shares what Live can't sync live; saves are merged by track")

        def save(peer, track_index, marker):
            """What pressing Cmd+S does: Live rewrites the song file."""
            path = peer.state()["song_file"]
            tracks = []
            for i, t in enumerate(peer.song.tracks):
                tracks.append(("midi" if t.has_midi_input else "audio", t.name,
                               automation(marker) if i == track_index else ""))
            write_als(path, tracks, [])

        save(bob, 0, "bob-drums")
        check(h.run(20, lambda: alice.state()["update_available"] and cara.state()["update_available"]),
              "Alice and Cara are told an update is available")
        check(not bob.state()["update_available"], "Bob's own save needs no update")

        opened_before = len(cara.opened)
        save(cara, 1, "cara-bass")
        check(h.run(20, lambda: len(cara.opened) > opened_before), "Cara's save is merged and her Live reloads")
        cara.open_live(clone(cara.song))
        check(h.run(15, lambda: cara.state()["live_bound"] and not cara.state()["update_available"]),
              "Cara is in sync with the merged song")

        snaps_dir = os.path.join(alice.data, "sessions")
        newest = None
        for root, _, files in os.walk(snaps_dir):
            for f in files:
                if f.endswith(".json"):
                    meta = json.load(open(os.path.join(root, f)))
                    if newest is None or meta.get("at", "") > newest.get("at", ""):
                        newest = meta
        check(newest is not None and newest.get("merged"), "the newest version is a merge")
        als_hash = newest["als"]
        store = os.path.join(alice.data, "Store", "files", als_hash[:2], als_hash)
        merged_xml = gzip.open(os.path.join(store, os.listdir(store)[0] if os.listdir(store)[0] != "meta.json"
                                            else os.listdir(store)[1])).read().decode("utf-8")
        check("bob-drums" in merged_xml and "cara-bass" in merged_xml, "the merge kept Bob's and Cara's changes")
        check(len(cara.state()["notices"]) > 0, "Cara was told what happened")

        print("4b2. Bob uses Save As into the session folder: Tutti follows the new file")
        bob_opened = len(bob.opened)
        old_file = bob.state()["song_file"]
        new_file = os.path.join(os.path.dirname(old_file), "Night Drive v2.als")
        tracks = [("midi" if t.has_midi_input else "audio", t.name, automation("bob-saveas") if i == 2 else "")
                  for i, t in enumerate(bob.song.tracks)]
        write_als(new_file, tracks, [])
        check(h.run(15, lambda: bob.state()["song_file"] == new_file), "Bob's Tutti now shares Night Drive v2.als")
        check(h.run(20, lambda: alice.state()["update_available"]), "and Alice is told there's an update")
        if h.run(5, lambda: len(bob.opened) > bob_opened):  # merged with others' saves: his Live reloads
            bob.open_live(clone(bob.song))
        check(h.run(15, lambda: bob.state()["live_bound"]), "Bob's Live is in sync after the Save As")

        print("4c. Bob owns a plugin Alice doesn't: the Stand-in shares his recording")
        bob.plugins = ("Serum 2",)
        bob.open_live(bob.song)  # restart Bob's script so it reports his plugins
        check(h.run(15, lambda: bob.state()["live_bound"]), "Bob's Live is back in sync")
        bob.song.tracks[1].ui_add_device("Serum 2", class_name="PluginDevice", params=[("Cutoff", 0.5, 0.0, 1.0)])
        bob.song.tracks[1].ui_add_device("Tutti Stand-in", class_name="PluginDevice", params=[("Link", 0.424242, 0.0, 1.0)])
        check(h.run(10, lambda: any("added Serum 2" in n for n in bob.state()["notices"])), "Bob is told to save to share Serum 2")
        check(h.run(10, lambda: any("bob added Serum 2" in n for n in alice.state()["notices"])),
              "Alice is told Serum 2 arrives when Bob saves")

        def has_standin(p):
            return any("Tutti Stand-in" in json.dumps(e["v"]) for e in p.state()["doc"]["e"] if e["t"] == "device")
        check(h.run(10, lambda: has_standin(alice)), "the Stand-in is in the shared song")

        bob_si = FakeStandIn(bob.local_port, 0.424242)
        alice_si = FakeStandIn(alice.local_port, 0.424242)
        check(h.run(10, lambda: (bob_si.poll() or {}).get("mode") == "capture"), "Bob's Stand-in records (he owns Serum 2)")
        check(h.run(10, lambda: (alice_si.poll() or {}).get("mode") == "play"), "Alice's Stand-in plays (she doesn't)")

        take3 = write_wav(os.path.join(h.dir, "standin take.wav"), seconds=2.0)
        bob_si.send({"type": "segment", "link": 0.424242, "path": take3, "start": 0.0, "len": 4.0, "tempo": 120.0,
                     "rate": 44100.0})

        def alice_gets_segment():
            m = alice_si.poll() or {}
            segs = m.get("segments") or []
            return len(segs) == 1 and os.path.exists(segs[0]["path"]) and not segs[0]["stale"]
        check(h.run(20, alice_gets_segment), "Alice's Stand-in gets Bob's recording")

        bob.song.tracks[1].ui_draw_arrangement_clip(16.0, 4.0).ui_add_note(50, 2.0, 0.5)

        def stale():
            m = alice_si.poll() or {}
            segs = m.get("segments") or []
            return len(segs) == 1 and segs[0]["stale"]
        check(h.run(15, stale), "it's marked out of date once Bob changes the notes")
        bob_si.close()
        alice_si.close()

        print("5. Cara asks to be Player 1; Alice says yes")
        cara.cmd(cmd="request_p1")
        check(h.run(10, lambda: cara.peer_id in alice.state()["p1_requests"]), "Alice sees the request")
        alice.cmd(cmd="answer_p1", peer=cara.peer_id, allow=True)
        check(h.run(10, lambda: cara.state()["role"] == "player1"), "Cara is Player 1")
        alice.song.tempo = 126.0
        check(h.run(10, lambda: bob.song.tempo == 126.0 and cara.song.tempo == 126.0), "edits still flow")

        print("6. Cara's computer crashes; Alice (next in line) takes over")
        cara.kill()
        check(h.run(30, lambda: alice.state()["role"] == "player1" or bob.state()["role"] == "player1"),
              "someone took over")
        bob.song.tracks[0].name = "Kit"
        check(h.run(10, lambda: alice.song.tracks[0].name == "Kit"), "edits flow under the new Player 1")
        same, _ = docs_equal([alice, bob])
        check(same, "Alice and Bob agree")
        print("All end-to-end checks passed.")
        return 0
    except AssertionError:
        # Say what each Tutti was doing, so a failure in CI can be diagnosed from the log.
        for p in h.peers.values():
            code = p.proc.poll()
            print("--- %s's Tutti is %s" % (p.name, "running" if code is None else "gone (exit %s)" % code))
            log = os.path.join(p.data, "Logs", "tutti.log")
            print("--- %s's tutti.log (last 60 lines)" % p.name)
            try:
                with open(log) as f:
                    print("".join(f.readlines()[-60:]))
            except OSError:
                print("(no log)")
        raise
    finally:
        h.close()


if __name__ == "__main__":
    sys.exit(main())
