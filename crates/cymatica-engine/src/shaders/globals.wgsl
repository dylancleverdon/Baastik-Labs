// ---- Cymatica globals: group 0, shared by every scene ----
struct Globals {
    resolution: vec2f,
    inv_resolution: vec2f,
    time: f32,
    dt: f32,
    frame: u32,
    aspect: f32,
    bass: f32,
    mid: f32,
    treble: f32,
    loudness: f32,
    kick: f32,
    snare: f32,
    hat: f32,
    beat: f32,
    beat_phase: f32,
    bpm: f32,
    centroid: f32,
    flatness: f32,
    level_l: f32,
    level_r: f32,
    correlation: f32,
    note_strength: f32,
    note: u32,
    silent: u32,
    bar_count: u32,
    wave_len: u32,
    hits: vec4<u32>,
    bands: array<vec4f, 2>,
    chroma: array<vec4f, 3>,
    bg_top: vec4f,
    bg_bottom: vec4f,
    accent: vec4f,
    note_color: vec4f,
    palette_shift: f32,
    energy: f32,
    seed: f32,
    spectrum_len: u32,
};

@group(0) @binding(0) var<uniform> G: Globals;
@group(0) @binding(1) var<storage, read> spectrum: array<f32>;
@group(0) @binding(2) var<storage, read> bars: array<f32>;
@group(0) @binding(3) var<storage, read> wave: array<vec2f>;
@group(0) @binding(4) var palette_tex: texture_2d<f32>;
@group(0) @binding(5) var lin_clamp: sampler;
@group(0) @binding(6) var lin_repeat: sampler;

fn band(i: u32) -> f32 { return G.bands[i / 4u][i % 4u]; }
fn chroma(i: u32) -> f32 { return G.chroma[i / 4u][i % 4u]; }

// Spectrum value at normalized log-frequency x in 0..1 (linearly interpolated).
fn spec(x: f32) -> f32 {
    let n = f32(G.spectrum_len);
    let p = clamp(x, 0.0, 1.0) * (n - 1.0);
    let i = u32(floor(p));
    let j = min(i + 1u, G.spectrum_len - 1u);
    return mix(spectrum[i], spectrum[j], fract(p));
}

// cava bar value at normalized position x in 0..1.
fn bar_at(x: f32) -> f32 {
    let n = max(G.bar_count, 1u);
    let i = min(u32(clamp(x, 0.0, 0.9999) * f32(n)), n - 1u);
    return bars[i];
}

// Theme palette (linear colour), t in 0..1, clamped.
fn palette(t: f32) -> vec3f {
    return textureSampleLevel(palette_tex, lin_clamp, vec2f(clamp(t, 0.0, 1.0) * 0.996 + 0.002, 0.5), 0.0).rgb;
}
// Theme palette that cycles smoothly (ping-pong) and follows the palette shift.
fn palette_cycle(t: f32) -> vec3f {
    let u = fract((t + G.palette_shift) * 0.5) * 2.0;
    return palette(1.0 - abs(u - 1.0));
}
fn background(uv_y: f32) -> vec3f { return mix(G.bg_top.rgb, G.bg_bottom.rgb, clamp(uv_y, 0.0, 1.0)); }
