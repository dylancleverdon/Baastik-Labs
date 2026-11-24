"""A small stand-in for Ableton Live's Python API, for testing the Tutti Remote Script.

It mimics the parts Tutti uses and enforces Live's rules: changing the Set from inside a
listener callback raises RuntimeError ("Changes cannot be triggered by notifications"), and
optional API features (per Live version) can be switched off with `configure()`.
"""

import types

_notifying = [0]

# Optional API features; tests switch them off to check fallbacks.
FEATURES = {
    "arrangement_create": True,   # Track.create_midi_clip / Track.create_audio_clip (12.1.10+)
    "session_audio": True,        # ClipSlot.create_audio_clip (12.0.5+)
    "insert_device": True,        # Track.insert_device (12.3+)
    "data": True,                 # get_data / set_data
    "duplicate_to_arrangement": True,
}

VERSION = (12, 3, 2)


def configure(**flags):
    for k, v in flags.items():
        if k not in FEATURES:
            raise KeyError(k)
        FEATURES[k] = v


def reset_features():
    for k in FEATURES:
        FEATURES[k] = True


def _guard():
    if _notifying[0] > 0:
        raise RuntimeError("Changes cannot be triggered by notifications. You will need to defer your response.")


class _Listenable(object):
    _props = ()

    def __init__(self):
        self.__dict__["_listeners"] = {}

    def _notify(self, prop):
        callbacks = list(self._listeners.get(prop, ()))
        _notifying[0] += 1
        try:
            for cb in callbacks:
                cb()
        finally:
            _notifying[0] -= 1

    def __getattr__(self, name):
        listeners = self.__dict__.get("_listeners")
        for prefix, kind in (("add_", "add"), ("remove_", "remove")):
            if name.startswith(prefix) and name.endswith("_listener"):
                prop = name[len(prefix):-len("_listener")]
                if prop in self._props:
                    if kind == "add":
                        return lambda cb: listeners.setdefault(prop, []).append(cb)
                    return lambda cb: listeners.get(prop, []).remove(cb)
        if name.endswith("_has_listener"):
            prop = name[: -len("_has_listener")]
            if prop in self._props:
                return lambda cb: cb in listeners.get(prop, [])
        raise AttributeError(name)

    def _set(self, prop, value):
        _guard()
        if self.__dict__.get("_" + prop) == value:
            return
        self.__dict__["_" + prop] = value
        self._notify(prop)

    def listener_count(self):
        return sum(len(v) for v in self._listeners.values())


def _prop(name, readonly=False):
    def getter(self):
        return self.__dict__.get("_" + name)

    if readonly:
        return property(getter)

    def setter(self, value):
        self._set(name, value)

    return property(getter, setter)


class _Data(object):
    def __init__(self):
        self.__dict__["_data"] = {}

    def __getattr__(self, name):
        if name in ("get_data", "set_data"):
            if not FEATURES["data"]:
                raise AttributeError(name)
            if name == "get_data":
                return lambda key, default=None: self._data.get(key, default)

            def set_data(key, value):
                _guard()
                self._data[key] = value
            return set_data
        raise AttributeError(name)


# ---------------------------------------------------------------- parameters and devices

class FakeDeviceParameter(_Listenable):
    _props = ("value", "name", "automation_state")

    def __init__(self, name, value=0.0, min=0.0, max=1.0, is_quantized=False):
        _Listenable.__init__(self)
        self.__dict__.update(_name=name, _value=value, min=min, max=max, is_quantized=is_quantized,
                             is_enabled=True, original_name=name, automation_state=0)

    def ui_write_automation(self):
        """What drawing or recording automation for this parameter does."""
        self.__dict__["automation_state"] = 1
        self._notify("automation_state")

    name = _prop("name", readonly=True)

    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, v):
        v = max(self.min, min(self.max, float(v)))
        if self.is_quantized:
            v = float(round(v))
        self._set("value", v)


class FakeDevice(_Listenable):
    _props = ("name", "parameters", "is_active", "selected_preset_index")

    def __init__(self, name, class_name=None, params=None, presets=None):
        _Listenable.__init__(self)
        params = params if params is not None else [("Dry/Wet", 0.5, 0.0, 1.0), ("Decay", 0.3, 0.0, 1.0)]
        parameters = [FakeDeviceParameter("Device On", 1.0, 0.0, 1.0, True)]
        parameters += [FakeDeviceParameter(*p) for p in params]
        self.__dict__.update(_name=name, class_name=class_name or name.replace(" ", ""),
                             class_display_name=name, _parameters=parameters, can_have_chains=False,
                             chains=[], type=2, _is_active=True)
        if presets is not None:   # plugins that list presets inside Live
            self.__dict__.update(presets=list(presets), _selected_preset_index=0)

    name = _prop("name")

    @property
    def selected_preset_index(self):
        if "presets" not in self.__dict__:
            raise AttributeError("selected_preset_index")
        return self._selected_preset_index

    @selected_preset_index.setter
    def selected_preset_index(self, v):
        _guard()
        if not 0 <= int(v) < len(self.presets):
            raise RuntimeError("Invalid preset index")
        self._set("selected_preset_index", int(v))
    parameters = _prop("parameters", readonly=True)
    is_active = _prop("is_active", readonly=True)


class MixerDevice(_Listenable):
    _props = ("sends",)

    def __init__(self, num_sends=0):
        _Listenable.__init__(self)
        self.__dict__.update(volume=FakeDeviceParameter("Track Volume", 0.85, 0.0, 1.0),
                             panning=FakeDeviceParameter("Track Panning", 0.0, -1.0, 1.0),
                             _sends=[FakeDeviceParameter("Send %d" % i, 0.0) for i in range(num_sends)])

    sends = _prop("sends", readonly=True)

    def _set_send_count(self, n):
        sends = list(self._sends)
        while len(sends) < n:
            sends.append(FakeDeviceParameter("Send %d" % len(sends), 0.0))
        del sends[n:]
        self.__dict__["_sends"] = sends
        self._notify("sends")


# ---------------------------------------------------------------- clips and notes

class MidiNote(object):
    def __init__(self, note_id, pitch, start_time, duration, velocity=100.0, mute=False,
                 probability=1.0, velocity_deviation=0.0, release_velocity=64.0):
        self.note_id = note_id
        self.pitch = pitch
        self.start_time = start_time
        self.duration = duration
        self.velocity = velocity
        self.mute = mute
        self.probability = probability
        self.velocity_deviation = velocity_deviation
        self.release_velocity = release_velocity

    def copy(self):
        return MidiNote(self.note_id, self.pitch, self.start_time, self.duration, self.velocity, self.mute,
                        self.probability, self.velocity_deviation, self.release_velocity)


class MidiNoteSpecification(object):
    def __init__(self, pitch, start_time, duration, velocity=100.0, mute=False, probability=1.0,
                 velocity_deviation=0.0, release_velocity=64.0):
        self.pitch = pitch
        self.start_time = start_time
        self.duration = duration
        self.velocity = velocity
        self.mute = mute
        self.probability = probability
        self.velocity_deviation = velocity_deviation
        self.release_velocity = release_velocity


_next_note_id = [1]


class FakeClip(_Listenable):
    _props = ("name", "color", "muted", "looping", "loop_start", "loop_end", "start_marker", "end_marker",
              "notes", "warping", "warp_mode", "gain", "pitch_coarse", "pitch_fine", "is_recording",
              "signature_numerator", "signature_denominator")

    def __init__(self, length=4.0, is_midi=True, start_time=0.0, file_path=None, arrangement=False, parent=None):
        _Listenable.__init__(self)
        self.__dict__.update(
            _name="", _color=0, _muted=False, _looping=is_midi, _loop_start=0.0, _loop_end=length,
            _start_marker=0.0, _end_marker=length, _warping=True, _warp_mode=0, _gain=0.4, _pitch_coarse=0,
            _pitch_fine=0.0, _is_recording=False, _signature_numerator=4, _signature_denominator=4,
            is_midi_clip=is_midi, is_audio_clip=not is_midi, file_path=file_path,
            _start_time=start_time, _length=length, is_arrangement_clip=arrangement,
            canonical_parent=parent, _notes={})

    name = _prop("name")
    color = _prop("color")
    muted = _prop("muted")
    looping = _prop("looping")
    loop_start = _prop("loop_start")
    loop_end = _prop("loop_end")
    start_marker = _prop("start_marker")
    end_marker = _prop("end_marker")
    warping = _prop("warping")
    warp_mode = _prop("warp_mode")
    gain = _prop("gain")
    pitch_coarse = _prop("pitch_coarse")
    pitch_fine = _prop("pitch_fine")
    is_recording = _prop("is_recording", readonly=True)
    signature_numerator = _prop("signature_numerator")
    signature_denominator = _prop("signature_denominator")

    @property
    def start_time(self):
        return self._start_time

    @property
    def end_time(self):
        return self._start_time + self._length

    @property
    def length(self):
        return self._length

    # --- notes (Live 11+ extended API)
    def get_notes_extended(self, from_pitch, pitch_span, from_time, time_span):
        out = []
        for n in self._notes.values():
            if from_pitch <= n.pitch < from_pitch + pitch_span and from_time <= n.start_time < from_time + time_span:
                out.append(n.copy())
        return tuple(sorted(out, key=lambda n: (n.start_time, n.pitch)))

    def get_all_notes_extended(self):
        return self.get_notes_extended(0, 128, -1e9, 2e9)

    def add_new_notes(self, specs):
        _guard()
        ids = []
        for s in specs:
            nid = _next_note_id[0]
            _next_note_id[0] += 1
            self._notes[nid] = MidiNote(nid, s.pitch, s.start_time, s.duration, s.velocity, s.mute,
                                        s.probability, s.velocity_deviation, s.release_velocity)
            ids.append(nid)
        self._notify("notes")
        return ids

    def apply_note_modifications(self, notes):
        _guard()
        for n in notes:
            if n.note_id in self._notes:
                self._notes[n.note_id] = n.copy()
        self._notify("notes")

    def remove_notes_by_id(self, ids):
        _guard()
        for nid in ids:
            self._notes.pop(nid, None)
        self._notify("notes")

    # --- test helpers: what a person does in Live's UI
    def ui_add_note(self, pitch, start, duration=0.25, velocity=100.0):
        nid = _next_note_id[0]
        _next_note_id[0] += 1
        self._notes[nid] = MidiNote(nid, pitch, start, duration, velocity)
        self._notify("notes")
        return nid

    def ui_move_note(self, note_id, start):
        self._notes[note_id].start_time = start
        self._notify("notes")

    def ui_delete_note(self, note_id):
        del self._notes[note_id]
        self._notify("notes")

    def ui_set_recording(self, recording):
        self.__dict__["_is_recording"] = recording
        self._notify("is_recording")


class ClipSlot(_Listenable):
    _props = ("has_clip",)

    def __init__(self, track):
        _Listenable.__init__(self)
        self.__dict__.update(_track=track, _clip=None)

    @property
    def has_clip(self):
        return self._clip is not None

    @property
    def clip(self):
        return self._clip

    @property
    def canonical_parent(self):
        return self._track

    def create_clip(self, length):
        _guard()
        if self._clip is not None:
            raise RuntimeError("Slot already has a clip")
        if not self._track.has_midi_input:
            raise RuntimeError("Can't create MIDI clips on audio tracks")
        self.__dict__["_clip"] = FakeClip(length, True, parent=self)
        self._notify("has_clip")

    def delete_clip(self):
        _guard()
        self.__dict__["_clip"] = None
        self._notify("has_clip")

    def __getattr__(self, name):
        if name == "create_audio_clip":
            if not FEATURES["session_audio"]:
                raise AttributeError(name)

            def create_audio_clip(path):
                _guard()
                self.__dict__["_clip"] = FakeClip(8.0, False, file_path=path, parent=self)
                self._notify("has_clip")
            return create_audio_clip
        return _Listenable.__getattr__(self, name)


# ---------------------------------------------------------------- tracks

# Real Live answers some questions with an error instead of a value. Python's hasattr doesn't
# swallow those, so code must not even ask.
NO_ARRANGEMENT = ("master", "return", "group")
NO_ARRANGEMENT_ERROR = "Main, Group and Return Tracks have no arrangement clips"


def _refused(message):
    def refuse(*args, **kwargs):
        raise RuntimeError(message)
    return refuse


def _track_prop(name, refused_kinds, message):
    def getter(self):
        if self.kind in refused_kinds:
            raise RuntimeError(message)
        return self.__dict__.get("_" + name)

    def setter(self, value):
        if self.kind in refused_kinds:
            raise RuntimeError(message)
        self._set(name, value)

    return property(getter, setter)


class FakeTrack(_Listenable, _Data):
    _props = ("name", "color", "mute", "solo", "arm", "devices", "clip_slots", "arrangement_clips",
              "output_meter_level")

    def __init__(self, song, kind="midi", name="", num_scenes=0, num_sends=0):
        _Listenable.__init__(self)
        _Data.__init__(self)
        self.__dict__.update(_song=song, kind=kind, _name=name, _color=0, _mute=False, _solo=False, _arm=False,
                             has_midi_input=kind == "midi", has_audio_input=kind == "audio",
                             is_foldable=kind == "group", is_grouped=False, _devices=[],
                             _clip_slots=[], _arrangement_clips=[], mixer_device=MixerDevice(num_sends),
                             can_be_armed=kind in ("midi", "audio"))
        for _ in range(num_scenes if kind in ("midi", "audio") else 0):
            self._clip_slots.append(ClipSlot(self))

    name = _prop("name")
    color = _prop("color")
    mute = _track_prop("mute", ("master",), "Main track has no mute")
    solo = _track_prop("solo", ("master",), "Main track has no solo")
    arm = _prop("arm")
    devices = _prop("devices", readonly=True)
    clip_slots = _prop("clip_slots", readonly=True)

    @property
    def arrangement_clips(self):
        if self.kind in NO_ARRANGEMENT:
            raise RuntimeError(NO_ARRANGEMENT_ERROR)
        return self.__dict__.get("_arrangement_clips")

    def _add_arrangement_clip(self, clip):
        clips = sorted(self._arrangement_clips + [clip], key=lambda c: c.start_time)
        self.__dict__["_arrangement_clips"] = clips
        self._notify("arrangement_clips")
        return clip

    def delete_device(self, index):
        _guard()
        devices = list(self._devices)
        del devices[index]
        self.__dict__["_devices"] = devices
        self._notify("devices")

    def __getattr__(self, name):
        if name in ("get_data", "set_data"):
            return _Data.__getattr__(self, name)
        if self.__dict__.get("kind") in NO_ARRANGEMENT and name in (
                "add_arrangement_clips_listener", "remove_arrangement_clips_listener",
                "arrangement_clips_has_listener", "create_midi_clip", "create_audio_clip",
                "duplicate_clip_to_arrangement", "delete_clip"):
            if name in ("create_midi_clip", "create_audio_clip") and not FEATURES["arrangement_create"]:
                raise AttributeError(name)
            return _refused(NO_ARRANGEMENT_ERROR)
        if name in ("create_midi_clip", "create_audio_clip") and not FEATURES["arrangement_create"]:
            raise AttributeError(name)
        if name == "create_midi_clip":
            def create_midi_clip(start_time, length):
                _guard()
                return self._add_arrangement_clip(FakeClip(length, True, start_time, arrangement=True, parent=self))
            return create_midi_clip
        if name == "create_audio_clip":
            def create_audio_clip(file_path, position):
                _guard()
                return self._add_arrangement_clip(FakeClip(8.0, False, position, file_path, True, parent=self))
            return create_audio_clip
        if name in ("duplicate_clip_to_arrangement", "delete_clip"):
            if not FEATURES["duplicate_to_arrangement"]:
                raise AttributeError(name)
            if name == "delete_clip":
                def delete_clip(clip):
                    _guard()
                    clips = [c for c in self._arrangement_clips if c is not clip]
                    self.__dict__["_arrangement_clips"] = clips
                    self._notify("arrangement_clips")
                return delete_clip

            def duplicate_clip_to_arrangement(clip, destination_time):
                _guard()
                copy = FakeClip(clip.length, clip.is_midi_clip, destination_time, clip.file_path, True, parent=self)
                for k in FakeClip._props:
                    if k not in ("notes", "is_recording") and ("_" + k) in clip.__dict__:
                        copy.__dict__["_" + k] = clip.__dict__["_" + k]
                for n in clip._notes.values():
                    nid = _next_note_id[0]
                    _next_note_id[0] += 1
                    m = n.copy()
                    m.note_id = nid
                    copy._notes[nid] = m
                return self._add_arrangement_clip(copy)
            return duplicate_clip_to_arrangement
        if name == "insert_device":
            if not FEATURES["insert_device"]:
                raise AttributeError(name)

            def insert_device(device_name, target_index=-1):
                _guard()
                devices = list(self._devices)
                device = FakeDevice(device_name)
                if target_index < 0 or target_index > len(devices):
                    devices.append(device)
                else:
                    devices.insert(target_index, device)
                self.__dict__["_devices"] = devices
                self._notify("devices")
                return device
            return insert_device
        return _Listenable.__getattr__(self, name)

    # --- test helpers: what a person does in Live's UI
    def ui_add_device(self, name, index=None, params=None, class_name=None, presets=None):
        device = FakeDevice(name, class_name, params, presets)
        devices = list(self._devices)
        devices.insert(len(devices) if index is None else index, device)
        self.__dict__["_devices"] = devices
        self._notify("devices")
        return device

    def ui_draw_arrangement_clip(self, start, length=4.0):
        return self._add_arrangement_clip(FakeClip(length, self.has_midi_input, start, arrangement=True, parent=self))

    def ui_record_audio_clip(self, start, path, length=8.0):
        return self._add_arrangement_clip(FakeClip(length, False, start, path, True, parent=self))

    def ui_move_arrangement_clip(self, clip, start):
        clip.__dict__["_start_time"] = start
        self.__dict__["_arrangement_clips"] = sorted(self._arrangement_clips, key=lambda c: c.start_time)
        self._notify("arrangement_clips")

    def ui_delete_arrangement_clip(self, clip):
        self.__dict__["_arrangement_clips"] = [c for c in self._arrangement_clips if c is not clip]
        self._notify("arrangement_clips")


class Scene(_Listenable):
    _props = ("name", "color", "tempo")

    def __init__(self, name=""):
        _Listenable.__init__(self)
        self.__dict__.update(_name=name, _color=0, _tempo=-1.0)

    name = _prop("name")
    color = _prop("color")
    tempo = _prop("tempo")


class CuePoint(_Listenable):
    _props = ("name", "time")

    def __init__(self, time, name=""):
        _Listenable.__init__(self)
        self.__dict__.update(_time=time, _name=name)

    name = _prop("name")
    time = _prop("time", readonly=True)

    def jump(self):
        pass


class SongView(_Listenable):
    _props = ("selected_track", "detail_clip", "selected_scene")

    def __init__(self):
        _Listenable.__init__(self)
        self.__dict__.update(_selected_track=None, _detail_clip=None, _selected_scene=None)

    selected_track = _prop("selected_track")
    detail_clip = _prop("detail_clip")
    selected_scene = _prop("selected_scene")


class FakeSong(_Listenable, _Data):
    _props = ("tracks", "return_tracks", "scenes", "cue_points", "tempo", "signature_numerator",
              "signature_denominator", "swing_amount", "current_song_time", "is_playing", "record_mode",
              "metronome", "loop")

    def __init__(self):
        _Listenable.__init__(self)
        _Data.__init__(self)
        self.__dict__.update(_tracks=[], _return_tracks=[], _scenes=[Scene()], _cue_points=[], _tempo=120.0,
                             _signature_numerator=4, _signature_denominator=4, _swing_amount=0.0,
                             _current_song_time=0.0, _is_playing=False, _record_mode=False, _metronome=False,
                             _loop=False, view=SongView())
        self.__dict__["master_track"] = FakeTrack(self, "master", "Main")

    tracks = _prop("tracks", readonly=True)
    return_tracks = _prop("return_tracks", readonly=True)
    scenes = _prop("scenes", readonly=True)
    cue_points = _prop("cue_points", readonly=True)
    tempo = _prop("tempo")
    signature_numerator = _prop("signature_numerator")
    signature_denominator = _prop("signature_denominator")
    swing_amount = _prop("swing_amount")
    current_song_time = _prop("current_song_time")
    is_playing = _prop("is_playing", readonly=True)
    record_mode = _prop("record_mode")
    metronome = _prop("metronome")
    loop = _prop("loop")

    def _insert_track(self, kind, index):
        _guard()
        track = FakeTrack(self, kind, "%d-%s" % (len(self._tracks) + 1, "MIDI" if kind == "midi" else "Audio"),
                      len(self._scenes), len(self._return_tracks))
        tracks = list(self._tracks)
        if index < 0 or index > len(tracks):
            index = len(tracks)
        tracks.insert(index, track)
        self.__dict__["_tracks"] = tracks
        self._notify("tracks")
        return track

    def create_midi_track(self, index=-1):
        return self._insert_track("midi", index)

    def create_audio_track(self, index=-1):
        return self._insert_track("audio", index)

    def create_return_track(self):
        _guard()
        track = FakeTrack(self, "return", "%s-Return" % chr(ord("A") + len(self._return_tracks)))
        self.__dict__["_return_tracks"] = self._return_tracks + [track]
        for t in self._tracks + self._return_tracks + [self.master_track]:
            t.mixer_device._set_send_count(len(self._return_tracks))
        self._notify("return_tracks")
        return track

    def delete_track(self, index):
        _guard()
        tracks = list(self._tracks)
        del tracks[index]
        self.__dict__["_tracks"] = tracks
        self._notify("tracks")

    def delete_return_track(self, index):
        _guard()
        returns = list(self._return_tracks)
        del returns[index]
        self.__dict__["_return_tracks"] = returns
        for t in self._tracks + self._return_tracks + [self.master_track]:
            t.mixer_device._set_send_count(len(returns))
        self._notify("return_tracks")

    def duplicate_track(self, index):
        _guard()
        src = self._tracks[index]
        track = FakeTrack(self, src.kind, src.name + " 2", len(self._scenes), len(self._return_tracks))
        track.__dict__["_color"] = src.color
        for d in src.devices:
            copy = FakeDevice(d.name, d.class_name, [])
            copy.__dict__["_parameters"] = [FakeDeviceParameter(p.name, p.value, p.min, p.max, p.is_quantized)
                                            for p in d.parameters]
            track._devices.append(copy)
        for c in src._arrangement_clips:
            dup = FakeClip(c.length, c.is_midi_clip, c.start_time, c.file_path, True, parent=track)
            for k in ("name", "color", "muted", "looping", "loop_start", "loop_end", "start_marker", "end_marker"):
                dup.__dict__["_" + k] = c.__dict__["_" + k]
            for n in c._notes.values():
                nid = _next_note_id[0]
                _next_note_id[0] += 1
                m = n.copy()
                m.note_id = nid
                dup._notes[nid] = m
            track._arrangement_clips.append(dup)
        tracks = list(self._tracks)
        tracks.insert(index + 1, track)
        self.__dict__["_tracks"] = tracks
        self._notify("tracks")
        return track

    def create_scene(self, index=-1):
        _guard()
        scenes = list(self._scenes)
        if index < 0 or index > len(scenes):
            index = len(scenes)
        scene = Scene()
        scenes.insert(index, scene)
        self.__dict__["_scenes"] = scenes
        for t in self._tracks:
            if t.kind in ("midi", "audio"):
                slots = list(t._clip_slots)
                slots.insert(index, ClipSlot(t))
                t.__dict__["_clip_slots"] = slots
                t._notify("clip_slots")
        self._notify("scenes")
        return scene

    def delete_scene(self, index):
        _guard()
        scenes = list(self._scenes)
        del scenes[index]
        self.__dict__["_scenes"] = scenes
        for t in self._tracks:
            if t.kind in ("midi", "audio"):
                slots = list(t._clip_slots)
                del slots[index]
                t.__dict__["_clip_slots"] = slots
                t._notify("clip_slots")
        self._notify("scenes")

    def start_playing(self):
        _guard()
        self._set("is_playing", True)

    def continue_playing(self):
        _guard()
        self._set("is_playing", True)

    def stop_playing(self):
        _guard()
        self._set("is_playing", False)

    def set_or_delete_cue(self):
        _guard()
        t = self._current_song_time
        existing = [c for c in self._cue_points if abs(c.time - t) < 1e-6]
        if existing:
            cues = [c for c in self._cue_points if c is not existing[0]]
        else:
            cues = sorted(self._cue_points + [CuePoint(t)], key=lambda c: c.time)
        self.__dict__["_cue_points"] = cues
        self._notify("cue_points")

    # --- test helpers
    def ui_add_track(self, kind="midi", index=-1, name=None):
        _notifying[0] += 0
        track = FakeTrack(self, kind, name or "", len(self._scenes), len(self._return_tracks))
        tracks = list(self._tracks)
        tracks.insert(len(tracks) if index < 0 else index, track)
        self.__dict__["_tracks"] = tracks
        self._notify("tracks")
        return track

    def ui_delete_track(self, track):
        self.__dict__["_tracks"] = [t for t in self._tracks if t is not track]
        self._notify("tracks")


class _AppView(object):
    focused_document_view = "Arranger"


class _BrowserItem(object):
    def __init__(self, name, children=(), is_folder=False):
        self.name = name
        self.children = list(children)
        self.is_folder = is_folder
        self.is_loadable = not is_folder


class _Browser(object):
    def __init__(self, plugins):
        self.plugins = _BrowserItem("Plug-Ins", [_BrowserItem("VST3", [_BrowserItem(n) for n in plugins], True)], True)


class FakeApplication(object):
    def __init__(self, song, plugins=()):
        self._song = song
        self.view = _AppView()
        self.browser = _Browser(plugins)

    def get_major_version(self):
        return VERSION[0]

    def get_minor_version(self):
        return VERSION[1]

    def get_bugfix_version(self):
        return VERSION[2]

    def get_document(self):
        return self._song


_app = [None]


def new_set():
    """Starts a fresh fake Live Set and returns its Song."""
    song = FakeSong()
    _app[0] = FakeApplication(song)
    return song


def _get_application():
    if _app[0] is None:
        new_set()
    return _app[0]


# Module-style namespaces, the way the real API is organised.
Application = types.SimpleNamespace(get_application=_get_application, Application=FakeApplication)
Clip = types.SimpleNamespace(Clip=FakeClip, MidiNoteSpecification=MidiNoteSpecification, MidiNote=MidiNote)
Song = types.SimpleNamespace(Song=FakeSong)
Track = types.SimpleNamespace(Track=FakeTrack)
Device = types.SimpleNamespace(Device=FakeDevice)
DeviceParameter = types.SimpleNamespace(DeviceParameter=FakeDeviceParameter)
