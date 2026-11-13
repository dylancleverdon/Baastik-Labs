@group(1) @binding(0) var<uniform> U: StarU;
@group(1) @binding(1) var<storage, read> stars: array<Star>;

@fragment
fn fs_space(in: FsIn) -> @location(0) vec4f {
    // deep space: dark gradient + faint nebula in theme colours
    let p = (in.uv - 0.5) * vec2f(G.aspect, 1.0);
    let n = fbm(vec3f(p * 1.8, U.time * 0.01), 4) * 0.5 + 0.5;
    var c = background(in.uv.y) * 0.25;
    c = c + palette(n) * pow(n, 3.0) * 0.06 * (0.5 + G.energy);
    return vec4f(c, 1.0);
}

struct StarOut {
    @builtin(position) pos: vec4f,
    @location(0) local: vec2f,
    @location(1) color: vec3f,
};

@vertex
fn vs_star(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> StarOut {
    let s = stars[ii];
    let vpos = (U.view * vec4f(s.pos, 1.0)).xyz;
    var out: StarOut;
    if (vpos.z > -0.5) {
        out.pos = vec4f(2.0, 2.0, 0.0, 1.0);
        return out;
    }
    let corner = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
    let c = corner[vi];
    let clip = U.proj * vec4f(vpos, 1.0);
    // size in pixels, never below ~1 px so stars don't flicker
    let px = max(U.size * s.size * G.resolution.y / (-vpos.z * 40.0), 0.9);
    out.pos = clip + vec4f(c * px * 2.0 / G.resolution * clip.w, 0.0, 0.0);
    out.local = c;
    let speed = length(s.vel);
    var t: f32;
    switch U.color_by {
        case 1u: { t = s.hue; }
        case 2u: { t = clamp(length(s.pos) / 25.0, 0.0, 1.0); }
        default: { t = clamp(speed / 10.0, 0.0, 1.0); }
    }
    let pixel_area_fix = 1.0 / max(px * px * 0.25, 1.0);
    out.color = palette(t) * U.brightness * (0.25 + 0.75 * smoothstep(0.0, 6.0, speed)) * mix(1.0, pixel_area_fix, 0.6) * 0.5;
    return out;
}

@fragment
fn fs_star(in: StarOut) -> @location(0) vec4f {
    let r2 = dot(in.local, in.local);
    if (r2 > 1.0) { discard; }
    return vec4f(in.color * exp(-r2 * 4.0), 0.0);
}
