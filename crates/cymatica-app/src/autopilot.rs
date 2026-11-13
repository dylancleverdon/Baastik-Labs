//! Auto-pilot ("DJ mode"): changes scene, preset or theme on the beat every N bars, and when a new
//! song seems to start (sound after a silence).

use crate::presets::Preset;
use crate::settings::{self, Settings};
use cymatica_audio::AudioFrame;
use cymatica_engine::scene::scenes;

pub struct Autopilot {
    beats: u32,
    last_beat_count: u32,
    clock: f32,
    silent_for: f32,
    cooldown: f32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Change {
    pub big: bool,
}

impl Autopilot {
    pub fn new() -> Self {
        Self { beats: 0, last_beat_count: 0, clock: 0.0, silent_for: 0.0, cooldown: 0.0 }
    }

    pub fn tick(&mut self, cfg: &settings::Autopilot, a: &AudioFrame, dt: f32) -> Option<Change> {
        self.cooldown = (self.cooldown - dt).max(0.0);
        if !cfg.enabled {
            self.beats = 0;
            self.last_beat_count = a.beat_count;
            return None;
        }
        // a new song: sound returning after a gap of silence
        if a.silent {
            self.silent_for += dt;
            return None;
        }
        let gap = self.silent_for;
        self.silent_for = 0.0;
        if cfg.on_song_change && gap > 1.5 && self.cooldown == 0.0 {
            self.reset(a);
            return Some(Change { big: true });
        }
        // count beats (fall back to a 120 BPM clock when the tempo is unclear)
        if a.tempo_confidence > 0.15 {
            if a.beat_count != self.last_beat_count {
                self.beats += a.beat_count.wrapping_sub(self.last_beat_count).min(4);
            }
        } else {
            self.clock += dt;
            while self.clock >= 0.5 {
                self.clock -= 0.5;
                self.beats += 1;
            }
        }
        self.last_beat_count = a.beat_count;
        if self.beats >= cfg.every_bars * 4 && self.cooldown == 0.0 {
            self.reset(a);
            return Some(Change { big: false });
        }
        None
    }

    fn reset(&mut self, a: &AudioFrame) {
        self.beats = 0;
        self.clock = 0.0;
        self.cooldown = 4.0;
        self.last_beat_count = a.beat_count;
    }
}

fn pick(seed: u64, n: usize) -> usize {
    ((seed >> 33) as usize) % n.max(1)
}

/// Applies an auto-pilot change according to what the user enabled.
pub fn apply(change: Change, s: &mut Settings, presets: &[Preset], seed: u64) {
    let cfg = s.autopilot.clone();
    let mut seed = seed | 1;
    let mut next = || {
        seed = seed.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        seed
    };
    let all = scenes();
    if cfg.change_scene && (change.big || next() % 3 == 0 || !cfg.change_preset) && all.len() > 1 {
        let cur = all.iter().position(|x| x.id == s.scene).unwrap_or(0);
        let mut i = pick(next(), all.len() - 1);
        if i >= cur {
            i += 1;
        }
        s.scene = all[i].id.to_string();
    }
    if cfg.change_preset {
        let options: Vec<&Preset> = presets.iter().filter(|p| p.scene == s.scene).collect();
        if !options.is_empty() {
            let p = options[pick(next(), options.len())];
            *s.params_for(&p.scene) = p.params.clone();
            s.look = p.look.clone().unwrap_or_default();
            if let (Some(t), false) = (&p.theme, cfg.change_theme) {
                s.theme = t.clone();
            }
        }
    }
    if cfg.change_theme {
        let themes = s.all_themes();
        s.theme = themes[pick(next(), themes.len())].id.clone();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn changes_every_n_bars_on_the_beat() {
        let cfg = settings::Autopilot { enabled: true, every_bars: 2, ..Default::default() };
        let mut ap = Autopilot::new();
        let mut a = AudioFrame { silent: false, tempo_confidence: 0.8, ..Default::default() };
        let mut changes = 0;
        for beat in 1..=32u32 {
            a.beat_count = beat;
            for _ in 0..30 {
                if ap.tick(&cfg, &a, 1.0 / 60.0).is_some() {
                    changes += 1;
                }
            }
        }
        // 32 beats / (2 bars * 4 beats) = 4 changes
        assert_eq!(changes, 4);
    }

    #[test]
    fn a_new_song_after_silence_triggers_a_big_change() {
        let cfg = settings::Autopilot { enabled: true, every_bars: 64, ..Default::default() };
        let mut ap = Autopilot::new();
        let silent = AudioFrame { silent: true, ..Default::default() };
        for _ in 0..120 {
            assert!(ap.tick(&cfg, &silent, 1.0 / 60.0).is_none());
        }
        let sound = AudioFrame { silent: false, ..Default::default() };
        assert_eq!(ap.tick(&cfg, &sound, 1.0 / 60.0), Some(Change { big: true }));
    }

    #[test]
    fn apply_respects_switches() {
        let mut s = Settings { scene: "bars".into(), theme: "nord".into(), ..Default::default() };
        s.autopilot = settings::Autopilot { change_scene: false, change_preset: false, change_theme: true, ..Default::default() };
        apply(Change { big: true }, &mut s, &crate::presets::builtin_presets(), 99);
        assert_eq!(s.scene, "bars");
    }
}
