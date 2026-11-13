// ---- Acid: psychedelic feedback (melt, tunnel, fractal) ----

struct AcidU {
    mode: u32,
    symmetry: f32,
    zoom: f32,
    twist: f32,
    melt: f32,
    decay: f32,
    hue: f32,
    hue_step: f32,
    intensity: f32,
    time: f32,
    depth: f32,
    shock: f32,
    shock_amp: f32,
    sparkle_seed: f32,
    sparkle: f32,
    p0: f32,
};

@group(1) @binding(0) var<uniform> A: AcidU;
@group(1) @binding(1) var prev: texture_2d<f32>;
@group(1) @binding(2) var samp: sampler;

fn hue_shift(c: vec3f, a: f32) -> vec3f {
    // rotate around the grey axis
    let k = vec3f(0.57735);
    let ca = cos(a);
    return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}

// centred coordinates (aspect-correct, y up) <-> texture uv
fn to_p(uv: vec2f) -> vec2f { return (uv - 0.5) * vec2f(G.aspect, -1.0); }
fn to_uv(p: vec2f) -> vec2f { return p / vec2f(G.aspect, -1.0) + 0.5; }

fn sample_prev(p: vec2f) -> vec3f {
    let uv = to_uv(p);
    // nothing comes in from outside the picture (it would smear the edge)
    let inside = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);
    return textureSampleLevel(prev, samp, uv, 0.0).rgb * inside;
}

fn mirror(p: vec2f, n: f32) -> vec2f {
    if (n < 1.5) { return p; }
    let seg = TAU / n;
    var a = atan2(p.y, p.x);
    a = abs(fract(a / seg + 0.5) - 0.5) * seg;
    return vec2f(cos(a), sin(a)) * length(p);
}

// Where this pixel's colour comes from in the last frame: closer to the middle (so everything
// pours outward), turned, and wobbled.
fn warp(p: vec2f) -> vec2f {
    let r = length(p);
    var q = p * (1.0 - A.zoom);
    q = rot2(A.twist * (1.0 + 0.6 / (r + 0.25))) * q;
    let t = A.time;
    let m = A.melt * (0.004 + 0.012 * G.mid);
    q = q + m * vec2f(sin(q.y * 7.0 + t * 1.3) + 0.5 * sin(q.y * 17.0 - t * 2.1), cos(q.x * 6.0 - t * 1.1) + 0.5 * cos(q.x * 15.0 + t * 1.7));
    return q;
}

// New light added each frame: the steady part (ring, dots), scaled by how much the feedback
// keeps, so it settles at the same brightness whatever the trail length...
fn emit(p: vec2f) -> vec3f {
    let r = length(p);
    let a = atan2(p.y, p.x);
    var c = vec3f(0.0);
    // the waveform as a ring
    let n = f32(G.wave_len);
    let i = u32(clamp((a / TAU + 0.5) * (n - 1.0), 0.0, n - 1.0));
    let w = wave[i];
    let rw = 0.28 + (w.x + w.y) * 0.18 + G.bass * 0.1;
    let ring = exp(-abs(r - rw) * 140.0);
    c = c + palette(fract(a / TAU + A.hue)) * ring * (0.4 + 1.5 * G.loudness);
    // the spectrum as dots around it
    let s = spec(fract(a / TAU + 0.5));
    let dot_r = rw + 0.06 + s * 0.12;
    let dots = exp(-length(vec2f(r - dot_r, 0.0)) * 90.0) * step(0.6, fract(a * 24.0 / TAU)) * s;
    c = c + palette(fract(s + A.hue + 0.3)) * dots * 1.5;
    return c * A.intensity * (1.0 - A.decay) * 2.5;
}

// ...and the flashes: kick shockwaves (a ring racing outward: it's drawn every frame of its short
// life, so it's scaled like the steady light) and hi-hat sparkles (one frame each).
fn flashes(p: vec2f) -> vec3f {
    let r = length(p);
    var c = vec3f(0.0);
    let sw = exp(-abs(r - A.shock * 2.5) * 50.0) * A.shock_amp;
    c = c + mix(G.accent.rgb, vec3f(1.0), 0.3) * sw * (1.0 - A.decay) * 6.0;
    let cell = floor(p * 18.0);
    let h = hash21(cell + vec2f(A.sparkle_seed * 0.013, 3.0));
    let sp = step(0.985, h) * exp(-length(fract(p * 18.0) - 0.5) * 9.0) * A.sparkle;
    c = c + palette(h + A.hue) * sp * 1.2;
    return c * A.intensity;
}

// An endless tunnel: rings and checks in log-polar coordinates, flying inward with `depth`.
fn tunnel(p: vec2f) -> vec3f {
    let r = max(length(p), 1e-4);
    let a = atan2(p.y, p.x);
    var u = -log(r) * 1.2 + A.depth * 2.0;
    var v = a / TAU * 8.0 + u * A.twist * 30.0;
    v = v + sin(u * 2.0 + A.time) * A.melt * 0.6;
    u = u + sin(v * TAU * 0.25 + A.time * 0.7) * A.melt * 0.25;
    let check = sign(sin(u * PI * 2.0) * sin(v * PI * 2.0));
    let stripe = 0.5 + 0.5 * sin(u * PI * 2.0);
    let b = band(u32(fract(u * 0.25) * 6.99));
    var c = palette(fract(u * 0.15 + A.hue)) * (0.25 + 0.75 * step(0.0, check)) * (0.3 + stripe * 0.7);
    c = c * (0.4 + 1.6 * b);
    // the far end glows; the near end (edges) fades a little
    c = c + palette(fract(A.hue + 0.5)) * exp(-r * 14.0) * (0.5 + G.bass * 2.0);
    return c * smoothstep(0.0, 0.08, r) * A.intensity;
}

@fragment
fn fs_feedback(in: FsIn) -> @location(0) vec4f {
    let p = to_p(in.uv);
    let q = warp(mirror(p, A.symmetry));
    var c = sample_prev(q);
    if (A.mode == 2u) {
        // fractal: the last frame twice, turned and shrunk, so it grows copies of itself
        let q2 = rot2(PI * 0.5 + A.time * 0.05) * q * 1.6 + vec2f(0.22, 0.0);
        c = max(c, sample_prev(q2) * 0.85);
    }
    // (a little black-level drain, so faint haze fades to black instead of fogging up)
    c = max(hue_shift(c, A.hue_step) * A.decay - vec3f(0.002), vec3f(0.0));
    if (A.mode == 1u) {
        c = c * 0.6 + tunnel(p) * 0.55;
    } else {
        c = c + emit(p) + flashes(p);
    }
    return vec4f(min(c, vec3f(40.0)), 1.0);
}

@fragment
fn fs_show(in: FsIn) -> @location(0) vec4f {
    let c = textureSampleLevel(prev, samp, in.uv, 0.0).rgb;
    // keep the colours vivid
    let l = luma(c);
    return vec4f(max(l + (c - l) * 1.25, vec3f(0.0)), 1.0);
}
