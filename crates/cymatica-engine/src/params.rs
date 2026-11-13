//! Scene and look parameters: declared once (permanent IDs), stored sparsely in presets, and
//! optionally modulated by the music like a synth's mod matrix.

use cymatica_audio::AudioFrame;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

#[derive(Clone, Copy, Debug)]
pub enum ParamKind {
    Float { min: f32, max: f32, default: f32 },
    Int { min: i32, max: i32, default: i32 },
    Toggle { default: bool },
    /// Options are (id, label); values are stored by id so reordering is safe.
    Choice { options: &'static [(&'static str, &'static str)], default: &'static str },
    /// sRGB hex colour.
    Color { default: u32 },
}

#[derive(Clone, Copy, Debug)]
pub struct ParamDef {
    /// Permanent ID (stored in presets: never rename or reuse).
    pub id: &'static str,
    pub label: &'static str,
    /// UI section.
    pub group: &'static str,
    pub help: &'static str,
    pub kind: ParamKind,
    /// Excluded from Randomize (e.g. quality settings).
    pub no_random: bool,
}

pub const fn float(id: &'static str, label: &'static str, group: &'static str, min: f32, max: f32, default: f32, help: &'static str) -> ParamDef {
    ParamDef { id, label, group, help, kind: ParamKind::Float { min, max, default }, no_random: false }
}

pub const fn int(id: &'static str, label: &'static str, group: &'static str, min: i32, max: i32, default: i32, help: &'static str) -> ParamDef {
    ParamDef { id, label, group, help, kind: ParamKind::Int { min, max, default }, no_random: false }
}

pub const fn toggle(id: &'static str, label: &'static str, group: &'static str, default: bool, help: &'static str) -> ParamDef {
    ParamDef { id, label, group, help, kind: ParamKind::Toggle { default }, no_random: false }
}

pub const fn choice(
    id: &'static str,
    label: &'static str,
    group: &'static str,
    options: &'static [(&'static str, &'static str)],
    default: &'static str,
    help: &'static str,
) -> ParamDef {
    ParamDef { id, label, group, help, kind: ParamKind::Choice { options, default }, no_random: false }
}

pub const fn color(id: &'static str, label: &'static str, group: &'static str, default: u32, help: &'static str) -> ParamDef {
    ParamDef { id, label, group, help, kind: ParamKind::Color { default }, no_random: false }
}

impl ParamDef {
    pub const fn fixed(mut self) -> Self {
        self.no_random = true;
        self
    }

    pub fn default_value(&self) -> ParamValue {
        match self.kind {
            ParamKind::Float { default, .. } => ParamValue::Number(default as f64),
            ParamKind::Int { default, .. } => ParamValue::Number(default as f64),
            ParamKind::Toggle { default } => ParamValue::Bool(default),
            ParamKind::Choice { default, .. } => ParamValue::Text(default.to_string()),
            ParamKind::Color { default } => {
                ParamValue::Color([(default >> 16) as u8, (default >> 8) as u8, default as u8])
            }
        }
    }

    pub fn is_numeric(&self) -> bool {
        matches!(self.kind, ParamKind::Float { .. } | ParamKind::Int { .. })
    }

    /// (min, max) for numeric parameters.
    pub fn range(&self) -> Option<(f32, f32)> {
        match self.kind {
            ParamKind::Float { min, max, .. } => Some((min, max)),
            ParamKind::Int { min, max, .. } => Some((min as f32, max as f32)),
            _ => None,
        }
    }

    /// Clamps/validates a stored value; `None` if it doesn't fit this parameter.
    pub fn sanitize(&self, v: &ParamValue) -> Option<ParamValue> {
        match (self.kind, v) {
            (ParamKind::Float { min, max, .. }, ParamValue::Number(n)) if n.is_finite() => {
                Some(ParamValue::Number(n.clamp(min as f64, max as f64)))
            }
            (ParamKind::Int { min, max, .. }, ParamValue::Number(n)) if n.is_finite() => {
                Some(ParamValue::Number(n.round().clamp(min as f64, max as f64)))
            }
            (ParamKind::Toggle { .. }, ParamValue::Bool(b)) => Some(ParamValue::Bool(*b)),
            (ParamKind::Choice { options, .. }, ParamValue::Text(t)) => {
                options.iter().any(|(id, _)| id == t).then(|| ParamValue::Text(t.clone()))
            }
            (ParamKind::Color { .. }, ParamValue::Color(c)) => Some(ParamValue::Color(*c)),
            _ => None,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum ParamValue {
    Bool(bool),
    Number(f64),
    Text(String),
    Color([u8; 3]),
}

/// What a modulation follows.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize, Hash)]
pub enum ModSource {
    Bass,
    Mid,
    Treble,
    Loudness,
    Kick,
    Snare,
    Hat,
    Beat,
    Brightness,
    Noisiness,
    /// Slow sine, one cycle per 8 beats.
    SlowWave,
    /// One cycle per beat.
    BeatWave,
}

impl ModSource {
    pub const ALL: [ModSource; 12] = [
        ModSource::Bass,
        ModSource::Mid,
        ModSource::Treble,
        ModSource::Loudness,
        ModSource::Kick,
        ModSource::Snare,
        ModSource::Hat,
        ModSource::Beat,
        ModSource::Brightness,
        ModSource::Noisiness,
        ModSource::SlowWave,
        ModSource::BeatWave,
    ];

    pub fn label(self) -> &'static str {
        match self {
            ModSource::Bass => "Bass",
            ModSource::Mid => "Mids",
            ModSource::Treble => "Treble",
            ModSource::Loudness => "Loudness",
            ModSource::Kick => "Kick hits",
            ModSource::Snare => "Snare hits",
            ModSource::Hat => "Hi-hat hits",
            ModSource::Beat => "Beat pulse",
            ModSource::Brightness => "Brightness",
            ModSource::Noisiness => "Noisiness",
            ModSource::SlowWave => "Slow wave (8 beats)",
            ModSource::BeatWave => "Beat wave",
        }
    }

    /// 0..1
    pub fn value(self, a: &AudioFrame) -> f32 {
        match self {
            ModSource::Bass => a.bass,
            ModSource::Mid => a.mid,
            ModSource::Treble => a.treble,
            ModSource::Loudness => a.loudness,
            ModSource::Kick => a.onset[0],
            ModSource::Snare => a.onset[1],
            ModSource::Hat => a.onset[2],
            ModSource::Beat => a.beat,
            ModSource::Brightness => a.centroid,
            ModSource::Noisiness => a.flatness,
            ModSource::SlowWave => {
                let beats = (a.beat_count % 8) as f32 + a.beat_phase;
                0.5 - 0.5 * (beats / 8.0 * std::f32::consts::TAU).cos()
            }
            ModSource::BeatWave => 0.5 - 0.5 * (a.beat_phase * std::f32::consts::TAU).cos(),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
pub struct Modulation {
    pub source: ModSource,
    /// Fraction of the parameter's range added at full source level (-1..1).
    pub amount: f32,
}

/// Sparse parameter values plus modulations (missing = default).
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct ParamSet {
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub values: BTreeMap<String, ParamValue>,
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub mods: BTreeMap<String, Modulation>,
}

fn find<'d>(defs: &'d [ParamDef], id: &str) -> Option<&'d ParamDef> {
    defs.iter().find(|d| d.id == id)
}

impl ParamSet {
    pub fn get(&self, defs: &[ParamDef], id: &str) -> ParamValue {
        let def = find(defs, id).unwrap_or_else(|| panic!("unknown parameter {id}"));
        self.values.get(id).and_then(|v| def.sanitize(v)).unwrap_or_else(|| def.default_value())
    }

    pub fn set(&mut self, defs: &[ParamDef], id: &str, value: ParamValue) {
        if let Some(def) = find(defs, id) {
            match def.sanitize(&value) {
                Some(v) if v == def.default_value() => {
                    self.values.remove(id);
                }
                Some(v) => {
                    self.values.insert(id.to_string(), v);
                }
                None => {}
            }
        }
    }

    pub fn number(&self, defs: &[ParamDef], id: &str) -> f32 {
        match self.get(defs, id) {
            ParamValue::Number(n) => n as f32,
            ParamValue::Bool(b) => b as i32 as f32,
            _ => 0.0,
        }
    }

    /// Drops unknown parameters and invalid values (e.g. from an older/newer preset).
    pub fn sanitize(&mut self, defs: &[ParamDef]) {
        self.values.retain(|id, v| match find(defs, id).and_then(|d| d.sanitize(v)) {
            Some(clean) => {
                *v = clean;
                true
            }
            None => false,
        });
        self.mods.retain(|id, m| {
            m.amount = m.amount.clamp(-1.0, 1.0);
            find(defs, id).map(|d| d.is_numeric()).unwrap_or(false) && m.amount != 0.0
        });
    }

    /// Randomizes parameters (skipping fixed ones). `amount` 1.0 = anywhere in range, smaller
    /// values mutate around the current setting.
    pub fn randomize(&mut self, defs: &[ParamDef], rng: &mut Rng, amount: f32) {
        for d in defs.iter().filter(|d| !d.no_random) {
            let current = self.get(defs, d.id);
            let v = match d.kind {
                ParamKind::Float { min, max, .. } => {
                    let cur = if let ParamValue::Number(n) = current { n as f32 } else { min };
                    let target = min + rng.next() * (max - min);
                    ParamValue::Number((cur + (target - cur) * amount) as f64)
                }
                ParamKind::Int { min, max, .. } => {
                    let cur = if let ParamValue::Number(n) = current { n as f32 } else { min as f32 };
                    let target = min as f32 + rng.next() * (max - min) as f32;
                    ParamValue::Number((cur + (target - cur) * amount).round() as f64)
                }
                ParamKind::Toggle { .. } => {
                    if rng.next() < amount * 0.5 {
                        ParamValue::Bool(rng.next() < 0.5)
                    } else {
                        current
                    }
                }
                ParamKind::Choice { options, .. } => {
                    if rng.next() < amount {
                        let i = ((rng.next() * options.len() as f32) as usize).min(options.len() - 1);
                        ParamValue::Text(options[i].0.to_string())
                    } else {
                        current
                    }
                }
                ParamKind::Color { .. } => current,
            };
            self.set(defs, d.id, v);
        }
    }
}

/// Tiny deterministic RNG (xorshift) for randomize/mutate.
#[derive(Clone, Debug)]
pub struct Rng(u64);

impl Rng {
    pub fn new(seed: u64) -> Self {
        Self(seed.max(1))
    }
    /// 0..1
    pub fn next(&mut self) -> f32 {
        let mut x = self.0;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.0 = x;
        (x >> 40) as f32 / (1u64 << 24) as f32
    }
}

/// Read access to a parameter set with modulation applied for the current audio frame.
pub struct Params<'a> {
    pub defs: &'static [ParamDef],
    pub set: &'a ParamSet,
    pub audio: &'a AudioFrame,
}

impl<'a> Params<'a> {
    pub fn new(defs: &'static [ParamDef], set: &'a ParamSet, audio: &'a AudioFrame) -> Self {
        Self { defs, set, audio }
    }

    /// Numeric parameter, modulated and clamped to its range.
    pub fn f(&self, id: &str) -> f32 {
        let def = find(self.defs, id).unwrap_or_else(|| panic!("unknown parameter {id}"));
        let base = self.set.number(self.defs, id);
        let Some((min, max)) = def.range() else { return base };
        match self.set.mods.get(id) {
            Some(m) => (base + m.amount * (max - min) * m.source.value(self.audio)).clamp(min, max),
            None => base,
        }
    }

    pub fn i(&self, id: &str) -> i32 {
        self.f(id).round() as i32
    }

    pub fn b(&self, id: &str) -> bool {
        matches!(self.set.get(self.defs, id), ParamValue::Bool(true))
    }

    pub fn choice(&self, id: &str) -> String {
        match self.set.get(self.defs, id) {
            ParamValue::Text(t) => t,
            _ => String::new(),
        }
    }

    /// Index of the current choice within its options.
    pub fn choice_index(&self, id: &str) -> usize {
        let def = find(self.defs, id).unwrap_or_else(|| panic!("unknown parameter {id}"));
        let cur = self.choice(id);
        match def.kind {
            ParamKind::Choice { options, .. } => options.iter().position(|(o, _)| *o == cur).unwrap_or(0),
            _ => 0,
        }
    }

    /// Linear-light colour.
    pub fn color(&self, id: &str) -> [f32; 3] {
        match self.set.get(self.defs, id) {
            ParamValue::Color(c) => crate::theme::rgb8_to_linear(c),
            _ => [1.0, 1.0, 1.0],
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    static DEFS: &[ParamDef] = &[
        float("speed", "Speed", "Motion", 0.0, 2.0, 1.0, ""),
        int("count", "Count", "Motion", 1, 10, 4, ""),
        toggle("glow", "Glow", "Look", true, ""),
        choice("style", "Style", "Look", &[("a", "A"), ("b", "B")], "a", ""),
        color("tint", "Tint", "Look", 0xff0000, "").fixed(),
    ];

    #[test]
    fn defaults_are_not_stored() {
        let mut s = ParamSet::default();
        s.set(DEFS, "speed", ParamValue::Number(1.0));
        assert!(s.values.is_empty());
        s.set(DEFS, "speed", ParamValue::Number(5.0));
        assert_eq!(s.values.get("speed"), Some(&ParamValue::Number(2.0)));
    }

    #[test]
    fn modulation_follows_the_music() {
        let mut s = ParamSet::default();
        s.mods.insert("speed".into(), Modulation { source: ModSource::Bass, amount: 0.25 });
        let quiet = AudioFrame { bass: 0.0, ..Default::default() };
        let loud = AudioFrame { bass: 1.0, ..Default::default() };
        assert_eq!(Params::new(DEFS, &s, &quiet).f("speed"), 1.0);
        assert_eq!(Params::new(DEFS, &s, &loud).f("speed"), 1.5);
    }

    #[test]
    fn presets_round_trip_and_survive_unknown_keys() {
        let mut s = ParamSet::default();
        s.set(DEFS, "style", ParamValue::Text("b".into()));
        s.set(DEFS, "count", ParamValue::Number(7.0));
        s.set(DEFS, "glow", ParamValue::Bool(false));
        let json = serde_json::to_string(&s).unwrap();
        let mut back: ParamSet = serde_json::from_str(&json).unwrap();
        assert_eq!(back, s);
        back.values.insert("removed_param".into(), ParamValue::Number(1.0));
        back.values.insert("style".into(), ParamValue::Text("nope".into()));
        back.sanitize(DEFS);
        assert!(!back.values.contains_key("removed_param"));
        assert!(!back.values.contains_key("style"));
        assert_eq!(Params::new(DEFS, &back, &AudioFrame::default()).i("count"), 7);
    }

    #[test]
    fn randomize_stays_in_range_and_skips_fixed() {
        let mut s = ParamSet::default();
        let mut rng = Rng::new(42);
        for _ in 0..50 {
            s.randomize(DEFS, &mut rng, 1.0);
            let p = Params::new(DEFS, &s, &AudioFrame::default());
            assert!((0.0..=2.0).contains(&p.f("speed")));
            assert!((1..=10).contains(&p.i("count")));
            assert_eq!(p.color("tint"), [1.0, 0.0, 0.0]);
        }
    }
}
