//! Presets: a scene's settings plus (optionally) a theme and look. Built-ins ship with the app;
//! user presets live in the support folder as JSON (easy to share).

use crate::paths;
use cymatica_engine::params::{ParamSet, ParamValue, Rng};
use cymatica_engine::scene::scene_info;
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Preset {
    pub name: String,
    pub scene: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub theme: Option<String>,
    #[serde(default)]
    pub params: ParamSet,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub look: Option<ParamSet>,
    #[serde(skip)]
    pub builtin: bool,
}

impl Preset {
    pub fn key(&self) -> String {
        format!("{}/{}", self.scene, self.name)
    }
}

#[allow(dead_code)]
enum V {
    N(f64),
    T(&'static str),
    B(bool),
}

fn preset(scene: &str, name: &str, theme: Option<&str>, values: &[(&str, V)], look: &[(&str, V)]) -> Preset {
    let info = scene_info(scene).expect("preset for unknown scene");
    let mut params = ParamSet::default();
    for (k, v) in values {
        params.set(info.params, k, to_value(v));
    }
    let look = if look.is_empty() {
        None
    } else {
        let mut l = ParamSet::default();
        for (k, v) in look {
            l.set(cymatica_engine::post::LOOK_PARAMS, k, to_value(v));
        }
        Some(l)
    };
    Preset { name: name.into(), scene: scene.into(), theme: theme.map(Into::into), params, look, builtin: true }
}

fn to_value(v: &V) -> ParamValue {
    match v {
        V::N(n) => ParamValue::Number(*n),
        V::T(t) => ParamValue::Text(t.to_string()),
        V::B(b) => ParamValue::Bool(*b),
    }
}

/// Built-in presets for every scene that exists in this build.
pub fn builtin_presets() -> Vec<Preset> {
    use V::*;
    let mut v = vec![
        // Bars
        preset("bars", "Sparks", Some("synthwave"), &[("style", T("sparks"))], &[]),
        preset("bars", "Classic cava", Some("cava-classic"), &[("style", T("classic")), ("reflection", N(0.0)), ("gap", N(0.2))], &[("bloom", N(0.3)), ("vignette", N(0.0)), ("grain", N(0.0))]),
        preset("bars", "Jelly", Some("candy"), &[("style", T("jelly")), ("wobble", N(0.85)), ("spring", N(0.35)), ("roundness", N(1.0))], &[]),
        preset("bars", "Halo", Some("aurora"), &[("style", T("sparks")), ("layout", T("radial")), ("color_by", T("frequency")), ("spin", N(0.15))], &[("trails", N(0.85)), ("trail_zoom", N(0.012))]),
        preset("bars", "Mirror", Some("cyberpunk"), &[("style", T("jelly")), ("layout", T("mirror")), ("color_by", T("time")), ("gap", N(0.45))], &[("chromatic", N(0.5))]),
        preset("bars", "Kaleidoscope", Some("vaporwave"), &[("style", T("sparks")), ("layout", T("radial")), ("radial_radius", N(0.12))], &[("kaleido", N(6.0)), ("trails", N(0.8))]),
        // Ink
        preset("ink", "Marbling", Some("neon-tokyo"), &[], &[]),
        preset("ink", "Fountain", Some("fire-ice"), &[("jets", T("fountain")), ("swirl", N(0.15))], &[]),
        preset("ink", "Monsoon", Some("ocean-deep"), &[("jets", T("rain")), ("curl", N(0.6)), ("fade", N(0.35))], &[]),
        preset("ink", "Lava lamp", Some("lava"), &[("viscosity", N(0.75)), ("curl", N(0.15)), ("shading", N(0.9)), ("jet_force", N(0.45))], &[("bloom", N(1.4))]),
        preset("ink", "Smoke", Some("mono"), &[("shading", N(0.2)), ("ink", N(0.35)), ("curl", N(0.8))], &[("saturation", N(0.6))]),
        // Ripples
        preset("ripples", "Pool party", Some("ocean-deep"), &[], &[]),
        preset("ripples", "Rainy night", Some("tokyo-night"), &[("mode", T("rain")), ("floor", T("neon")), ("tilt", N(0.35))], &[]),
        preset("ripples", "Faraday", Some("gold"), &[("mode", T("faraday")), ("floor", T("sand")), ("tilt", N(1.0)), ("spin", N(0.0))], &[]),
        preset("ripples", "Lagoon", Some("forest"), &[("floor", T("sand")), ("tint", N(0.7)), ("caustics", N(1.6))], &[]),
    ];
    v.retain(|p| scene_info(&p.scene).is_some());
    v
}

pub fn user_presets() -> Vec<Preset> {
    let mut out = Vec::new();
    if let Ok(entries) = std::fs::read_dir(paths::presets_dir()) {
        for e in entries.flatten() {
            let p = e.path();
            if p.extension().map(|x| x == "json").unwrap_or(false) {
                if let Some(mut preset) =
                    std::fs::read_to_string(&p).ok().and_then(|t| serde_json::from_str::<Preset>(&t).ok())
                {
                    if let Some(info) = scene_info(&preset.scene) {
                        preset.params.sanitize(info.params);
                        if let Some(l) = preset.look.as_mut() {
                            l.sanitize(cymatica_engine::post::LOOK_PARAMS);
                        }
                        out.push(preset);
                    }
                }
            }
        }
    }
    out.sort_by(|a, b| a.name.to_lowercase().cmp(&b.name.to_lowercase()));
    out
}

fn file_name(p: &Preset) -> String {
    let clean: String = p
        .name
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() || c == '-' || c == '_' || c == ' ' { c } else { '_' })
        .collect();
    format!("{}-{}.json", p.scene, clean.trim())
}

pub fn save_user_preset(p: &Preset) -> std::io::Result<std::path::PathBuf> {
    let dir = paths::presets_dir();
    std::fs::create_dir_all(&dir)?;
    let path = dir.join(file_name(p));
    std::fs::write(&path, serde_json::to_string_pretty(p).unwrap_or_default())?;
    Ok(path)
}

pub fn delete_user_preset(p: &Preset) {
    let _ = std::fs::remove_file(paths::presets_dir().join(file_name(p)));
}

/// Randomizes a scene's parameters (amount 1.0 = anywhere; smaller = mutate).
pub fn randomize(scene: &str, params: &mut ParamSet, seed: u64, amount: f32) {
    if let Some(info) = scene_info(scene) {
        let mut rng = Rng::new(seed);
        params.randomize(info.params, &mut rng, amount);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builtin_presets_are_valid() {
        let presets = builtin_presets();
        assert!(presets.len() >= 10);
        for p in &presets {
            let info = scene_info(&p.scene).unwrap();
            let mut clean = p.params.clone();
            clean.sanitize(info.params);
            assert_eq!(clean, p.params, "preset {} has invalid values", p.key());
        }
        let mut keys: Vec<String> = presets.iter().map(|p| p.key()).collect();
        keys.sort();
        keys.dedup();
        assert_eq!(keys.len(), presets.len());
    }

    #[test]
    fn presets_serialize_compactly() {
        let p = &builtin_presets()[0];
        let json = serde_json::to_string(p).unwrap();
        let back: Preset = serde_json::from_str(&json).unwrap();
        assert_eq!(back.params, p.params);
        assert_eq!(back.scene, p.scene);
    }
}
