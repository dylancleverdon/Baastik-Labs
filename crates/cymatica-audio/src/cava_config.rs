//! Reads cava's own config file (`~/.config/cava/config`) so Cymatica can pick up the same input
//! source, audio processing settings and colours.

use crate::cava::{CavaSettings, ChannelLayout, Scaling};
use std::collections::HashMap;
use std::path::{Path, PathBuf};

#[derive(Clone, Debug, Default, PartialEq)]
pub struct CavaColors {
    pub background: Option<[u8; 3]>,
    pub foreground: Option<[u8; 3]>,
    /// Vertical gradient colours, bottom to top (empty when `gradient = 0`).
    pub gradient: Vec<[u8; 3]>,
    pub horizontal_gradient: Vec<[u8; 3]>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct CavaConfig {
    pub settings: CavaSettings,
    /// `[input] source` (e.g. "BlackHole 2ch").
    pub source: Option<String>,
    pub method: Option<String>,
    pub framerate: Option<u32>,
    pub colors: CavaColors,
    /// Keys we understood, for the import summary.
    pub imported_keys: Vec<String>,
}

/// Where cava looks for its config: `$XDG_CONFIG_HOME/cava/config` or `~/.config/cava/config`.
pub fn default_path() -> Option<PathBuf> {
    if let Ok(x) = std::env::var("XDG_CONFIG_HOME") {
        if !x.is_empty() {
            return Some(Path::new(&x).join("cava/config"));
        }
    }
    std::env::var("HOME").ok().map(|h| Path::new(&h).join(".config/cava/config"))
}

fn parse_ini(text: &str) -> HashMap<String, String> {
    let mut out = HashMap::new();
    let mut section = String::new();
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with(';') || line.starts_with('#') {
            continue;
        }
        if line.starts_with('[') && line.ends_with(']') {
            section = line[1..line.len() - 1].trim().to_lowercase();
            continue;
        }
        if let Some((k, v)) = line.split_once('=') {
            let v = v.trim();
            let value = match v.chars().next() {
                // quoted: everything up to the matching quote
                Some(q @ ('\'' | '"')) => match v[1..].find(q) {
                    Some(end) => v[1..1 + end].to_string(),
                    None => v[1..].to_string(),
                },
                // unquoted: strip trailing comments
                _ => v.split([';', '#']).next().unwrap_or("").trim().to_string(),
            };
            out.insert(format!("{}:{}", section, k.trim().to_lowercase()), value);
        }
    }
    out
}

/// Parses a cava colour: '#rrggbb' or one of cava's named colours.
pub fn parse_color(s: &str) -> Option<[u8; 3]> {
    let s = s.trim();
    if let Some(hex) = s.strip_prefix('#') {
        if hex.len() == 6 {
            let v = u32::from_str_radix(hex, 16).ok()?;
            return Some([(v >> 16) as u8, (v >> 8) as u8, v as u8]);
        }
        return None;
    }
    Some(match s.to_lowercase().as_str() {
        "black" => [0, 0, 0],
        "red" => [205, 49, 49],
        "green" => [13, 188, 121],
        "yellow" => [229, 229, 16],
        "blue" => [36, 114, 200],
        "magenta" => [188, 63, 188],
        "cyan" => [17, 168, 205],
        "white" => [229, 229, 229],
        _ => return None,
    })
}

pub fn parse(text: &str) -> CavaConfig {
    let ini = parse_ini(text);
    let mut cfg = CavaConfig::default();
    let mut s = CavaSettings::default();
    let mut used = Vec::new();
    let mut get = |k: &str| -> Option<String> {
        let v = ini.get(k).cloned();
        if v.is_some() {
            used.push(k.to_string());
        }
        v
    };
    let num = |v: Option<String>| v.and_then(|x| x.parse::<f64>().ok());

    if let Some(b) = num(get("general:bars")) {
        s.bars = if b <= 0.0 { 0 } else { b as usize };
    }
    if let Some(v) = num(get("general:autosens")) {
        s.autosens = v != 0.0;
    }
    if let Some(v) = num(get("general:sensitivity")) {
        s.sensitivity = v.max(1.0);
    }
    if let Some(v) = num(get("general:lower_cutoff_freq")) {
        s.lower_cutoff_freq = (v as i32).max(1);
    }
    if let Some(v) = num(get("general:higher_cutoff_freq")) {
        s.higher_cutoff_freq = v as i32;
    }
    if let Some(v) = get("general:scaling") {
        s.scaling = if v.eq_ignore_ascii_case("decibel") { Scaling::Decibel } else { Scaling::Linear };
    }
    if let Some(v) = num(get("general:framerate")) {
        cfg.framerate = Some(v.max(1.0) as u32);
    }
    if let Some(v) = num(get("smoothing:noise_reduction")) {
        s.noise_reduction = v.clamp(0.0, 100.0);
    }
    if let Some(v) = num(get("smoothing:monstercat")) {
        s.monstercat = v.max(0.0);
    }
    if let Some(v) = num(get("smoothing:waves")) {
        s.waves = v != 0.0;
    }
    let channels = get("output:channels");
    let mono = get("output:mono_option");
    s.layout = match channels.as_deref() {
        Some(c) if c.eq_ignore_ascii_case("mono") => match mono.as_deref().map(|m| m.to_lowercase()) {
            Some(m) if m == "left" => ChannelLayout::MonoLeft,
            Some(m) if m == "right" => ChannelLayout::MonoRight,
            _ => ChannelLayout::MonoAverage,
        },
        _ => ChannelLayout::Stereo,
    };
    if let Some(v) = num(get("output:reverse")) {
        s.reverse = v != 0.0;
    }
    cfg.method = get("input:method");
    cfg.source = get("input:source").filter(|v| !v.eq_ignore_ascii_case("auto"));

    cfg.colors.background = get("color:background").and_then(|c| parse_color(&c));
    cfg.colors.foreground = get("color:foreground").and_then(|c| parse_color(&c));
    if num(get("color:gradient")).unwrap_or(0.0) != 0.0 {
        for i in 1..=8 {
            if let Some(c) = get(&format!("color:gradient_color_{i}")).and_then(|c| parse_color(&c)) {
                cfg.colors.gradient.push(c);
            }
        }
    }
    if num(get("color:horizontal_gradient")).unwrap_or(0.0) != 0.0 {
        for i in 1..=8 {
            if let Some(c) =
                get(&format!("color:horizontal_gradient_color_{i}")).and_then(|c| parse_color(&c))
            {
                cfg.colors.horizontal_gradient.push(c);
            }
        }
    }
    cfg.settings = s;
    used.sort();
    cfg.imported_keys = used;
    cfg
}

pub fn load(path: &Path) -> std::io::Result<CavaConfig> {
    Ok(parse(&std::fs::read_to_string(path)?))
}

#[cfg(test)]
mod tests {
    use super::*;

    const SAMPLE: &str = r#"
## cava config
[general]
framerate = 75
autosens = 1
bars = 48
lower_cutoff_freq = 40
higher_cutoff_freq = 12000

[input]
method = coreaudio
source = "BlackHole 2ch"

[output]
channels = mono
mono_option = left

[color]
background = '#111111'
foreground = 'cyan'
gradient = 1
gradient_color_1 = '#59cc33'
gradient_color_2 = '#cc3333' ; red top

[smoothing]
noise_reduction = 66
monstercat = 1
"#;

    #[test]
    fn reads_a_blackhole_config() {
        let c = parse(SAMPLE);
        assert_eq!(c.source.as_deref(), Some("BlackHole 2ch"));
        assert_eq!(c.method.as_deref(), Some("coreaudio"));
        assert_eq!(c.framerate, Some(75));
        assert_eq!(c.settings.bars, 48);
        assert_eq!(c.settings.lower_cutoff_freq, 40);
        assert_eq!(c.settings.higher_cutoff_freq, 12000);
        assert_eq!(c.settings.noise_reduction, 66.0);
        assert_eq!(c.settings.monstercat, 1.0);
        assert_eq!(c.settings.layout, ChannelLayout::MonoLeft);
        assert_eq!(c.colors.background, Some([0x11, 0x11, 0x11]));
        assert_eq!(c.colors.foreground, Some([17, 168, 205]));
        assert_eq!(c.colors.gradient, vec![[0x59, 0xcc, 0x33], [0xcc, 0x33, 0x33]]);
    }

    #[test]
    fn commented_defaults_leave_settings_alone() {
        let c = parse("[general]\n; bars = 12\n[input]\n; source = auto\n");
        assert_eq!(c.settings, CavaSettings::default());
        assert!(c.source.is_none());
        assert!(c.imported_keys.is_empty());
    }
}
