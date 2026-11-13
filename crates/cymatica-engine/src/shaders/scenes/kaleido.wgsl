// ---- Kaleidoscope: patterns folded into mirror segments, flowing outward forever ----

struct KaleidoU {
    segments: f32,
    style: u32,
    flow: f32,
    spin: f32,
    detail: f32,
    brightness: f32,
    core: f32,
    shard_phase: f32,
    petal_phase: f32,
    p0: f32,
    p1: f32,
    p2: f32,
};

@group(1) @binding(0) var<uniform> K: KaleidoU;

// Mirror-fold a point into one wedge of the kaleidoscope.
fn fold(p: vec2f) -> vec2f {
    let seg = TAU / K.segments;
    var a = atan2(p.y, p.x) + K.spin;
    a = abs(fract(a / seg + 0.5) - 0.5) * seg;
    return vec2f(cos(a), sin(a)) * length(p);
}

// Stained glass: Voronoi shards, each lit by one frequency band, with bright leading between.
fn glass(q: vec2f, seed: f32) -> vec3f {
    let g = floor(q);
    let f = fract(q);
    var best = 8.0;
    var second = 8.0;
    var id = vec2f(0.0);
    for (var y = -1; y <= 1; y = y + 1) {
        for (var x = -1; x <= 1; x = x + 1) {
            let o = vec2f(f32(x), f32(y));
            let h = hash22(g + o + vec2f(seed * 13.1, seed * 7.7));
            let pnt = o + 0.5 + 0.38 * sin(K.shard_phase * 1.3 + h * TAU);
            let d = length(pnt - f);
            if (d < best) {
                second = best;
                best = d;
                id = g + o;
            } else if (d < second) {
                second = d;
            }
        }
    }
    let h = hash21(id + vec2f(seed * 3.3, 1.7));
    let b = band(u32(h * 6.99));
    // dark leading between the pieces, with a thin bright line in it on the treble
    let gap = second - best;
    let lead = smoothstep(0.02, 0.06, gap);
    let line = exp(-gap * 90.0);
    let col = palette_cycle(h + K.shard_phase * 0.05);
    // each piece glows like a jewel: brighter towards its middle, by its band
    let jewel = (1.15 - best) * (0.05 + 1.1 * b * b);
    return col * jewel * lead + mix(col, vec3f(1.0), 0.6) * line * (0.02 + 0.7 * G.treble);
}

// Petals: glowing discs that orbit inside the wedge (the folding turns them into flowers).
fn petals(q: vec2f, seed: f32) -> vec3f {
    var c = vec3f(0.0);
    for (var i = 0u; i < 7u; i = i + 1u) {
        let fi = f32(i);
        let b = band(i % 7u);
        let ang = fi * 0.9 + seed + K.petal_phase * (0.2 + fi * 0.06);
        let center = vec2f(cos(ang), sin(ang)) * (0.15 + fract(fi * 0.37 + seed * 0.11) * 0.8);
        let r = 0.025 + 0.06 * hash11(fi + seed) + 0.05 * b;
        let d = length(q - center) - r;
        // a glowing ring with a faint fill: the folding turns them into rosettes
        let ring = exp(-abs(d) * 120.0) * (0.25 + 1.2 * b);
        let fill = smoothstep(0.004, -0.004, d) * (0.04 + 0.25 * b);
        c = c + palette_cycle(fi / 7.0 + seed * 0.21 + K.petal_phase * 0.03) * (ring + fill);
    }
    return c;
}

// Spectrum lace: rays and rings drawn by the spectrum itself.
fn lace(q: vec2f, seed: f32) -> vec3f {
    let r = length(q);
    let a = atan2(q.y, q.x);
    let s = spec(fract(r * 0.9 + seed * 0.37));
    let ray = exp(-abs(sin(a * 4.0 + r * 5.0 - K.shard_phase + seed)) * 18.0) * (0.15 + 1.6 * s);
    let ring = exp(-abs(fract(r * 3.0 + seed) - 0.5) * 30.0) * (0.1 + G.bass * 1.2);
    return palette(fract(r * 0.6 + a * 0.15 + seed * 0.3)) * (ray + ring * 0.7);
}

fn layer(q: vec2f, seed: f32) -> vec3f {
    switch K.style {
        case 0u: { return glass(q, seed); }
        case 1u: { return petals(q * 0.35, seed); }
        case 2u: { return lace(q * 0.25, seed); }
        default: { return glass(q, seed) * 0.7 + petals(q * 0.35, seed) * 0.8 + lace(q * 0.25, seed) * 0.6; }
    }
}

@fragment
fn fs_kaleido(in: FsIn) -> @location(0) vec4f {
    let p = (in.uv - 0.5) * vec2f(G.aspect, 1.0) * 2.0;
    let q = fold(p);
    let r = length(p);
    let density = mix(2.5, 7.0, K.detail);
    // three copies at doubling scales, each fading in small and out large: an endless zoom
    var col = vec3f(0.0);
    for (var i = 0; i < 3; i = i + 1) {
        let fi = f32(i);
        let t = fract(K.flow + fi / 3.0);
        let zoom = exp2(t * 2.0);
        let w = sin(PI * t);
        // a new pattern each time a copy comes round again
        let seed = floor(K.flow + fi / 3.0) * 3.0 + fi;
        col = col + layer(q * density / zoom, seed) * w * 0.8;
    }
    // light at the centre
    col = col + (G.accent.rgb * 0.6 + palette(0.5) * 0.4) * exp(-r * 5.0) * K.core * (0.3 + G.bass * 2.0);
    // fade towards the corners
    col = col * smoothstep(1.9, 0.6, r);
    return vec4f(col * K.brightness * (0.75 + 0.5 * G.energy), 1.0);
}
