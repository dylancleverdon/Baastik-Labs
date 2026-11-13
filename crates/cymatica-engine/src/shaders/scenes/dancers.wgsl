// ---- Dancers: neon stick figures on a disco stage ----

struct StageU {
    floor_y: f32,
    lights: f32,
    horizon: f32,
    beat_phase: f32,
    beat_count: f32,
    light_angle: f32,
    flash: f32,
    count: u32,
};

struct Prim {
    ab: vec4f,
    // radius, ring radius (0 = tube), palette position, brightness (negative = reflection)
    style: vec4f,
};

@group(1) @binding(0) var<uniform> S: StageU;
@group(1) @binding(1) var<storage, read> prims: array<Prim>;

// screen units: x in -aspect..aspect, y in -1..1 (up)
fn to_screen(uv: vec2f) -> vec2f { return vec2f((uv.x - 0.5) * 2.0 * G.aspect, (0.5 - uv.y) * 2.0); }

// ---------- the stage: backdrop, spotlights, a flashing tiled floor ----------
@fragment
fn fs_stage(in: FsIn) -> @location(0) vec4f {
    let p = to_screen(in.uv);
    var c = background(in.uv.y) * 0.5;
    let horizon = S.horizon;
    if (S.lights > 0.5) {
        // spotlights from above, sweeping with the music
        for (var i = 0; i < 4; i = i + 1) {
            let fi = f32(i);
            let src = vec2f((fi / 3.0 - 0.5) * 2.4 * G.aspect * 0.6, 1.15);
            let ang = sin(S.light_angle * (0.5 + fi * 0.17) + fi * 1.7) * 0.45;
            let dir = vec2f(sin(ang), -cos(ang));
            let d = p - src;
            let along = dot(d, dir);
            let across = abs(d.x * dir.y - d.y * dir.x);
            let width = 0.06 + along * 0.18;
            let beam = exp(-across * across / (width * width)) * smoothstep(0.0, 0.4, along) * step(0.0, along);
            c = c + palette(fract(fi * 0.27 + S.beat_count * 0.0625)) * beam * 0.09 * (0.6 + G.energy) * G.bg_alpha;
        }
    }
    // the dance floor
    if (p.y < horizon) {
        // (the same perspective the dancers stand in: depth 1 at the front row's feet)
        let depth = (horizon - S.floor_y) / max(horizon - p.y, 0.02);
        let fx = p.x * depth;
        let fz = depth * 3.0;
        let cell = floor(vec2f(fx * 2.0, fz));
        let f = fract(vec2f(fx * 2.0, fz));
        let grout = smoothstep(0.0, 0.06, min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)));
        var tile = vec3f(0.012) * grout;
        if (S.lights > 0.5) {
            // tiles light up on the beat, a different pattern every beat
            let h = hash21(cell + vec2f(S.beat_count * 1.31, 0.7));
            let lit = step(0.72, h) * (1.0 - S.beat_phase * 0.85);
            tile = tile + palette(h) * lit * grout * 0.55 * (0.5 + G.energy);
        }
        let fade = smoothstep(0.0, 0.25, horizon - p.y);
        c = mix(c, tile + c * 0.25, fade) * G.bg_alpha;
    }
    c = c + G.accent.rgb * S.flash * 0.04 * G.bg_alpha;
    return vec4f(c, 1.0);
}

// ---------- figures: neon tubes (and the head ring) ----------
struct PrimOut {
    @builtin(position) pos: vec4f,
    @location(0) p: vec2f,
    @location(1) a: vec2f,
    @location(2) b: vec2f,
    @location(3) style: vec4f,
};

@vertex
fn vs_prim(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> PrimOut {
    let q = prims[ii];
    let a = q.ab.xy;
    let b = q.ab.zw;
    let reach = q.style.x * 7.0 + q.style.y;
    let lo = min(a, b) - vec2f(reach);
    let hi = max(a, b) + vec2f(reach);
    let corner = array<vec2f, 6>(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(1.0, 1.0), vec2f(0.0, 0.0), vec2f(1.0, 1.0), vec2f(0.0, 1.0));
    let p = mix(lo, hi, corner[vi]);
    var out: PrimOut;
    out.pos = vec4f(p.x / G.aspect, p.y, 0.0, 1.0);
    out.p = p;
    out.a = a;
    out.b = b;
    out.style = q.style;
    return out;
}

@fragment
fn fs_prim(in: PrimOut) -> @location(0) vec4f {
    let r = in.style.x;
    var d: f32;
    if (in.style.y > 0.0) {
        // head: a ring
        d = abs(length(in.p - in.a) - in.style.y);
    } else {
        let ab = in.b - in.a;
        let t = clamp(dot(in.p - in.a, ab) / max(dot(ab, ab), 1e-8), 0.0, 1.0);
        d = length(in.p - (in.a + ab * t));
    }
    var bright = in.style.w;
    if (bright < 0.0) {
        // a reflection: fades the further it is below the floor line
        bright = -bright;
    }
    let col = palette_cycle(in.style.z);
    // a white-hot core in a coloured tube, with a soft glow around it
    let core = smoothstep(r, r * 0.35, d);
    let tube = smoothstep(r * 1.8, r, d);
    let halo = exp(-d / (r * 2.5)) * 0.35;
    let c = (mix(col, vec3f(1.0), 0.65) * core * 1.6 + col * (tube * 0.9 + halo)) * bright;
    return vec4f(c, 0.0);
}
