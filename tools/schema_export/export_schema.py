#!/usr/bin/env python3
"""Regenerate libs/serum2/data/{serum2_schema,multisamples,init_template}.json.

Serum 2's preset format is undocumented. The parameter tables here are
exported from serum-mcp (MIT, https://github.com/Celian-mrc/serum-mcp), whose
authors verified them against a real Serum 2 install. See
THIRD_PARTY_NOTICES.md.

Usage:
    git clone https://github.com/Celian-mrc/serum-mcp /tmp/serum-mcp
    git -C /tmp/serum-mcp checkout <SERUM_MCP_COMMIT>
    pip install cbor2 zstandard
    python tools/schema_export/export_schema.py /tmp/serum-mcp

The output is committed, so building the repo never needs Python.
"""

from __future__ import annotations

import dataclasses
import json
import struct
import sys
from pathlib import Path

SERUM_MCP_COMMIT = "6c471bb8424f3f06f16f5b9fc5bfab244fd11384"

REPO = Path(__file__).resolve().parents[2]
OUT_DIR = REPO / "libs" / "serum2" / "data"

# LFO beat-sync rate raw values, read back from presets saved by Serum 2.0.24
# with a single RATE change each (1/4 is the absent-key default).
LFO_SYNC_RATES = {
    "4 bar": 0.2108496543592536,
    "1 bar": 1.6269264358721542,
    "1/2": 3.3735944697480575,
    "1/4": 6.25,
    "1/8": 10.66,
    "1/16": 17.078820419579646,
    "1/32": 26.030822973954468,
}

# Wavetables whose exact numFrames/sampleRate/numChannels were read from
# real Serum-saved presets (a mismatch can make Serum misread the table).
# Tags are ours and drive tag-based selection in the generator.
WAVETABLE_TAGS = {
    "default": ["basic", "clean", "sine", "saw", "square"],
    "analog_basic": ["basic", "analog", "clean", "saw", "square"],
    "analog_warm": ["analog", "warm"],
    "pwm": ["analog", "pwm", "warm"],
    "analog_mini": ["analog", "basic", "warm"],
    "warm_sub": ["analog", "sub", "warm", "sine"],
    "digital_fm": ["digital", "fm", "bright", "keys"],
    "harmonic_smooth": ["digital", "harmonic", "organ", "clean"],
    "dying_sine": ["digital", "sine", "evolving", "growl"],
    "acid": ["analog", "acid", "growl", "bright"],
    "fm_piano": ["fm", "keys", "epiano", "bell"],
    "flute": ["acoustic", "winds", "breathy"],
    "mini_bass": ["analog", "bass", "warm", "moog"],
    "rhodes_mk1": ["keys", "epiano", "acoustic", "warm"],
}

# Factory wavetables seen in Serum 2.0.22-saved presets, with the exact file
# metadata Serum wrote for them.
EXTRA_WAVETABLES = {
    "mini_bass": ("Analog/MiniBass.wav", 22528, 44100, 1),
    "rhodes_mk1": ("S2 Tables/Digital/RMrk1.wav", 524288, 44100, 1),
}

# Table positions whose sound is known from real presets.
KNOWN_FRAMES = {
    "default": {"saw": 0.0, "sine": 32.875},
}

# Mod destinations observed in Serum-saved presets but missing upstream.
EXTRA_MOD_DESTS = {
    f"oscillator{i}.detune": {
        "dest_type": "Oscillator",
        "dest_id": i,
        "param_name": "kParamDetune",
        "param_id": 26,
    }
    for i in range(3)
}

MULTISAMPLE_TAGS = {
    "choir_ah": ["choir", "vocal", "pad", "acoustic"],
    "synth_sid": ["synth", "retro", "lead"],
    "guitar_ac": ["guitar", "plucked", "acoustic"],
    "violins": ["strings", "acoustic", "orchestral"],
    "brass_french_horn": ["brass", "winds", "acoustic", "orchestral"],
    "epiano_suitcase": ["keys", "epiano", "acoustic"],
    "synth_pad_superjx": ["synth", "pad", "retro"],
    "mallet_balafon": ["mallet", "acoustic", "percussive"],
    "strings_full": ["strings", "acoustic", "orchestral", "pad"],
    "piano_grand": ["keys", "piano", "acoustic"],
}


def _param(d) -> dict:
    out = {"kind": d.kind}
    for field in ("default", "min", "max", "unit", "confidence"):
        value = getattr(d, field)
        if value not in (None, ""):
            out[field] = value
    if d.enum_values:
        out["enum"] = list(d.enum_values)
    return out


def _table(params: dict) -> dict:
    return {key: _param(defn) for key, defn in params.items()}


def export_schema(s) -> dict:
    modules = {
        "Oscillator": _table(s.OSCILLATOR_PARAMS),
        "WTOsc": _table(s.WTOSC_PARAMS),
        "NoiseOsc": _table(s.NOISEOSC_PARAMS),
        "SubOsc": _table(s.SUBOSC_PARAMS),
        "VoiceFilter": _table(s.VOICE_FILTER_PARAMS),
        "Env": _table(s.ENV_PARAMS),
        "LFO": _table(s.LFO_PARAMS),
        "Macro": _table(s.MACRO_PARAMS),
        "Global": _table(s.GLOBAL_PARAMS),
        "RoutingSlot": _table(s.ROUTING_SLOT_PARAMS),
        "ModSlot": _table(s.MODSLOT_PARAMS),
    }
    modules["MultiSampleOsc"] = _table(s.MULTISAMPLEOSC_PARAMS)
    # Oscillator0 (Osc A) is the only slot enabled by default.
    modules["Oscillator"]["kParamEnable"]["slotDefaults"] = {"0": True}

    # Absent-key behavior, from Serum-saved presets: LFOs are beat-synced at
    # 1/4 (raw 6.25) unless kParamBeatSync is written as 0.0.
    lfo = modules["LFO"]
    lfo["kParamBeatSync"]["default"] = True
    lfo["kParamRate"]["default"] = 6.25
    # Serum writes kParamMode together with kParamDefaultMode=0.0 whenever the
    # mode is changed; the absent state is not "Free", so never omit it.
    lfo["kParamMode"].pop("default", None)
    lfo["kParamDefaultMode"] = {"kind": "float", "confidence": "observed"}
    # An untouched Sub is not a saw (writing kSaw explicitly sounds different).
    modules["SubOsc"]["kParamShape"].pop("default", None)

    fx = {name: _table(params) for name, params in s.FX_PARAMS.items()}
    fx_type_ids = {name: type_id for type_id, name in s.FX_TYPE_IDS.items()}

    mod_dests = {
        name: dataclasses.asdict(d) for name, d in s.MOD_DEST_TARGETS.items()
    }
    fx_extra = {
        fx_type: {suffix: {"param_name": p, "param_id": i} for suffix, (p, i) in extra.items()}
        for fx_type, extra in s.FX_EXTRA_MOD_DEST_PARAMS.items()
    }

    wavetables = {}
    for wt_id, wt in s.SIMPLE_WAVETABLES.items():
        wavetables[wt_id] = {
            "path": wt.relative_path,
            "numFrames": wt.num_frames,
            "sampleRate": wt.sample_rate,
            "numChannels": wt.num_channels,
            "tags": WAVETABLE_TAGS.get(wt_id, []),
        }
    for wt_id, (path, frames, rate, channels) in EXTRA_WAVETABLES.items():
        wavetables[wt_id] = {
            "path": path,
            "numFrames": frames,
            "sampleRate": rate,
            "numChannels": channels,
            "tags": WAVETABLE_TAGS.get(wt_id, []),
        }
    for wt_id, frames in KNOWN_FRAMES.items():
        wavetables[wt_id]["knownFrames"] = frames
    mod_dests.update(EXTRA_MOD_DESTS)

    return {
        "_source": f"exported from serum-mcp@{SERUM_MCP_COMMIT} (MIT) by tools/schema_export",
        "modules": modules,
        "fx": fx,
        "fxTypeIds": fx_type_ids,
        "fxWetParamId": 1,
        "fxExtraModDests": fx_extra,
        "modSources": dict(s.MOD_SOURCE_IDS),
        "modDests": mod_dests,
        "wavetables": wavetables,
        "lfoSyncRates": LFO_SYNC_RATES,
        "lfoCurveShapes": dict(s.SIMPLE_LFO_TYPES),
    }


def export_multisamples(s) -> dict:
    return {
        inst_id: {
            "sfzPathRelative": inst.sfz_path_relative,
            "embedded_sfz": inst.embedded_sfz,
            "files": dict(inst.files),
            "tags": MULTISAMPLE_TAGS.get(inst_id, []),
        }
        for inst_id, inst in s.MULTISAMPLE_INSTRUMENTS.items()
    }


def load_preset(path: Path):
    import cbor2
    import zstandard

    raw = path.read_bytes()
    meta_len, _ = struct.unpack_from("<II", raw, 9)
    metadata = json.loads(raw[17 : 17 + meta_len])
    offset = 17 + meta_len
    payload_len, _ = struct.unpack_from("<II", raw, offset)
    data = cbor2.loads(
        zstandard.ZstdDecompressor().decompress(raw[offset + 8 :], max_output_size=payload_len)
    )
    return metadata, data


def export_init(serum_mcp: Path) -> dict:
    metadata, data = load_preset(serum_mcp / "fixtures" / "init_preset.SerumPreset")

    # Match what Serum 2.0.24 writes for a fresh init: all 64 mod slots exist,
    # and the payload's own name fields are blank.
    for i in range(64):
        data.setdefault(f"ModSlot{i}", {"plainParams": "default"})
    data["presetName"] = "Init"
    data["presetAuthor"] = ""
    data["presetDescription"] = ""
    data["tags"] = []
    # ClipPlayer0 carries a leftover record mode in the source fixture.
    data["ClipPlayer0"] = {"plainParams": "default"}

    metadata = {
        "fileType": "SerumPreset",
        "hash": "",
        "presetAuthor": "",
        "presetDescription": "",
        "presetName": "Init",
        "product": "Serum2",
        "productVersion": metadata.get("productVersion", "2.0.11"),
        "tags": [],
        "url": "https://xferrecords.com/",
        "vendor": "Xfer Records",
        "version": metadata.get("version", 4.0),
    }
    return {"metadata": metadata, "data": data}


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    serum_mcp = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(serum_mcp / "src"))
    from serum_mcp.preset import schema  # noqa: PLC0415

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    (OUT_DIR / "serum2_schema.json").write_text(
        json.dumps(export_schema(schema), indent=1, sort_keys=False) + "\n"
    )
    (OUT_DIR / "multisamples.json").write_text(
        json.dumps(export_multisamples(schema), indent=1) + "\n"
    )
    (OUT_DIR / "init_template.json").write_text(
        json.dumps(export_init(serum_mcp), indent=1, sort_keys=True) + "\n"
    )
    print(f"wrote {OUT_DIR}")


if __name__ == "__main__":
    main()
