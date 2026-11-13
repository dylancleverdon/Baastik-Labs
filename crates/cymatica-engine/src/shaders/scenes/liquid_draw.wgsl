// Liquid rendering: screen-space fluid. Particles become sphere-impostor depths, the depth is
// smoothed into a surface, and that surface is shaded with refraction, reflection and absorption.

struct DrawU {
    view: mat4x4f,
    proj: mat4x4f,
    inv_view: mat4x4f,
    eye: vec4f,
    grid: vec4f,            // gx, gy, gz, particle count
    resolution: vec2f,      // fluid buffer size
    blur_dir: vec2f,
    radius: f32,
    tan_half_fov: f32,
    aspect: f32,
    thickness_scale: f32,
    absorb: f32,
    refraction: f32,
    foam: f32,
    gloss: f32,
    color_mode: u32,        // 0 theme colour, 1 rainbow dye, 2 by depth
    blur_radius: f32,
    kick: f32,
    tank_style: u32,
    liquid_color: vec4f,
    out_resolution: vec2f,
    time: f32,
    _p: f32,
};

struct Particle {
    pos: vec3f,
    dye: f32,
    vel: vec3f,
    speed: f32,
    c0: vec3f,
    _a: f32,
    c1: vec3f,
    _b: f32,
    c2: vec3f,
    _c: f32,
};

@group(1) @binding(0) var<uniform> D: DrawU;
@group(1) @binding(1) var<storage, read> particles: array<Particle>;
@group(1) @binding(2) var tex_a: texture_2d<f32>;   // depth (blur input / composite)
@group(1) @binding(3) var tex_b: texture_2d<f32>;   // thickness + dye
@group(1) @binding(4) var tex_bg: texture_2d<f32>;  // background (tank room)

const FAR: f32 = 10000.0;

fn load_clamped(t: texture_2d<f32>, p: vec2<i32>) -> vec4f {
    let s = vec2<i32>(textureDimensions(t));
    return textureLoad(t, clamp(p, vec2<i32>(0), s - 1), 0);
}

fn bilinear(t: texture_2d<f32>, uv: vec2f) -> vec4f {
    let s = vec2f(textureDimensions(t));
    let x = uv * s - 0.5;
    let i = vec2<i32>(floor(x));
    let f = fract(x);
    let a = load_clamped(t, i);
    let b = load_clamped(t, i + vec2<i32>(1, 0));
    let c = load_clamped(t, i + vec2<i32>(0, 1));
    let d = load_clamped(t, i + vec2<i32>(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// ---------- the tank room (also what the liquid refracts) ----------
fn world_ray(uv: vec2f) -> vec3f {
    let ndc = vec2f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let dir_view = normalize(vec3f(ndc.x * D.tan_half_fov * D.aspect, ndc.y * D.tan_half_fov, -1.0));
    return normalize((D.inv_view * vec4f(dir_view, 0.0)).xyz);
}

fn env(dir: vec3f) -> vec3f {
    let y = dir.y * 0.5 + 0.5;
    var c = mix(G.bg_bottom.rgb * 1.5, G.bg_top.rgb * 2.0 + G.accent.rgb * 0.03, smoothstep(0.2, 0.9, y));
    // studio lights in theme colours
    let l1 = normalize(vec3f(0.6, 0.7, 0.4));
    let l2 = normalize(vec3f(-0.7, 0.5, -0.3));
    let l3 = normalize(vec3f(0.0, 1.0, 0.0));
    c = c + palette(0.85) * pow(max(dot(dir, l1), 0.0), 60.0) * 6.0;
    c = c + palette(0.35) * pow(max(dot(dir, l2), 0.0), 40.0) * 4.0;
    c = c + vec3f(1.0) * pow(max(dot(dir, l3), 0.0), 200.0) * 10.0;
    return c;
}

fn seg_dist(ro: vec3f, rd: vec3f, a: vec3f, b: vec3f) -> f32 {
    // closest distance between the ray and segment ab
    let ba = b - a;
    let oa = ro - a;
    let d = dot(rd, ba);
    let denom = dot(ba, ba) - d * d;
    var t = 0.0;
    if (abs(denom) > 1e-5) { t = clamp((dot(oa, ba) - dot(oa, rd) * d) / denom, 0.0, 1.0); }
    let pa = a + ba * t;
    let s = max(dot(pa - ro, rd), 0.0);
    return length(ro + rd * s - pa);
}

fn tank_room(uv: vec2f) -> vec3f {
    let ro = D.eye.xyz;
    let rd = world_ray(uv);
    var col = env(rd) * 0.35;
    let g = D.grid.xyz;
    // floor
    if (rd.y < -1e-4) {
        let t = (0.0 - ro.y) / rd.y;
        let p = ro + rd * t;
        let inside = p.x > 0.0 && p.x < g.x && p.z > 0.0 && p.z < g.z;
        if (inside) {
            let q = p.xz / 4.0;
            let line = abs(fract(q) - 0.5);
            let grid_line = exp(-min(line.x, line.y) * 40.0);
            var tile = palette(fract(hash21(floor(q)) * 0.25 + p.x / g.x * 0.6)) * 0.18;
            if (D.tank_style == 1u) { tile = vec3f(0.02); }
            col = tile + palette(0.6) * grid_line * 0.15;
            // the subwoofer: a glowing ring in the middle of the floor
            let r = length(p.xz - g.xz * 0.5);
            let ring = exp(-abs(r - g.x * 0.12) * 1.5);
            col = col + G.accent.rgb * ring * (0.4 + 3.0 * D.kick);
        } else {
            let fade = exp(-t / (g.x * 2.5));
            let q = p.xz / 8.0;
            let line = abs(fract(q) - 0.5);
            col = mix(col, vec3f(0.01) + G.accent.rgb * exp(-min(line.x, line.y) * 50.0) * 0.05, fade);
        }
    }
    // glass edges
    let e = array<vec3f, 8>(
        vec3f(0.0, 0.0, 0.0), vec3f(g.x, 0.0, 0.0), vec3f(g.x, 0.0, g.z), vec3f(0.0, 0.0, g.z),
        vec3f(0.0, g.y, 0.0), vec3f(g.x, g.y, 0.0), vec3f(g.x, g.y, g.z), vec3f(0.0, g.y, g.z));
    var d = 1e9;
    d = min(d, seg_dist(ro, rd, e[0], e[1])); d = min(d, seg_dist(ro, rd, e[1], e[2]));
    d = min(d, seg_dist(ro, rd, e[2], e[3])); d = min(d, seg_dist(ro, rd, e[3], e[0]));
    d = min(d, seg_dist(ro, rd, e[4], e[5])); d = min(d, seg_dist(ro, rd, e[5], e[6]));
    d = min(d, seg_dist(ro, rd, e[6], e[7])); d = min(d, seg_dist(ro, rd, e[7], e[4]));
    d = min(d, seg_dist(ro, rd, e[0], e[4])); d = min(d, seg_dist(ro, rd, e[1], e[5]));
    d = min(d, seg_dist(ro, rd, e[2], e[6])); d = min(d, seg_dist(ro, rd, e[3], e[7]));
    col = col + (vec3f(0.6, 0.8, 1.0) * 0.3 + palette(0.7) * 0.4) * exp(-d * 6.0) * 1.2;
    return col;
}

@fragment
fn fs_background(in: FsIn) -> @location(0) vec4f {
    return vec4f(tank_room(in.uv) * G.bg_alpha, 1.0);
}

// ---------- particle impostors ----------
struct SphereOut {
    @builtin(position) pos: vec4f,
    @location(0) local: vec2f,
    @location(1) view_center: vec3f,
    @location(2) color: vec4f,
};

fn dye_color(p: Particle, view_depth: f32) -> vec3f {
    switch D.color_mode {
        case 1u: { return palette_cycle(p.dye); }
        case 2u: { return palette(clamp(1.0 - p.pos.y / (D.grid.y * 0.6), 0.0, 1.0)); }
        default: { return D.liquid_color.rgb; }
    }
}

@vertex
fn vs_sphere(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> SphereOut {
    let p = particles[ii];
    let corner = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
    let c = corner[vi];
    let vc = (D.view * vec4f(p.pos, 1.0)).xyz;
    var out: SphereOut;
    let v = vc + vec3f(c * D.radius, 0.0);
    out.pos = D.proj * vec4f(v, 1.0);
    out.local = c;
    out.view_center = vc;
    out.color = vec4f(dye_color(p, -vc.z), p.speed);
    return out;
}

struct DepthOut {
    @location(0) depth: vec4f,
    @builtin(frag_depth) frag_depth: f32,
};

@fragment
fn fs_depth(in: SphereOut) -> DepthOut {
    let r2 = dot(in.local, in.local);
    if (r2 > 1.0) { discard; }
    let nz = sqrt(1.0 - r2);
    let surface = in.view_center + vec3f(0.0, 0.0, nz * D.radius);
    let clip = D.proj * vec4f(surface, 1.0);
    var out: DepthOut;
    out.depth = vec4f(-surface.z, 0.0, 0.0, 1.0);
    out.frag_depth = clamp(clip.z / clip.w, 0.0, 1.0);
    return out;
}

@fragment
fn fs_thickness(in: SphereOut) -> @location(0) vec4f {
    let r2 = dot(in.local, in.local);
    if (r2 > 1.0) { discard; }
    let w = (1.0 - r2) * D.thickness_scale;
    return vec4f(in.color.rgb * w, w);
}

// ---------- depth smoothing (bilateral, separable) ----------
@fragment
fn fs_blur(in: FsIn) -> @location(0) vec4f {
    let p = vec2<i32>(in.pos.xy);
    let center = load_clamped(tex_a, p).r;
    if (center >= FAR * 0.5) { return vec4f(FAR, 0.0, 0.0, 1.0); }
    // radius shrinks with distance so the filter covers about one particle on screen
    let px = D.radius * D.resolution.y / (2.0 * D.tan_half_fov * center);
    let radius = i32(clamp(px * D.blur_radius, 2.0, 14.0));
    let sigma = f32(radius) * 0.5 + 0.5;
    let depth_sigma = D.radius * 2.2;
    var sum = 0.0;
    var wsum = 0.0;
    for (var i = -14; i <= 14; i = i + 1) {
        if (abs(i) > radius) { continue; }
        let q = p + vec2<i32>(D.blur_dir * f32(i));
        let d = load_clamped(tex_a, q).r;
        if (d >= FAR * 0.5) { continue; }
        let ws = exp(-f32(i * i) / (2.0 * sigma * sigma));
        let dz = (d - center) / depth_sigma;
        let wr = exp(-dz * dz);
        sum = sum + d * ws * wr;
        wsum = wsum + ws * wr;
    }
    return vec4f(sum / max(wsum, 1e-5), 0.0, 0.0, 1.0);
}

// ---------- shading ----------
fn view_pos(uv: vec2f, d: f32) -> vec3f {
    let ndc = vec2f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return vec3f(ndc.x * D.tan_half_fov * D.aspect * d, ndc.y * D.tan_half_fov * d, -d);
}

fn depth_at(uv: vec2f) -> f32 {
    return load_clamped(tex_a, vec2<i32>(uv * D.resolution)).r;
}

@fragment
fn fs_composite(in: FsIn) -> @location(0) vec4f {
    let uv = in.uv;
    let bg = bilinear(tex_bg, uv).rgb;
    let d = depth_at(uv);
    let thick = bilinear(tex_b, uv);
    if (d >= FAR * 0.5) {
        return vec4f(bg, 1.0);
    }
    // normal from the smoothed depth (pick the smaller difference at edges)
    let t = 1.0 / D.resolution;
    let p = view_pos(uv, d);
    let px1 = view_pos(uv + vec2f(t.x, 0.0), depth_at(uv + vec2f(t.x, 0.0)));
    let px0 = view_pos(uv - vec2f(t.x, 0.0), depth_at(uv - vec2f(t.x, 0.0)));
    let py1 = view_pos(uv + vec2f(0.0, t.y), depth_at(uv + vec2f(0.0, t.y)));
    let py0 = view_pos(uv - vec2f(0.0, t.y), depth_at(uv - vec2f(0.0, t.y)));
    var ddx = px1 - p;
    if (abs(px0.z - p.z) < abs(ddx.z)) { ddx = p - px0; }
    var ddy = py1 - p;
    if (abs(py0.z - p.z) < abs(ddy.z)) { ddy = p - py0; }
    var n_view = normalize(cross(ddy, ddx));
    if (n_view.z < 0.0) { n_view = -n_view; }
    let n = normalize((D.inv_view * vec4f(n_view, 0.0)).xyz);
    let v_view = normalize(p);
    let rd = normalize((D.inv_view * vec4f(v_view, 0.0)).xyz);

    let thickness = thick.a;
    var base = D.liquid_color.rgb;
    if (D.color_mode != 0u && thickness > 1e-4) {
        base = thick.rgb / thickness;
    }
    // refraction: the room seen through the liquid, bent and tinted by its thickness
    let bend = n_view.xy * D.refraction * min(thickness, 4.0) * 0.012;
    let seen = bilinear(tex_bg, uv + vec2f(bend.x, -bend.y)).rgb;
    let absorb = exp(-min(thickness, 6.0) * D.absorb * (vec3f(1.05) - base) * 0.9);
    var col = seen * absorb * (0.55 + 0.45 * base) + base * (1.0 - absorb.g) * 0.18 * (0.6 + G.energy);
    // reflection
    let cosi = clamp(dot(-rd, n), 0.0, 1.0);
    let fresnel = 0.02 + 0.98 * pow(1.0 - cosi, 5.0);
    let refl = env(reflect(rd, n));
    col = mix(col, refl, fresnel * D.gloss);
    // specular from a key light and a light behind the camera, plus a rim
    let l = normalize(vec3f(0.6, 0.7, 0.4));
    let h = normalize(l - rd);
    col = col + vec3f(1.0) * pow(max(dot(n, h), 0.0), 160.0) * 5.0 * D.gloss;
    let l2 = normalize(-rd + vec3f(0.0, 0.6, 0.0));
    let h2 = normalize(l2 - rd);
    col = col + palette(0.9) * pow(max(dot(n, h2), 0.0), 90.0) * 1.5 * D.gloss;
    col = col + base * pow(1.0 - cosi, 3.0) * 0.6 * D.gloss;
    return vec4f(col, 1.0);
}

// ---------- foam / spray ----------
@vertex
fn vs_foam(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> SphereOut {
    let p = particles[ii];
    var out: SphereOut;
    // only genuinely fast liquid sprays (speeds cap at ~4.5 cells per time unit)
    let fast = smoothstep(3.2, 4.4, p.speed);
    if (fast <= 0.01 || D.foam <= 0.0) {
        out.pos = vec4f(2.0, 2.0, 0.0, 1.0);
        return out;
    }
    let corner = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
    let c = corner[vi];
    let vc = (D.view * vec4f(p.pos, 1.0)).xyz;
    let v = vc + vec3f(c * D.radius * 0.45, 0.0);
    out.pos = D.proj * vec4f(v, 1.0);
    out.local = c;
    out.view_center = vc;
    out.color = vec4f(mix(vec3f(1.0), palette(0.9), 0.3) * fast * D.foam * 0.9, 1.0);
    return out;
}

@fragment
fn fs_foam(in: SphereOut) -> @location(0) vec4f {
    let r2 = dot(in.local, in.local);
    if (r2 > 1.0) { discard; }
    return vec4f(in.color.rgb * exp(-r2 * 3.0), 0.0);
}
