//! Colour themes: a gradient palette (interpolated in OKLab), a background and an accent.

// The OKLab matrices are quoted exactly as published.
#![allow(clippy::excessive_precision)]

use serde::{Deserialize, Serialize};

pub type Rgb8 = [u8; 3];

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Theme {
    pub id: String,
    pub name: String,
    /// Palette stops, low (bass / bottom / cold) to high.
    pub stops: Vec<Rgb8>,
    /// Background gradient: top, bottom.
    pub background: [Rgb8; 2],
    pub accent: Rgb8,
}

const fn hex(v: u32) -> Rgb8 {
    [(v >> 16) as u8, (v >> 8) as u8, v as u8]
}

fn theme(id: &str, name: &str, stops: &[u32], bg: [u32; 2], accent: u32) -> Theme {
    Theme {
        id: id.into(),
        name: name.into(),
        stops: stops.iter().map(|&v| hex(v)).collect(),
        background: [hex(bg[0]), hex(bg[1])],
        accent: hex(accent),
    }
}

/// The built-in themes, in menu order.
pub fn builtin_themes() -> Vec<Theme> {
    vec![
        theme(
            "synthwave",
            "Synthwave",
            &[0x2d00f7, 0x8900f2, 0xe500a4, 0xff5400, 0xffbd00],
            [0x0b0221, 0x1a0533],
            0xff2a6d,
        ),
        theme(
            "vaporwave",
            "Vaporwave",
            &[0xb967ff, 0xff71ce, 0x01cdfe, 0x05ffa1, 0xfffb96],
            [0x1a1033, 0x2b1b4d],
            0x01cdfe,
        ),
        theme(
            "cyberpunk",
            "Cyberpunk",
            &[0x00f0ff, 0x0077ff, 0x7a04eb, 0xff00a0, 0xfcee0a],
            [0x05010a, 0x0b0213],
            0xfcee0a,
        ),
        theme(
            "neon-tokyo",
            "Neon Tokyo",
            &[0xff0055, 0xff00cc, 0x8c1eff, 0x00b3ff, 0x00ffc8],
            [0x07020f, 0x120720],
            0xff0055,
        ),
        theme(
            "ocean-deep",
            "Ocean Deep",
            &[0x03045e, 0x0077b6, 0x00b4d8, 0x48cae4, 0xcaf0f8],
            [0x000814, 0x001d3d],
            0x00b4d8,
        ),
        theme("aurora", "Aurora", &[0x00ffa3, 0x03e1ff, 0x6a5cff, 0xdc1fff, 0xff4fd8], [0x020611, 0x071a2b], 0x00ffa3),
        theme("lava", "Lava", &[0x2a0000, 0x8b0000, 0xff2400, 0xff8c00, 0xffe066], [0x0a0000, 0x1a0500], 0xff4500),
        theme("sunset", "Sunset", &[0x355c7d, 0x6c5b7b, 0xc06c84, 0xf67280, 0xf8b195], [0x141026, 0x2a1a35], 0xf67280),
        theme("ice", "Ice", &[0x0a1128, 0x1282a2, 0x6ad1e3, 0xc4f1f9, 0xffffff], [0x01060f, 0x0a1128], 0x6ad1e3),
        theme("forest", "Forest", &[0x081c15, 0x1b4332, 0x2d6a4f, 0x52b788, 0xb7e4c7], [0x030b08, 0x0b1a13], 0x52b788),
        theme("candy", "Candy", &[0xff9ff3, 0xfeca57, 0xff6b6b, 0x48dbfb, 0x1dd1a1], [0x1e1029, 0x2d1840], 0xff9ff3),
        theme("gold", "Gold", &[0x3d2c00, 0x8a6d1c, 0xd4af37, 0xf5d97a, 0xfff8e1], [0x0a0700, 0x171002], 0xd4af37),
        theme(
            "fire-ice",
            "Fire & Ice",
            &[0x00e5ff, 0x2962ff, 0x6200ea, 0xd50000, 0xff6d00, 0xffd600],
            [0x03030a, 0x0a0712],
            0xff6d00,
        ),
        theme("matrix", "Matrix", &[0x001a00, 0x003b00, 0x008f11, 0x00ff41, 0xd0ffd0], [0x000000, 0x000a00], 0x00ff41),
        theme("mono", "Mono", &[0x202020, 0x555555, 0x9a9a9a, 0xd0d0d0, 0xffffff], [0x000000, 0x0c0c0c], 0xffffff),
        theme(
            "spectrum",
            "Spectrum",
            &[0xff0000, 0xff8000, 0xffff00, 0x00ff00, 0x00ffff, 0x0000ff, 0x8000ff, 0xff00ff],
            [0x05050a, 0x0d0d18],
            0xffffff,
        ),
        theme(
            "catppuccin",
            "Catppuccin",
            &[0xcba6f7, 0xf38ba8, 0xfab387, 0xf9e2af, 0xa6e3a1, 0x89dceb, 0x89b4fa],
            [0x11111b, 0x1e1e2e],
            0xcba6f7,
        ),
        theme(
            "dracula",
            "Dracula",
            &[0xbd93f9, 0xff79c6, 0xff5555, 0xffb86c, 0xf1fa8c, 0x50fa7b, 0x8be9fd],
            [0x191a21, 0x282a36],
            0xff79c6,
        ),
        theme(
            "nord",
            "Nord",
            &[0x5e81ac, 0x81a1c1, 0x88c0d0, 0x8fbcbb, 0xa3be8c, 0xebcb8b, 0xd08770, 0xbf616a],
            [0x242933, 0x2e3440],
            0x88c0d0,
        ),
        theme(
            "gruvbox",
            "Gruvbox",
            &[0xcc241d, 0xd65d0e, 0xd79921, 0x98971a, 0x689d6a, 0x458588, 0xb16286],
            [0x1d2021, 0x282828],
            0xfabd2f,
        ),
        theme(
            "tokyo-night",
            "Tokyo Night",
            &[0x7aa2f7, 0xbb9af7, 0xf7768e, 0xff9e64, 0xe0af68, 0x9ece6a, 0x7dcfff],
            [0x16161e, 0x1a1b26],
            0x7aa2f7,
        ),
        theme(
            "rose-pine",
            "Rosé Pine",
            &[0x31748f, 0x9ccfd8, 0xc4a7e7, 0xebbcba, 0xf6c177, 0xeb6f92],
            [0x191724, 0x1f1d2e],
            0xebbcba,
        ),
        theme(
            "cava-classic",
            "cava Classic",
            &[0x59cc33, 0x80cc33, 0xa6cc33, 0xcccc33, 0xcca633, 0xcc8033, 0xcc5933, 0xcc3333],
            [0x000000, 0x000000],
            0x33ffff,
        ),
    ]
}

/// A theme from cava's colours (`[color]` in ~/.config/cava/config).
pub fn theme_from_cava(colors: &cymatica_audio::cava_config::CavaColors) -> Option<Theme> {
    let mut stops = colors.gradient.clone();
    if stops.is_empty() {
        stops = colors.horizontal_gradient.clone();
    }
    if stops.is_empty() {
        let fg = colors.foreground?;
        // a single foreground colour: build a gentle ramp around it
        let dark = [fg[0] / 4, fg[1] / 4, fg[2] / 4];
        stops = vec![dark, fg, [fg[0].saturating_add(60), fg[1].saturating_add(60), fg[2].saturating_add(60)]];
    }
    if stops.len() == 1 {
        stops.push(stops[0]);
    }
    let bg = colors.background.unwrap_or([0, 0, 0]);
    Some(Theme {
        id: "my-cava".into(),
        name: "My cava".into(),
        accent: colors.foreground.unwrap_or(*stops.last().unwrap()),
        stops,
        background: [bg, bg],
    })
}

// ---- colour maths ----

pub fn srgb_to_linear(c: f32) -> f32 {
    if c <= 0.04045 {
        c / 12.92
    } else {
        ((c + 0.055) / 1.055).powf(2.4)
    }
}

pub fn linear_to_srgb(c: f32) -> f32 {
    if c <= 0.0031308 {
        c * 12.92
    } else {
        1.055 * c.max(0.0).powf(1.0 / 2.4) - 0.055
    }
}

pub fn rgb8_to_linear(c: Rgb8) -> [f32; 3] {
    [srgb_to_linear(c[0] as f32 / 255.0), srgb_to_linear(c[1] as f32 / 255.0), srgb_to_linear(c[2] as f32 / 255.0)]
}

pub fn linear_to_oklab(c: [f32; 3]) -> [f32; 3] {
    let l = 0.4122214708 * c[0] + 0.5363325363 * c[1] + 0.0514459929 * c[2];
    let m = 0.2119034982 * c[0] + 0.6806995451 * c[1] + 0.1073969566 * c[2];
    let s = 0.0883024619 * c[0] + 0.2817188376 * c[1] + 0.6299787005 * c[2];
    let (l, m, s) = (l.cbrt(), m.cbrt(), s.cbrt());
    [
        0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
        1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
        0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s,
    ]
}

pub fn oklab_to_linear(c: [f32; 3]) -> [f32; 3] {
    let l = c[0] + 0.3963377774 * c[1] + 0.2158037573 * c[2];
    let m = c[0] - 0.1055613458 * c[1] - 0.0638541728 * c[2];
    let s = c[0] - 0.0894841775 * c[1] - 1.2914855480 * c[2];
    let (l, m, s) = (l * l * l, m * m * m, s * s * s);
    [
        (4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s).max(0.0),
        (-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s).max(0.0),
        (-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s).max(0.0),
    ]
}

pub const PALETTE_SIZE: usize = 256;

impl Theme {
    /// Linear-light palette with `PALETTE_SIZE` entries, interpolated in OKLab.
    pub fn palette(&self) -> Vec<[f32; 3]> {
        let labs: Vec<[f32; 3]> = self.stops.iter().map(|&c| linear_to_oklab(rgb8_to_linear(c))).collect();
        let n = labs.len().max(1);
        (0..PALETTE_SIZE)
            .map(|i| {
                if n == 1 {
                    return oklab_to_linear(labs[0]);
                }
                let t = i as f32 / (PALETTE_SIZE - 1) as f32 * (n - 1) as f32;
                let k = (t.floor() as usize).min(n - 2);
                let f = t - k as f32;
                let a = labs[k];
                let b = labs[k + 1];
                let c = oklab_to_linear([a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f]);
                // vivid stops can interpolate slightly outside sRGB: clip back into gamut
                [c[0].min(1.0), c[1].min(1.0), c[2].min(1.0)]
            })
            .collect()
    }

    pub fn background_linear(&self) -> [[f32; 3]; 2] {
        [rgb8_to_linear(self.background[0]), rgb8_to_linear(self.background[1])]
    }

    pub fn accent_linear(&self) -> [f32; 3] {
        rgb8_to_linear(self.accent)
    }
}

/// How the dominant note becomes a colour ("Synesthesia").
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum NoteColors {
    /// Scriabin's "clavier à lumières" colour-to-key mapping.
    Scriabin,
    /// Circle of fifths around the colour wheel (related keys get related hues).
    CircleOfFifths,
    /// Chromatic: semitones around the colour wheel.
    Chromatic,
}

fn hsv(h: f32, s: f32, v: f32) -> [f32; 3] {
    let h = h.rem_euclid(1.0) * 6.0;
    let c = v * s;
    let x = c * (1.0 - ((h % 2.0) - 1.0).abs());
    let (r, g, b) = match h as u32 {
        0 => (c, x, 0.0),
        1 => (x, c, 0.0),
        2 => (0.0, c, x),
        3 => (0.0, x, c),
        4 => (x, 0.0, c),
        _ => (c, 0.0, x),
    };
    let m = v - c;
    [srgb_to_linear(r + m), srgb_to_linear(g + m), srgb_to_linear(b + m)]
}

/// Linear colour for pitch class `note` (0 = C).
pub fn note_color(mode: NoteColors, note: u8) -> [f32; 3] {
    let pc = (note % 12) as usize;
    match mode {
        NoteColors::Scriabin => {
            const SCRIABIN: [u32; 12] = [
                0xff0000, // C  red
                0x9000ff, // C# violet
                0xffff00, // D  yellow
                0xb7468b, // D# steel / flesh
                0xc3f2ff, // E  pearly sky blue
                0xab0034, // F  deep red
                0x7f8bfd, // F# bright blue
                0xff8000, // G  orange
                0xbb75fc, // G# purple
                0x33cc33, // A  green
                0xa9677c, // A# rose steel
                0x8ec9ff, // B  pearly blue
            ];
            rgb8_to_linear(hex(SCRIABIN[pc]))
        }
        NoteColors::CircleOfFifths => hsv((pc * 7 % 12) as f32 / 12.0, 0.85, 1.0),
        NoteColors::Chromatic => hsv(pc as f32 / 12.0, 0.85, 1.0),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builtin_themes_are_well_formed_and_unique() {
        let themes = builtin_themes();
        assert!(themes.len() >= 20);
        let mut ids: Vec<&str> = themes.iter().map(|t| t.id.as_str()).collect();
        ids.sort();
        ids.dedup();
        assert_eq!(ids.len(), themes.len());
        for t in &themes {
            assert!(t.stops.len() >= 2, "{}", t.id);
            let p = t.palette();
            assert_eq!(p.len(), PALETTE_SIZE);
            assert!(p.iter().flatten().all(|v| v.is_finite() && *v >= 0.0 && *v <= 1.01), "{}", t.id);
        }
    }

    #[test]
    fn palette_endpoints_match_stops() {
        let t = theme("t", "T", &[0xff0000, 0x0000ff], [0, 0], 0);
        let p = t.palette();
        let first = p[0];
        let last = p[PALETTE_SIZE - 1];
        assert!((first[0] - 1.0).abs() < 1e-3 && first[2] < 1e-3);
        assert!((last[2] - 1.0).abs() < 1e-3 && last[0] < 1e-3);
    }

    #[test]
    fn oklab_round_trips() {
        for c in [[0.2f32, 0.5, 0.9], [1.0, 1.0, 1.0], [0.0, 0.0, 0.0], [0.9, 0.1, 0.3]] {
            let back = oklab_to_linear(linear_to_oklab(c));
            for i in 0..3 {
                assert!((back[i] - c[i]).abs() < 1e-4, "{c:?} -> {back:?}");
            }
        }
    }

    #[test]
    fn cava_colours_make_a_theme() {
        use cymatica_audio::cava_config::CavaColors;
        let c = CavaColors {
            background: Some([17, 17, 17]),
            foreground: Some([51, 255, 255]),
            gradient: vec![[89, 204, 51], [204, 51, 51]],
            horizontal_gradient: vec![],
        };
        let t = theme_from_cava(&c).unwrap();
        assert_eq!(t.stops.len(), 2);
        assert_eq!(t.background, [[17, 17, 17], [17, 17, 17]]);
        let fg_only = CavaColors { foreground: Some([51, 255, 255]), ..Default::default() };
        assert_eq!(theme_from_cava(&fg_only).unwrap().stops.len(), 3);
        assert!(theme_from_cava(&CavaColors::default()).is_none());
    }

    #[test]
    fn scriabin_c_is_red_and_a_is_green() {
        let c = note_color(NoteColors::Scriabin, 0);
        assert!(c[0] > 0.9 && c[1] < 0.01);
        let a = note_color(NoteColors::Scriabin, 9);
        assert!(a[1] > a[0] && a[1] > a[2]);
    }
}
