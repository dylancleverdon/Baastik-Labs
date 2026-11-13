// Chladni: sand on a vibrating plate. The plate vibrates in a standing-wave mode (n, m); grains get
// kicked around where it moves and come to rest on the nodal lines, drawing the figure.

struct ChU {
    mode_a: vec4f,     // n, m of the previous mode, then the current one
    blend: f32,        // 0 → previous, 1 → current
    plate: u32,        // 0 square, 1 circle
    count: u32,
    vibration: f32,
    drift: f32,
    grain: f32,
    glow: f32,
    color_by: u32,
    seed: u32,
    show_plate: u32,
    _p0: u32,
    _p1: u32,
};

struct Grain {
    pos: vec2f,
    vel: vec2f,
};

@group(1) @binding(0) var<uniform> C: ChU;
// Bessel-like radial factor for circular plates (cheap approximation).
fn radial(n: f32, k: f32, r: f32) -> f32 {
    let x = k * r;
    let near = pow(x * 0.5, n) / max(1.0, n * n * 0.25 + 1.0);
    let far = cos(x - n * PI * 0.5 - PI * 0.25) * sqrt(2.0 / (PI * max(x, 1e-3)));
    return mix(near, far, smoothstep(n * 0.6, n * 0.6 + 2.0, x));
}

fn amp_mode(p: vec2f, nm: vec2f) -> f32 {
    if (C.plate == 1u) {
        let q = (p - 0.5) * 2.0;
        let r = length(q);
        let th = atan2(q.y, q.x);
        let k = PI * (nm.y + nm.x * 0.5 + 0.8);
        return radial(nm.x, k, r) * cos(nm.x * th);
    }
    let a = cos(nm.x * PI * p.x) * cos(nm.y * PI * p.y);
    let b = cos(nm.y * PI * p.x) * cos(nm.x * PI * p.y);
    return a - b;
}

fn amp(p: vec2f) -> f32 {
    return mix(amp_mode(p, C.mode_a.xy), amp_mode(p, C.mode_a.zw), C.blend);
}

