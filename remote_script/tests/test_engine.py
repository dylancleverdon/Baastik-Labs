import Live
from conftest import make_engine

from Tutti import livemodel as lm


# ---------------------------------------------------------------- helpers

def demo_song():
    song = Live.Song.Song()
    song.create_scene(-1)
    drums = song.create_midi_track(0)
    drums.name = "Drums"
    bass = song.create_midi_track(1)
    bass.name = "Bass"
    vox = song.create_audio_track(2)
    vox.name = "Vox"
    song.create_return_track()
    drums.ui_add_device("Drum Rack", params=[("Volume", 0.8, 0.0, 1.0)])
    bass.ui_add_device("Operator", params=[("Filter Freq", 0.6, 0.0, 1.0), ("Res", 0.2, 0.0, 1.0)])
    bass.ui_add_device("Reverb")
    c = drums.ui_draw_arrangement_clip(0.0, 4.0)
    c.name = "Beat"
    for i in range(4):
        c.ui_add_note(36, float(i), 0.25)
    c2 = bass.ui_draw_arrangement_clip(8.0, 8.0)
    for p, t in ((40, 0.0), (43, 1.0), (45, 2.5)):
        c2.ui_add_note(p, t, 0.5)
    vox.ui_record_audio_clip(16.0, "/tmp/vox-take1.wav")
    drums.clip_slots[0].create_clip(2.0)
    drums.clip_slots[0].clip.ui_add_note(38, 0.5, 0.25)
    song.tempo = 124.0
    return song


def clone_song(src):
    """What another computer's Live looks like after opening the same .als."""
    song = Live.Song.Song()
    while len(song.scenes) < len(src.scenes):
        song.create_scene(-1)
    for i, s in enumerate(src.scenes):
        song.scenes[i].name = s.name
    for _ in src.return_tracks:
        song.create_return_track()
    for i, t in enumerate(src.tracks):
        if t.kind == "group":  # joiners open the saved song, which has the same groups
            nt = song.ui_add_track("group", i)
        else:
            nt = song.create_midi_track(i) if t.has_midi_input else song.create_audio_track(i)
        copy_track(t, nt)
    copy_track(src.master_track, song.master_track)
    for i, r in enumerate(src.return_tracks):
        copy_track(r, song.return_tracks[i])
    song.tempo = src.tempo
    return song


def copy_track(t, nt):
    nt.name = t.name
    nt.color = t.color
    if t.kind != "master":  # the Main track has no mute
        nt.mute = t.mute
    nt.mixer_device.volume.value = t.mixer_device.volume.value
    nt.mixer_device.panning.value = t.mixer_device.panning.value
    for d in t.devices:
        nd = nt.ui_add_device(d.name, params=[], class_name=d.class_name)
        nd.__dict__["_parameters"] = [Live.DeviceParameter.DeviceParameter(p.name, p.value, p.min, p.max)
                                      for p in d.parameters]
    for c in (t.arrangement_clips if t.kind not in Live.NO_ARRANGEMENT else []):
        if c.is_midi_clip:
            nc = nt.ui_draw_arrangement_clip(c.start_time, c.length)
        else:
            nc = nt.ui_record_audio_clip(c.start_time, c.file_path, c.length)
        nc.name = c.name
        for n in c._notes.values():
            nc.ui_add_note(n.pitch, n.start_time, n.duration, n.velocity)
    for i, slot in enumerate(t.clip_slots):
        if slot.has_clip:
            nt.clip_slots[i].create_clip(slot.clip.length)
            for n in slot.clip._notes.values():
                nt.clip_slots[i].clip.ui_add_note(n.pitch, n.start_time, n.duration, n.velocity)


def verify(engine):
    """The engine's Live matches its mirror, and nothing in Live is unaccounted for."""
    song = engine.song
    problems = []
    for eid, obj in engine.live.items():
        etype = engine.mirror.type_of(eid)
        props = engine.mirror.props(eid)
        if etype == "track":
            live = lm.read_track(obj, props.get("kind"))
        elif etype == "device":
            live = lm.read_device(obj)
        elif etype == "clip":
            live = lm.read_clip(obj, props.get("where"))
        elif etype == "song":
            live = lm.read_song(obj)
        elif etype == "scene":
            live = lm.read_scene(obj)
        else:
            continue
        for k, v in live.items():
            if k in ("path",):
                continue
            if k in props and not lm.same(props[k], v):
                problems.append((etype, eid, k, props[k], v))
        if etype == "clip" and obj.is_midi_clip:
            doc_notes = sorted(lm.note_key(engine.mirror.props(n)) for n in engine.mirror.children(eid, "note"))
            live_notes = sorted(lm.note_key(p) for p in lm.read_notes(obj).values())
            if doc_notes != live_notes:
                problems.append(("notes", eid, doc_notes, live_notes))
    for t in list(song.tracks) + list(song.return_tracks):
        if engine.id_of(t) is None:
            problems.append(("unbound track", t.name))
        for d in t.devices:
            if engine.id_of(d) is None:
                problems.append(("unbound device", d.name))
        for c in (t.arrangement_clips if t.kind not in Live.NO_ARRANGEMENT else []):
            if engine.id_of(c) is None:
                problems.append(("unbound clip", c.start_time))
    assert not problems, problems


class Hub(object):
    """Relays ops between engines (no conflict rules: enough to test the Live side)."""

    def __init__(self, engines):
        self.engines = engines

    def pump(self, rounds=3):
        for _ in range(rounds):
            for e in self.engines:
                e.tick()
            for e in self.engines:
                ops = e.bridge.ops()
                if ops:
                    for other in self.engines:
                        if other is not e:
                            other.bridge.inbox.append({"type": "apply", "ops": ops})


def capture(engine):
    engine.bridge.inbox.append({"type": "capture", "session": "s1"})
    engine.tick()
    msg = engine.bridge.take("captured")[0]
    return msg["doc"], msg["map"], msg["notes"]


def joined_pair():
    a, song_a, _, _ = make_engine(demo_song())
    doc, path_map, notes_map = capture(a)
    b, song_b, _, _ = make_engine(clone_song(song_a))
    b.bridge.inbox.append({"type": "bind", "doc": doc, "map": path_map, "notes": notes_map, "session": "s1"})
    b.tick()
    stats = b.bridge.take("bound")[0]["stats"]
    return a, song_a, b, song_b, stats


# ---------------------------------------------------------------- tests

def test_hello_reports_version_and_capabilities():
    from Tutti.engine import Engine
    from conftest import FakeBridge
    song = demo_song()
    bridge = FakeBridge()
    e = Engine(song, Live.Application.Application(song), Live, bridge)
    e.tick()
    hello = bridge.take("hello")[0]
    assert hello["live"] == "12.3.2"
    assert hello["caps"]["arrangement_create"] is True
    assert hello["fingerprint"]["tracks"] == ["Drums", "Bass", "Vox"]


def test_capture_builds_the_whole_document():
    e, song, bridge, _ = make_engine(demo_song())
    doc, path_map, notes_map = capture(e)
    types = {}
    for ent in doc["e"]:
        types[ent["t"]] = types.get(ent["t"], 0) + 1
    assert types["track"] == 5          # 3 tracks, a return and the master
    assert types["device"] == 3
    assert types["clip"] == 4           # 3 arrangement + 1 session
    assert types["note"] == 8
    assert types["scene"] == 2
    assert path_map["T0"] == e.id_of(song.tracks[0])
    assert path_map["T1/D1"] == e.id_of(song.tracks[1].devices[1])
    assert len(notes_map[path_map["T0/A0"]]) == 4
    assert doc["e"][0]["v"]["tempo"] == 124.0
    verify(e)
    assert bridge.ops() == []           # capturing doesn't send edits


def test_local_edits_become_ops_without_echo():
    e, song, bridge, clock = make_engine(demo_song())
    capture(e)
    bass = song.tracks[1]
    bass.name = "Sub Bass"
    bass.mixer_device.volume.value = 0.5
    bass.devices[0].parameters[1].value = 0.9
    song.tempo = 128.0
    e.tick()
    ops = bridge.ops()
    sets = {(o["id"], k): v for o in ops if o["k"] == "s" for k, v in o["v"].items()}
    tid = e.id_of(bass)
    assert sets[(tid, "name")] == "Sub Bass"
    assert sets[(tid, "vol")] == 0.5
    assert sets[(e.id_of(bass.devices[0]), "p1")] == 0.9
    assert sets[("song", "tempo")] == 128.0
    e.tick()
    assert bridge.ops() == []           # nothing new, nothing repeated


def test_notes_are_tracked_one_by_one():
    e, song, bridge, _ = make_engine(demo_song())
    capture(e)
    clip = song.tracks[0].arrangement_clips[0]
    cid = e.id_of(clip)
    nid = clip.ui_add_note(42, 0.5, 0.25)
    e.tick()
    ops = bridge.ops()
    assert len(ops) == 1 and ops[0]["k"] == "c" and ops[0]["p"] == cid and ops[0]["v"]["p"] == 42
    note_entity = ops[0]["id"]
    clip.ui_move_note(nid, 1.5)
    e.tick()
    ops = bridge.ops()
    assert ops == [{"k": "s", "id": note_entity, "v": {"t": 1.5}}]
    clip.ui_delete_note(nid)
    e.tick()
    assert bridge.ops() == [{"k": "r", "id": note_entity}]


def test_joining_binds_everything_and_matches():
    a, song_a, b, song_b, stats = joined_pair()
    assert stats["matched"] >= 12
    assert stats["created"] == 0 and stats["new"] == 0 and stats["removed"] == 0
    verify(b)
    assert b.mirror.to_json() == a.mirror.to_json()


def test_edits_flow_both_ways():
    a, song_a, b, song_b, _ = joined_pair()
    hub = Hub([a, b])

    song_a.tracks[1].name = "Bass (A)"
    song_a.tracks[0].arrangement_clips[0].ui_add_note(50, 3.5, 0.5)
    song_a.tracks[1].ui_add_device("Saturator", params=[("Drive", 0.3, 0.0, 1.0)])
    hub.pump()
    assert song_b.tracks[1].name == "Bass (A)"
    assert [d.name for d in song_b.tracks[1].devices] == ["Operator", "Reverb", "Saturator"]
    assert any(n.pitch == 50 for n in song_b.tracks[0].arrangement_clips[0]._notes.values())

    t = song_b.create_midi_track(1)
    t.name = "Keys"
    clip = t.create_midi_clip(4.0, 4.0)
    clip.add_new_notes((Live.Clip.MidiNoteSpecification(60, 0.0, 1.0), Live.Clip.MidiNoteSpecification(64, 1.0, 1.0)))
    song_b.tracks[2].mixer_device.volume.value = 0.33
    song_b.delete_track(3)  # Vox
    hub.pump()
    assert [x.name for x in song_a.tracks] == ["Drums", "Keys", "Bass (A)"]
    assert len(song_a.tracks[1].arrangement_clips) == 1
    assert sorted(n.pitch for n in song_a.tracks[1].arrangement_clips[0]._notes.values()) == [60, 64]
    assert song_a.tracks[2].mixer_device.volume.value == 0.33

    for e in (a, b):
        verify(e)
    assert a.mirror.to_json() == b.mirror.to_json()


def test_new_tracks_land_in_the_same_order_everywhere():
    a, song_a, b, song_b, _ = joined_pair()
    hub = Hub([a, b])
    song_a.ui_add_track("midi", 0, "First")
    song_b.ui_add_track("audio", 3, "Last")
    hub.pump()
    names_a = [t.name for t in song_a.tracks]
    names_b = [t.name for t in song_b.tracks]
    assert names_a == names_b
    assert names_a[0] == "First" and names_a[-1] == "Last"


def test_arrangement_clip_move_and_delete():
    a, song_a, b, song_b, _ = joined_pair()
    hub = Hub([a, b])
    clip = song_a.tracks[1].arrangement_clips[0]
    song_a.tracks[1].ui_move_arrangement_clip(clip, 12.0)
    hub.pump()
    moved = song_b.tracks[1].arrangement_clips
    assert len(moved) == 1 and moved[0].start_time == 12.0
    assert len(moved[0]._notes) == 3       # notes came along
    song_b.tracks[0].ui_delete_arrangement_clip(song_b.tracks[0].arrangement_clips[0])
    hub.pump()
    assert song_a.tracks[0].arrangement_clips == []
    verify(a)
    verify(b)


def test_clip_creation_falls_back_without_the_newer_api():
    a, song_a, b, song_b, _ = joined_pair()
    Live.configure(arrangement_create=False)
    hub = Hub([a, b])
    c = song_a.tracks[1].ui_draw_arrangement_clip(32.0, 4.0)
    c.ui_add_note(48, 0.0, 1.0)
    hub.pump()
    starts = [x.start_time for x in song_b.tracks[1].arrangement_clips]
    assert 32.0 in starts
    assert all(not s.has_clip for s in song_b.tracks[1].clip_slots[1:])  # temporary slot cleaned up


def test_unsupported_changes_are_marked_stale():
    a, song_a, b, song_b, _ = joined_pair()
    Live.configure(insert_device=False)
    hub = Hub([a, b])
    song_a.tracks[0].ui_add_device("Chorus-Ensemble")
    hub.pump()
    stale = b.bridge.take("stale")
    assert stale and "Update" in stale[0]["reason"]
    plugin = song_a.tracks[1].ui_add_device("Serum 2", class_name="PluginDevice")
    hub.pump()
    assert any("plugins" in m["reason"] for m in b.bridge.take("stale"))
    assert a.mirror.props(a.id_of(plugin))["plugin"] == "Serum 2"


def test_recording_is_shared_when_it_finishes():
    a, song_a, b, song_b, _ = joined_pair()
    hub = Hub([a, b])
    clip = song_a.tracks[2].ui_record_audio_clip(40.0, "/tmp/take2.wav")
    clip.ui_set_recording(True)
    song_a.tracks[2]._notify("arrangement_clips")
    hub.pump()
    assert len(song_b.tracks[2].arrangement_clips) == 1
    clip.ui_set_recording(False)
    hub.pump()
    clips = song_b.tracks[2].arrangement_clips
    assert len(clips) == 2 and clips[1].file_path == "/tmp/take2.wav"


def test_preview_swaps_to_full_quality_in_place():
    a, song_a, b, song_b, _ = joined_pair()
    hub = Hub([a, b])
    song_a.tracks[2].ui_record_audio_clip(48.0, "/a/take3.wav")
    a.tick()
    ops = a.bridge.ops()
    create = [o for o in ops if o["k"] == "c" and o["t"] == "clip"][0]
    # The app on b's side swaps a's local path for b's preview file.
    create["v"]["path"] = "/b/previews/take3.wav"
    b.bridge.inbox.append({"type": "apply", "ops": [create]})
    b.tick()
    clip = [c for c in song_b.tracks[2].arrangement_clips if c.start_time == 48.0][0]
    assert clip.file_path == "/b/previews/take3.wav"
    b.bridge.inbox.append({"type": "swap_file", "id": create["id"], "path": "/b/store/take3.wav"})
    b.tick()
    clips = [c for c in song_b.tracks[2].arrangement_clips if c.start_time == 48.0]
    assert len(clips) == 1 and clips[0].file_path == "/b/store/take3.wav"
    assert b.id_of(clips[0]) == create["id"]
    b.tick()
    assert b.bridge.ops() == []   # swapping isn't an edit


def test_bind_removes_things_deleted_since_the_snapshot():
    a, song_a, _, _ = make_engine(demo_song())
    doc, path_map, notes_map = capture(a)
    b, song_b, _, _ = make_engine(clone_song(song_a))
    vox = path_map["T2"]
    doc["e"] = [e for e in doc["e"] if e["id"] != vox and e["p"] != vox]
    b.bridge.inbox.append({"type": "bind", "doc": doc, "map": path_map, "notes": notes_map})
    b.tick()
    stats = b.bridge.take("bound")[0]["stats"]
    assert stats["removed"] == 1
    assert [t.name for t in song_b.tracks] == ["Drums", "Bass"]
    verify(b)


def test_reconcile_restores_a_rejected_delete():
    a, song_a, b, song_b, _ = joined_pair()
    clip = song_b.tracks[1].arrangement_clips[0]
    cid = b.id_of(clip)
    state = [b.mirror.entity_json(i) for i in b.mirror.subtree(cid)]
    song_b.tracks[1].ui_delete_arrangement_clip(clip)
    b.tick()
    assert b.bridge.ops() == [{"k": "r", "id": cid}]
    b.bridge.inbox.append({"type": "reconcile", "items": [{"id": cid, "deep": True, "state": state}]})
    b.tick()
    clips = song_b.tracks[1].arrangement_clips
    assert len(clips) == 1 and len(clips[0]._notes) == 3
    assert b.id_of(clips[0]) == cid
    b.tick()
    assert b.bridge.ops() == []


def test_play_together_starts_at_the_given_time():
    e, song, bridge, clock = make_engine(demo_song())
    capture(e)
    bridge.inbox.append({"type": "play_at", "pos": 16.0, "at": (clock.t + 1.0) * 1000.0})
    e.tick()
    assert not song.is_playing
    clock.t += 1.0
    e.tick()
    assert song.is_playing and song.current_song_time == 16.0


def test_presence_reports_selection_and_playhead():
    e, song, bridge, clock = make_engine(demo_song())
    capture(e)
    song.view.selected_track = song.tracks[1]
    e.tick()
    p = bridge.take("presence")[-1]
    assert p["track"] == e.id_of(song.tracks[1]) and p["playing"] is False


def test_suggestion_idea_copies_the_track():
    a, song_a, b, song_b, _ = joined_pair()
    tid = a.id_of(song_a.tracks[1])
    a.bridge.inbox.append({"type": "idea", "track": tid, "name": "Bass (Sam's idea)", "color": 5})
    a.tick()
    created = a.bridge.take("idea_created")[0]
    assert created["track"] and created["orig"] == tid
    assert song_a.tracks[2].name == "Bass (Sam's idea)" and song_a.tracks[2].mute is True
    Hub([a, b]).pump()
    assert song_b.tracks[2].name == "Bass (Sam's idea)"
    assert len(song_b.tracks[2].arrangement_clips) == 1


def test_edits_while_the_app_is_away_are_kept():
    e, song, bridge, _ = make_engine(demo_song())
    capture(e)
    bridge.connected = False
    song.tracks[0].name = "Drums 2"
    e.tick()
    assert bridge.ops() == [] and e.unsent
    bridge.connected = True
    bridge._events.append(("connected",))
    e.tick()
    sent = bridge.ops()
    assert {"k": "s", "id": e.id_of(song.tracks[0]), "v": {"name": "Drums 2"}} in sent


def test_session_clip_in_new_scene():
    a, song_a, b, song_b, _ = joined_pair()
    hub = Hub([a, b])
    song_a.create_scene(-1)
    song_a.tracks[1].clip_slots[2].create_clip(4.0)
    song_a.tracks[1].clip_slots[2].clip.ui_add_note(55, 0.0, 1.0)
    hub.pump()
    assert len(song_b.scenes) == 3
    assert song_b.tracks[1].clip_slots[2].has_clip
    verify(b)


def test_main_group_and_return_tracks_are_read_without_asking_for_clips():
    # Real Live raises "Main, Group and Return Tracks have no arrangement clips" if you even ask
    # (hasattr included). The first real session hit this and never got going.
    song = Live.Song.Song()
    group = song.ui_add_track("group", name="Drums")
    kick = song.ui_add_track("audio", name="Kick")
    kick.ui_record_audio_clip(0.0, "/samples/kick.wav", 4.0)
    keys = song.ui_add_track("midi", name="Keys")
    keys.ui_draw_arrangement_clip(4.0, 4.0).ui_add_note(60, 0.0, 1.0)
    song.create_return_track()
    e, song, bridge, clock = make_engine(song)
    hello_caps = e.caps
    assert hello_caps["arrangement_clips"] is True     # probed on a plain track, not the group
    doc, path_map, notes_map = capture(e)
    kinds = sorted(ent["v"]["kind"] for ent in doc["e"] if ent["t"] == "track")
    assert kinds == ["audio", "group", "master", "midi", "return"]
    assert len([ent for ent in doc["e"] if ent["t"] == "clip"]) == 2
    assert path_map["T1/A0"] == e.id_of(kick.arrangement_clips[0])
    # Full scans and edits keep working, and nothing complains.
    group.name = "Drum Bus"
    for _ in range(3):
        clock.t += 5.0
        e.tick()
    assert any(op.get("v", {}).get("name") == "Drum Bus" for op in bridge.ops())
    assert [m["text"] for m in bridge.take("log")] == []
    verify(e)

    # Joining a song like this binds everything too.
    b, song_b, bridge_b, _ = make_engine(clone_song(song))
    bridge_b.inbox.append({"type": "bind", "doc": e.mirror.to_json(), "map": path_map, "notes": notes_map,
                           "session": "s1"})
    b.tick()
    bound = bridge_b.take("bound")
    assert bound and bound[0]["stats"]["matched"] == 7 and bound[0]["stats"]["stale"] == 0
    problems = [m["text"] for m in bridge_b.take("log")
                if any(w in m["text"] for w in ("error", "couldn't", "stale "))]
    assert problems == []


def test_a_failed_capture_is_reported_not_retried_forever():
    e, song, bridge, clock = make_engine(demo_song())

    def broken():
        raise RuntimeError("Live said no")
    e.capture = broken
    bridge.inbox.append({"type": "capture", "session": "s1"})
    e.tick()
    failed = bridge.take("capture_failed")
    assert failed and "Live said no" in failed[0]["error"]
    assert bridge.take("captured") == []
    assert e.bound is False
    logs = [m["text"] for m in bridge.take("log")]
    for _ in range(10):
        clock.t += 5.0
        e.tick()
    assert len(logs) == 1 and bridge.take("log") == []


def test_hello_says_which_script_version_is_running():
    from Tutti.engine import Engine
    from conftest import FakeBridge
    song = demo_song()
    bridge = FakeBridge()
    Engine(song, Live.Application.Application(song), Live, bridge).tick()
    assert bridge.take("hello")[0]["script"] == "3"


def test_hello_names_the_open_set_so_the_app_knows_live_opened_its_file():
    from Tutti.engine import Engine
    from conftest import FakeBridge
    song = demo_song()
    app = Live.Application.Application(song)
    bridge = FakeBridge()
    engine = Engine(song, app, Live, bridge)
    engine.tick()
    first = bridge.take("hello")[0]["doc"]
    engine.tick()
    assert bridge.take("hello") == []          # same Set: nothing new
    app._song = demo_song()                      # Live opens another Set
    engine.clock = lambda: 10.0 ** 11
    engine.tick()
    second = bridge.take("hello")[0]["doc"]
    assert first and second and first != second
