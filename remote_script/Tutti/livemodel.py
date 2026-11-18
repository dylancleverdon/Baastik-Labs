"""Reading Live objects into document props, and writing props back to Live.

Prop names are shared with the C++ side (docs/DESIGN.md). Personal state is never read here:
solo, arm, monitoring, input routing, loop region, metronome, selection and view."""

PLUGIN_CLASSES = ("PluginDevice", "AuPluginDevice", "Vst3PluginDevice", "VstPluginDevice")
NOTE_FIELDS = (("p", "pitch"), ("t", "start_time"), ("d", "duration"), ("v", "velocity"), ("m", "mute"),
               ("pr", "probability"), ("vd", "velocity_deviation"), ("rv", "release_velocity"))


def num(value):
    """Floats rounded so Live's own float noise doesn't look like an edit."""
    if isinstance(value, bool):
        return value
    if isinstance(value, float):
        return round(value, 6)
    return value


def same(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return a == b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return abs(float(a) - float(b)) <= 1e-5 * max(1.0, abs(float(a)))
    return a == b


def safe(getter, default=None):
    try:
        return getter()
    except Exception:
        return default


def has(obj, name):
    """hasattr for Live objects. Live answers some questions with an error instead of a value
    (asking the Main, a group or a return track for its arrangement clips raises RuntimeError),
    and Python's hasattr only swallows AttributeError, so it would raise too."""
    try:
        getattr(obj, name)
        return True
    except Exception:
        return False


# Track kinds Live gives no arrangement clips (it raises if you ask).
NO_ARRANGEMENT = ("master", "return", "group")


# ---------------------------------------------------------------- song

def read_song(song):
    return {
        "tempo": num(song.tempo),
        "sig_n": song.signature_numerator,
        "sig_d": song.signature_denominator,
        "swing": num(safe(lambda: song.swing_amount, 0.0)),
    }


def write_song(song, props):
    for key, value in props.items():
        if value is None:
            continue
        if key == "tempo":
            song.tempo = float(value)
        elif key == "sig_n":
            song.signature_numerator = int(value)
        elif key == "sig_d":
            song.signature_denominator = int(value)
        elif key == "swing":
            song.swing_amount = float(value)


# ---------------------------------------------------------------- tracks

def track_kind(track, song=None):
    if song is not None:
        if track == song.master_track:
            return "master"
        if any(track == r for r in song.return_tracks):
            return "return"
    if safe(lambda: track.is_foldable, False):
        return "group"
    if safe(lambda: track.has_midi_input, False):
        return "midi"
    return "audio"


def read_track(track, kind):
    props = {"kind": kind, "name": track.name, "color": safe(lambda: track.color, 0)}
    mixer = track.mixer_device
    if kind != "master":
        props["mute"] = bool(safe(lambda: track.mute, False))
    props["vol"] = num(mixer.volume.value)
    props["pan"] = num(mixer.panning.value)
    for i, send in enumerate(safe(lambda: list(mixer.sends), [])):
        props["send%d" % i] = num(send.value)
    return props


def write_track(track, props):
    mixer = track.mixer_device
    for key, value in props.items():
        if value is None:
            continue
        if key == "name":
            track.name = value
        elif key == "color":
            track.color = int(value)
        elif key == "mute":
            track.mute = bool(value)
        elif key == "vol":
            mixer.volume.value = float(value)
        elif key == "pan":
            mixer.panning.value = float(value)
        elif key.startswith("send"):
            sends = list(mixer.sends)
            index = int(key[4:])
            if index < len(sends):
                sends[index].value = float(value)


# ---------------------------------------------------------------- devices

def is_plugin(device):
    return safe(lambda: device.class_name, "") in PLUGIN_CLASSES


def read_device(device):
    props = {"cls": device.class_name, "name": device.name}
    display = safe(lambda: device.class_display_name, None)
    if display:
        props["disp"] = display
    if is_plugin(device):
        props["plugin"] = device.name
    for i, p in enumerate(device.parameters):
        props["p%d" % i] = num(p.value)
    return props


def write_device(device, props):
    params = list(device.parameters)
    for key, value in props.items():
        if value is None:
            continue
        if key == "name":
            if device.name != value:
                device.name = value
        elif key.startswith("p") and key[1:].isdigit():
            index = int(key[1:])
            if index < len(params):
                p = params[index]
                v = max(p.min, min(p.max, float(value)))
                if not same(p.value, v):
                    p.value = v


# ---------------------------------------------------------------- clips

CLIP_SETTERS = (("name", "name"), ("color", "color"), ("muted", "muted"), ("loop", "looping"),
                ("warp", "warping"), ("wmode", "warp_mode"), ("gain", "gain"), ("pc", "pitch_coarse"),
                ("pf", "pitch_fine"))


def read_clip(clip, where):
    props = {
        "where": where,
        "midi": bool(clip.is_midi_clip),
        "name": clip.name,
        "color": safe(lambda: clip.color, 0),
        "muted": bool(safe(lambda: clip.muted, False)),
        "loop": bool(safe(lambda: clip.looping, False)),
        "ls": num(clip.loop_start),
        "le": num(clip.loop_end),
        "sm": num(clip.start_marker),
        "em": num(clip.end_marker),
    }
    if where == "a":
        props["start"] = num(clip.start_time)
        props["len"] = num(clip.end_time - clip.start_time)
    else:
        props["len"] = num(clip.length)
    if not clip.is_midi_clip:
        props["path"] = safe(lambda: clip.file_path, None)
        props["warp"] = bool(safe(lambda: clip.warping, True))
        props["wmode"] = safe(lambda: clip.warp_mode, 0)
        props["gain"] = num(safe(lambda: clip.gain, 0.0))
        props["pc"] = safe(lambda: clip.pitch_coarse, 0)
        props["pf"] = num(safe(lambda: clip.pitch_fine, 0.0))
    return props


def write_clip(clip, props):
    """Sets everything except position/length/file (those need the clip recreated)."""
    for key, attr in CLIP_SETTERS:
        if key in props and props[key] is not None:
            value = props[key]
            if not same(safe(lambda: getattr(clip, attr)), value):
                setattr(clip, attr, value)
    _write_markers(clip, props)


def _write_markers(clip, props):
    # Live refuses a start after the end, so order the writes so each one is valid.
    for start_key, end_key, start_attr, end_attr in (("ls", "le", "loop_start", "loop_end"),
                                                      ("sm", "em", "start_marker", "end_marker")):
        start = props.get(start_key)
        end = props.get(end_key)
        if start is None and end is None:
            continue
        current_end = getattr(clip, end_attr)
        if start is not None and end is not None and float(start) >= current_end:
            setattr(clip, end_attr, float(end))
            setattr(clip, start_attr, float(start))
            continue
        if start is not None and not same(getattr(clip, start_attr), start):
            setattr(clip, start_attr, float(start))
        if end is not None and not same(getattr(clip, end_attr), end):
            setattr(clip, end_attr, float(end))


# ---------------------------------------------------------------- notes

def read_notes(clip):
    getter = getattr(clip, "get_all_notes_extended", None)
    if getter is not None:
        notes = getter()
    else:
        notes = clip.get_notes_extended(0, 128, -1e6, 2e6)
    out = {}
    for n in notes:
        out[n.note_id] = note_props(n)
    return out


def note_props(n):
    props = {}
    for key, attr in NOTE_FIELDS:
        value = getattr(n, attr, None)
        if value is not None:
            props[key] = num(value) if key != "p" else int(value)
    if "m" in props:
        props["m"] = bool(props["m"])
    return props


def note_key(props):
    return (round(float(props.get("t", 0.0)), 4), int(props.get("p", 0)), round(float(props.get("d", 0.0)), 4))


def note_spec(Live, props):
    kwargs = dict(pitch=int(props.get("p", 60)), start_time=float(props.get("t", 0.0)),
                  duration=float(props.get("d", 0.25)), velocity=float(props.get("v", 100.0)),
                  mute=bool(props.get("m", False)))
    for key, attr in (("pr", "probability"), ("vd", "velocity_deviation"), ("rv", "release_velocity")):
        if key in props:
            kwargs[attr] = float(props[key])
    return Live.Clip.MidiNoteSpecification(**kwargs)


def write_note(note, props):
    for key, attr in NOTE_FIELDS:
        if key in props and props[key] is not None:
            value = props[key]
            setattr(note, attr, bool(value) if key == "m" else (int(value) if key == "p" else float(value)))


# ---------------------------------------------------------------- scenes and cues

def read_scene(scene):
    return {"name": scene.name, "color": safe(lambda: scene.color, 0), "tempo": num(safe(lambda: scene.tempo, -1.0))}


def write_scene(scene, props):
    for key in ("name", "color", "tempo"):
        if key in props and props[key] is not None and not same(getattr(scene, key), props[key]):
            setattr(scene, key, props[key])


def read_cue(cue):
    return {"time": num(cue.time), "name": cue.name}
