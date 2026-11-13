//! Everything the user can change, saved to settings.json.

use crate::paths;
use cymatica_audio::{CavaSettings, InputChoice};
use cymatica_engine::params::ParamSet;
use cymatica_engine::theme::Theme;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

pub const SETTINGS_VERSION: u32 = 1;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize, Default)]
pub enum WindowMode {
    #[default]
    Window,
    Fullscreen,
    /// Behind the desktop icons (macOS), like a live wallpaper.
    Wallpaper,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Autopilot {
    pub enabled: bool,
    /// Change every N bars (4 beats each).
    pub every_bars: u32,
    pub change_scene: bool,
    pub change_theme: bool,
    pub change_preset: bool,
    /// Also change when a new song seems to start.
    pub on_song_change: bool,
}

impl Default for Autopilot {
    fn default() -> Self {
        Self {
            enabled: false,
            every_bars: 16,
            change_scene: true,
            change_theme: true,
            change_preset: true,
            on_song_change: true,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    pub version: u32,
    pub scene: String,
    pub theme: String,
    pub custom_themes: Vec<Theme>,
    /// Per-scene parameter values (sparse).
    pub scene_params: BTreeMap<String, ParamSet>,
    pub look: ParamSet,
    pub input: InputChoice,
    pub cava: CavaSettings,
    /// Delays the visuals (ms) to line up with speakers/headphones that lag (Bluetooth).
    pub av_offset_ms: f32,
    /// Render resolution relative to the window's pixels (1.0 = native Retina).
    pub render_scale: f32,
    /// Extended dynamic range on HDR/XDR displays (macOS).
    pub hdr: bool,
    pub window_mode: WindowMode,
    pub always_on_top: bool,
    pub show_hud: bool,
    pub panel_open: bool,
    pub welcomed: bool,
    pub autopilot: Autopilot,
    pub crossfade_seconds: f32,
    pub favorite_presets: Vec<String>,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            version: SETTINGS_VERSION,
            scene: "liquid".into(),
            theme: "synthwave".into(),
            custom_themes: Vec::new(),
            scene_params: BTreeMap::new(),
            look: ParamSet::default(),
            input: InputChoice::Auto,
            cava: CavaSettings::default(),
            av_offset_ms: 0.0,
            render_scale: 1.0,
            hdr: true,
            window_mode: WindowMode::Window,
            always_on_top: false,
            show_hud: false,
            panel_open: true,
            welcomed: false,
            autopilot: Autopilot::default(),
            crossfade_seconds: 1.5,
            favorite_presets: Vec::new(),
        }
    }
}

impl Settings {
    pub fn load() -> Self {
        let mut s: Settings = std::fs::read_to_string(paths::settings_file())
            .ok()
            .and_then(|t| serde_json::from_str(&t).ok())
            .unwrap_or_default();
        s.sanitize();
        s
    }

    pub fn save(&self) {
        let dir = paths::support_dir();
        let _ = std::fs::create_dir_all(&dir);
        if let Ok(json) = serde_json::to_string_pretty(self) {
            let tmp = dir.join("settings.json.tmp");
            if std::fs::write(&tmp, json).is_ok() {
                let _ = std::fs::rename(tmp, paths::settings_file());
            }
        }
    }

    /// Drops anything an older/newer version wrote that doesn't fit any more.
    pub fn sanitize(&mut self) {
        self.version = SETTINGS_VERSION;
        if cymatica_engine::scene::scene_info(&self.scene).is_none() {
            self.scene = cymatica_engine::scene::scenes()[0].id.to_string();
        }
        if self.theme_by_id(&self.theme).is_none() {
            self.theme = "synthwave".into();
        }
        for (id, set) in self.scene_params.iter_mut() {
            if let Some(info) = cymatica_engine::scene::scene_info(id) {
                set.sanitize(info.params);
            }
        }
        self.scene_params.retain(|id, _| cymatica_engine::scene::scene_info(id).is_some());
        self.look.sanitize(cymatica_engine::post::LOOK_PARAMS);
        self.render_scale = self.render_scale.clamp(0.25, 2.0);
        self.av_offset_ms = self.av_offset_ms.clamp(0.0, 1000.0);
        self.crossfade_seconds = self.crossfade_seconds.clamp(0.0, 6.0);
        self.autopilot.every_bars = self.autopilot.every_bars.clamp(1, 128);
    }

    pub fn all_themes(&self) -> Vec<Theme> {
        let mut t = cymatica_engine::theme::builtin_themes();
        t.extend(self.custom_themes.iter().cloned());
        t
    }

    pub fn theme_by_id(&self, id: &str) -> Option<Theme> {
        self.all_themes().into_iter().find(|t| t.id == id)
    }

    pub fn current_theme(&self) -> Theme {
        self.theme_by_id(&self.theme).unwrap_or_else(|| cymatica_engine::theme::builtin_themes()[0].clone())
    }

    pub fn params_for(&mut self, scene: &str) -> &mut ParamSet {
        self.scene_params.entry(scene.to_string()).or_default()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trips_and_ignores_junk() {
        let mut s = Settings { scene: "ink".into(), theme: "nord".into(), ..Default::default() };
        s.params_for("ink").values.insert("curl".into(), cymatica_engine::ParamValue::Number(0.9));
        let json = serde_json::to_string(&s).unwrap();
        let mut back: Settings = serde_json::from_str(&json).unwrap();
        back.sanitize();
        assert_eq!(back.scene, "ink");
        assert_eq!(back.theme, "nord");
        // unknown fields, scenes and themes from other versions are tolerated
        let mut odd: Settings =
            serde_json::from_str(r#"{"scene":"nope","theme":"gone","futureField":1,"render_scale":9}"#).unwrap();
        odd.sanitize();
        assert!(cymatica_engine::scene::scene_info(&odd.scene).is_some());
        assert_eq!(odd.theme, "synthwave");
        assert_eq!(odd.render_scale, 2.0);
    }
}
