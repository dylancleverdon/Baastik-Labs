// Phosphor: an oscilloscope drawn by a simulated CRT beam. Each segment between two samples gets
// the same beam energy, so slow parts of the trace glow brighter (dwell time); the phosphor keeps
// glowing and decays (persistence).

struct PhU {
    mode: u32,          // 0 XY, 1 goniometer, 2 scope, 3 polar
    count: u32,         // samples to draw
    start: u32,         // first sample (scope trigger)
    graticule: u32,
    gain: f32,
    beam: f32,          // beam width (px)
    intensity: f32,
    persistence: f32,
    curvature: f32,
    color_mode: u32,    // 0 green, 1 amber, 2 blue, 3 white, 4 theme
    time: f32,
    _p: f32,
};

@group(1) @binding(0) var<uniform> P: PhU;
@group(1) @binding(1) var prev: texture_2d<f32>;
@group(1) @binding(2) var samp: sampler;

fn phosphor_color(t: f32) -> vec3f {
    switch P.color_mode {
        case 0u: { return vec3f(0.2, 1.0, 0.35); }
        case 1u: { return vec3f(1.0, 0.62, 0.08); }
        case 2u: { return vec3f(0.25, 0.55, 1.0); }
        case 3u: { return vec3f(0.85, 0.9, 1.0); }
        default: { return palette(t); }
    }
}

fn sample_pos(i: u32) -> vec2f {
    let n = G.wave_len;
    let j = min(P.start + i, n - 1u);
    let s = wave[j];
    let scale = min(G.resolution.x, G.resolution.y) * 0.42 * P.gain;
    var p: vec2f;
    switch P.mode {
        case 1u: {
            // goniometer: mid up, side across
            let m = (s.x + s.y) * 0.7071;
            let sd = (s.x - s.y) * 0.7071;
            p = vec2f(sd, m) * scale;
        }
        case 2u: {
            let x = f32(i) / f32(max(P.count, 2u) - 1u) * 2.0 - 1.0;
            p = vec2f(x * G.resolution.x * 0.45, (s.x + s.y) * 0.5 * scale);
        }
        case 3u: {
            let a = f32(i) / f32(max(P.count, 2u)) * TAU;
            let r = scale * (0.55 + (s.x + s.y) * 0.5 * 0.9);
            p = vec2f(cos(a), sin(a)) * r;
        }
        default: { p = vec2f(s.x, s.y) * scale; }
    }
    return G.resolution * 0.5 + p;
}

// ---- decay the phosphor
@fragment
fn fs_decay(in: FsIn) -> @location(0) vec4f {
    let c = textureSampleLevel(prev, samp, in.uv, 0.0).rgb;
    return vec4f(c * P.persistence, 1.0);
}

// ---- beam segments
struct BeamOut {
    @builtin(position) pos: vec4f,
    @location(0) local: vec2f,     // x along (px from start), y across (px)
    @location(1) len: f32,
    @location(2) energy: f32,
    @location(3) t: f32,
};

@vertex
fn vs_beam(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> BeamOut {
    let a = sample_pos(ii);
    let b = sample_pos(ii + 1u);
    let d = b - a;
    let len = length(d);
    let dir = select(vec2f(1.0, 0.0), d / len, len > 1e-4);
    let nrm = vec2f(-dir.y, dir.x);
    let w = P.beam * 2.5 + 1.0;
    let corner = array<vec2f, 6>(vec2f(0.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(0.0, -1.0), vec2f(1.0, 1.0), vec2f(0.0, 1.0));
    let c = corner[vi];
    let along = c.x * (len + 2.0 * w) - w;
    let p = a + dir * along + nrm * c.y * w;
    var out: BeamOut;
    out.pos = vec4f(p / G.resolution * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0), 0.0, 1.0);
    out.local = vec2f(along, c.y * w);
    out.len = len;
    // equal energy per segment: slow beams (short segments) are brighter
    out.energy = P.intensity * 2.2 / max(len, 0.6) * P.beam;
    out.t = f32(ii) / f32(max(P.count, 1u));
    return out;
}

@fragment
fn fs_beam(in: BeamOut) -> @location(0) vec4f {
    // distance from the pixel to the segment
    let x = clamp(in.local.x, 0.0, in.len);
    let d = length(vec2f(in.local.x - x, in.local.y));
    let g = exp(-d * d / (P.beam * P.beam * 0.8));
    let col = phosphor_color(in.t) * g * min(in.energy, 6.0);
    return vec4f(col, 0.0);
}

// ---- screen: curvature, glass, graticule
@fragment
fn fs_screen(in: FsIn) -> @location(0) vec4f {
    var uv = in.uv - 0.5;
    let k = P.curvature * 0.18;
    uv = uv * (1.0 + k * dot(uv, uv));
    let inside = all(abs(uv) < vec2f(0.5));
    uv = uv + 0.5;
    var c = vec3f(0.0);
    if (inside) {
        c = textureSampleLevel(prev, samp, uv, 0.0).rgb;
        // faint glass base so the screen reads as a tube
        c = c + phosphor_color(0.5) * 0.004;
        if (P.graticule != 0u) {
            let px = uv * G.resolution;
            let cell = min(G.resolution.x, G.resolution.y) * 0.105;
            let gp = abs(fract((px - G.resolution * 0.5) / cell + 0.5) - 0.5) * cell;
            let line = exp(-min(gp.x, gp.y) * 1.6);
            c = c + phosphor_color(0.5) * line * 0.025;
            let axis = exp(-min(abs(px.x - G.resolution.x * 0.5), abs(px.y - G.resolution.y * 0.5)) * 0.8);
            c = c + phosphor_color(0.5) * axis * 0.03;
        }
        // vignette of the tube
        let e = abs(uv - 0.5) * 2.0;
        c = c * smoothstep(1.02, 0.85, max(e.x, e.y));
    }
    return vec4f(c, 1.0);
}
