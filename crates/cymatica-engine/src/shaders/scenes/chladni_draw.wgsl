// plate area in pixels (square, centred)
fn plate_rect() -> vec4f {
    let side = min(G.resolution.x, G.resolution.y) * 0.9;
    let origin = (G.resolution - side) * 0.5;
    return vec4f(origin, side, side);
}

@fragment
fn fs_plate(in: FsIn) -> @location(0) vec4f {
    let r = plate_rect();
    let px = in.uv * G.resolution;
    let p = (px - r.xy) / r.zw;
    var c = background(in.uv.y) * 0.4;
    var inside = all(p >= vec2f(0.0)) && all(p <= vec2f(1.0));
    if (C.plate == 1u) { inside = length(p - 0.5) <= 0.5; }
    if (inside && C.show_plate != 0u) {
        // brushed dark metal with the standing wave faintly glowing
        let q = p - 0.5;
        // brushed metal: fine noise stretched around the centre (no regular rings → no moiré)
        let ang = atan2(q.y, q.x);
        let brushed = fbm(vec3f(length(q) * 180.0, ang * 2.0, 0.0), 2) * 0.5 + 0.5;
        c = vec3f(0.03, 0.03, 0.035) * (0.85 + 0.3 * brushed);
        let a = abs(amp(p));
        c = c + palette(0.2) * a * 0.035 * (0.3 + G.energy);
        // rim
        var edge = min(min(p.x, 1.0 - p.x), min(p.y, 1.0 - p.y));
        if (C.plate == 1u) { edge = 0.5 - length(q); }
        c = c + G.accent.rgb * exp(-edge * 300.0) * 0.25;
    }
    return vec4f(c, 1.0);
}

struct GrainOut {
    @builtin(position) pos: vec4f,
    @location(0) local: vec2f,
    @location(1) color: vec3f,
};

@group(1) @binding(1) var<storage, read> grains_ro: array<Grain>;

@vertex
fn vs_grain(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> GrainOut {
    let g = grains_ro[ii];
    let r = plate_rect();
    let center = r.xy + g.pos * r.zw;
    let corner = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
    let c = corner[vi];
    let size = C.grain * (G.resolution.y / 1080.0) + 0.6;
    let p = center + c * size;
    var out: GrainOut;
    out.pos = vec4f(p / G.resolution * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0), 0.0, 1.0);
    out.local = c;
    let speed = length(g.vel);
    var t: f32;
    switch C.color_by {
        case 1u: { t = fract(f32(ii % 7919u) / 7919.0); }
        case 2u: { t = length(g.pos - 0.5) * 1.4; }
        default: { t = clamp(0.25 + speed * 4.0, 0.0, 1.0); }
    }
    out.color = palette(t) * C.glow * (0.35 + 0.65 * smoothstep(0.0, 0.05, 0.05 - speed)) * 0.55;
    return out;
}

@fragment
fn fs_grain(in: GrainOut) -> @location(0) vec4f {
    let r2 = dot(in.local, in.local);
    if (r2 > 1.0) { discard; }
    return vec4f(in.color * (1.0 - r2), 0.0);
}
