// Ripples: a heightfield water surface (wave equation) in a pool, with refraction, reflections and
// real-time caustics (light refracted through the surface; brightness = how much it focuses).

struct RipU {
    sim_size: vec2f,
    yaw: f32,
    tilt: f32,
    depth: f32,
    caustics: f32,
    reflections: f32,
    tint: f32,
    damping: f32,
    drop_count: u32,
    floor_style: u32,
    time: f32,
    faraday: f32,
    faraday_freq: f32,
    wave_gain: f32,
    _pad: f32,
};

struct Drop {
    pos_r_s: vec4f, // xy pool coords (-1..1), z radius, w strength
};

@group(1) @binding(0) var<uniform> R: RipU;
@group(1) @binding(1) var<storage, read> drops: array<Drop>;
@group(1) @binding(2) var water: texture_2d<f32>;    // r = height, g = velocity
@group(1) @binding(3) var caust: texture_2d<f32>;

fn load_w(p: vec2i) -> vec4f {
    let s = vec2i(R.sim_size);
    return textureLoad(water, clamp(p, vec2i(0), s - 1), 0);
}

// Bilinear height at pool coords (-1..1).
fn height_at(xz: vec2f) -> f32 {
    let uv = (xz * 0.5 + 0.5) * R.sim_size - 0.5;
    let i = vec2i(floor(uv));
    let f = fract(uv);
    let a = load_w(i).r;
    let b = load_w(i + vec2i(1, 0)).r;
    let c = load_w(i + vec2i(0, 1)).r;
    let d = load_w(i + vec2i(1, 1)).r;
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y) * R.wave_gain;
}

fn normal_at(xz: vec2f) -> vec3f {
    return normal_at_scale(xz, 2.0 / R.sim_size.x);
}

// slopes measured over +-e: a wider e ignores ripples finer than that
fn normal_at_scale(xz: vec2f, e: f32) -> vec3f {
    let hx = height_at(xz + vec2f(e, 0.0)) - height_at(xz - vec2f(e, 0.0));
    let hz = height_at(xz + vec2f(0.0, e)) - height_at(xz - vec2f(0.0, e));
    return normalize(vec3f(-hx, 2.0 * e, -hz));
}

// ---------- simulation step (one fragment per cell) ----------
@fragment
fn fs_step(in: FsIn) -> @location(0) vec4f {
    let p = vec2i(in.pos.xy);
    var info = load_w(p);
    let n0 = load_w(p + vec2i(1, 0));
    let n1 = load_w(p - vec2i(1, 0));
    let n2 = load_w(p + vec2i(0, 1));
    let n3 = load_w(p - vec2i(0, 1));
    let avg = (n0.r + n1.r + n2.r + n3.r) * 0.25;
    let avg_v = (n0.g + n1.g + n2.g + n3.g) * 0.25;
    info.g = info.g + (avg - info.r) * 2.0;
    // viscosity: this scheme keeps grid-scale ripples forever, and with busy music they pile up
    // into a glittering white sheet; blending velocity towards its neighbours damps them hard
    // (checkerboard: x0.76 per step) while long waves are left alone
    info.g = mix(info.g, avg_v, 0.12);
    info.g = info.g * R.damping;
    info.r = info.r + info.g;
    // drops
    let xz = (vec2f(p) + 0.5) / R.sim_size * 2.0 - 1.0;
    for (var i = 0u; i < R.drop_count; i = i + 1u) {
        let d = drops[i].pos_r_s;
        let k = max(0.0, 1.0 - length(xz - d.xy) / d.z);
        info.r = info.r + (0.5 - cos(k * PI) * 0.5) * d.w;
    }
    // Faraday: three oscillating sources make interference patterns
    if (R.faraday > 0.0) {
        for (var k = 0; k < 3; k = k + 1) {
            let a = f32(k) * TAU / 3.0 + R.time * 0.05;
            let c = vec2f(cos(a), sin(a)) * 0.35;
            let r = length(xz - c);
            info.r = info.r + sin(R.time * R.faraday_freq) * exp(-r * r * 900.0) * R.faraday * 0.02;
        }
    }
    // keep the edges calm
    let edge = max(abs(xz.x), abs(xz.y));
    info.g = info.g * select(1.0, 0.9, edge > 0.985);
    return vec4f(clamp(info.r, -2.0, 2.0), clamp(info.g, -2.0, 2.0), 0.0, 1.0);
}

// ---------- caustics: refract light through a surface mesh onto the floor ----------
const LIGHT: vec3f = vec3f(0.35, -1.0, 0.25);
const CAUSTIC_GRID: u32 = 200u;

struct CausticOut {
    @builtin(position) pos: vec4f,
    @location(0) old_pos: vec3f,
    @location(1) new_pos: vec3f,
};

fn floor_hit(p: vec3f, dir: vec3f) -> vec3f {
    let t = (-R.depth - p.y) / dir.y;
    return p + dir * t;
}

@vertex
fn vs_caustics(@builtin(vertex_index) vi: u32) -> CausticOut {
    let quad = vi / 6u;
    let corner = array<vec2u, 6>(vec2u(0u, 0u), vec2u(1u, 0u), vec2u(1u, 1u), vec2u(0u, 0u), vec2u(1u, 1u), vec2u(0u, 1u));
    let g = vec2u(quad % CAUSTIC_GRID, quad / CAUSTIC_GRID) + corner[vi % 6u];
    let xz = vec2f(g) / f32(CAUSTIC_GRID) * 2.0 - 1.0;
    let light = normalize(LIGHT);
    let flat_ray = refract(light, vec3f(0.0, 1.0, 0.0), 1.0 / 1.333);
    // slopes at the caustic mesh's own scale: finer ripples would focus every quad into a dot
    let n = normal_at_scale(xz, 2.0 / f32(CAUSTIC_GRID));
    let ray = refract(light, n, 1.0 / 1.333);
    let surface = vec3f(xz.x, height_at(xz), xz.y);
    var out: CausticOut;
    out.old_pos = floor_hit(vec3f(xz.x, 0.0, xz.y), flat_ray);
    out.new_pos = floor_hit(surface, ray);
    // project onto the floor texture (pool coords -1..1 → clip)
    out.pos = vec4f(out.new_pos.x, -out.new_pos.z, 0.0, 1.0);
    return out;
}

@fragment
fn fs_caustics(in: CausticOut) -> @location(0) vec4f {
    let old_area = length(dpdx(in.old_pos)) * length(dpdy(in.old_pos));
    let new_area = length(dpdx(in.new_pos)) * length(dpdy(in.new_pos));
    let i = old_area / max(new_area, 1e-7) * 0.18;
    return vec4f(min(i, 6.0), 0.0, 0.0, 1.0);
}

// ---------- rendering ----------
struct Cam {
    pos: vec3f,
    fwd: vec3f,
    right: vec3f,
    up: vec3f,
};

fn camera() -> Cam {
    var c: Cam;
    let tilt = mix(0.45, 1.5, R.tilt);
    let dist = mix(2.35, 1.75, R.tilt);
    c.pos = vec3f(sin(R.yaw) * cos(tilt), sin(tilt), cos(R.yaw) * cos(tilt)) * dist;
    c.fwd = normalize(-c.pos + vec3f(0.0, -0.25, 0.0));
    c.right = normalize(cross(c.fwd, vec3f(0.0, 1.0, 0.0)));
    c.up = cross(c.right, c.fwd);
    return c;
}

fn sky(dir: vec3f) -> vec3f {
    let y = clamp(dir.y, 0.0, 1.0);
    var col = mix(G.bg_bottom.rgb * 2.5 + G.accent.rgb * 0.05, G.bg_top.rgb * 1.5, pow(y, 0.6));
    // palette aurora bands
    let band = sin(dir.x * 3.0 + R.time * 0.1) * 0.5 + 0.5;
    col = col + palette(band) * 0.12 * smoothstep(0.2, 0.7, y);
    let sun = normalize(-LIGHT);
    let s = max(dot(dir, sun), 0.0);
    col = col + (G.accent.rgb * 0.5 + vec3f(1.0)) * (pow(s, 600.0) * 30.0 + pow(s, 30.0) * 0.4);
    return col;
}

fn tiles(xz: vec2f) -> vec3f {
    var base: vec3f;
    switch R.floor_style {
        case 1u: {
            // sand
            let n = fbm(vec3f(xz * 6.0, 0.0), 4) * 0.5 + 0.5;
            base = mix(vec3f(0.55, 0.47, 0.33), vec3f(0.85, 0.76, 0.58), n) * 0.7;
        }
        case 2u: {
            // neon grid
            let g = abs(fract(xz * 5.0) - 0.5);
            let line = exp(-min(g.x, g.y) * 60.0);
            base = vec3f(0.01) + palette(fract(length(xz) * 0.4 + R.time * 0.03)) * line * 3.0;
        }
        default: {
            let cell = floor(xz * 8.0);
            let g = abs(fract(xz * 8.0) - 0.5);
            let grout = smoothstep(0.47, 0.5, max(g.x, g.y));
            let variation = hash21(cell) * 0.15;
            let t = clamp(length(xz) * 0.55 + variation, 0.0, 1.0);
            base = mix(palette(t) * 0.55 + vec3f(0.12), vec3f(0.05), grout);
        }
    }
    return base;
}

fn caustic_at(xz: vec2f) -> f32 {
    // vs_caustics writes clip.y = -z, i.e. texture row (1 + z) / 2
    let uv = xz * 0.5 + 0.5;
    let s = vec2i(textureDimensions(caust));
    let p = clamp(vec2i(uv * vec2f(s)), vec2i(0), s - 1);
    return textureLoad(caust, p, 0).r;
}

// Colour seen along a ray travelling underwater from p.
fn underwater(p: vec3f, dir: vec3f) -> vec3f {
    // floor or walls of the box [-1,1] x [-depth, 0] x [-1,1]
    var t = (-R.depth - p.y) / min(dir.y, -1e-4);
    var hit = p + dir * t;
    var col: vec3f;
    if (abs(hit.x) > 1.0 || abs(hit.z) > 1.0) {
        // distance to the wall the ray is heading for (a near-zero direction never reaches it)
        var tx = 1e6;
        var tz = 1e6;
        if (abs(dir.x) > 1e-6) { tx = max((sign(dir.x) - p.x) / dir.x, 0.0); }
        if (abs(dir.z) > 1e-6) { tz = max((sign(dir.z) - p.z) / dir.z, 0.0); }
        t = min(min(tx, tz), t);
        hit = p + dir * t;
        let wall_uv = select(vec2f(hit.z, hit.y), vec2f(hit.x, hit.y), tz < tx);
        col = tiles(wall_uv) * 0.6;
    } else {
        col = tiles(hit.xz);
        let c = caustic_at(hit.xz);
        col = col * (0.35 + c * R.caustics * 1.6) + G.accent.rgb * c * R.caustics * 0.08;
    }
    // absorption: deeper paths take on the water's colour
    let water_col = mix(vec3f(0.6, 0.9, 1.0), palette(0.15), 0.6);
    let absorb = exp(-clamp(t, 0.0, 8.0) * R.tint * 2.2 * (1.0 - water_col));
    return col * absorb;
}

@fragment
fn fs_render(in: FsIn) -> @location(0) vec4f {
    let cam = camera();
    let ndc = vec2f(in.uv.x * 2.0 - 1.0, 1.0 - in.uv.y * 2.0);
    let fov = 0.62;
    let ray = normalize(cam.fwd + cam.right * ndc.x * G.aspect * fov + cam.up * ndc.y * fov);

    // water plane (one parallax refinement step)
    if (ray.y >= -1e-4) {
        return vec4f(sky(ray), 1.0);
    }
    var t = -cam.pos.y / ray.y;
    var hit = cam.pos + ray * t;
    if (abs(hit.x) <= 1.0 && abs(hit.z) <= 1.0) {
        let h = height_at(hit.xz);
        t = (h - cam.pos.y) / ray.y;
        hit = cam.pos + ray * t;
    }
    if (abs(hit.x) > 1.02 || abs(hit.z) > 1.02) {
        // pool deck: polished stone
        let deck_y = 0.06;
        let td = (deck_y - cam.pos.y) / ray.y;
        let dp = cam.pos + ray * td;
        let cell = floor(dp.xz * 2.0);
        let g = abs(fract(dp.xz * 2.0) - 0.5);
        let grout = smoothstep(0.48, 0.5, max(g.x, g.y));
        var stone = mix(vec3f(0.05, 0.048, 0.055) + hash21(cell) * 0.015, vec3f(0.01), grout);
        stone = stone * (0.6 + 0.4 * G.bg_top.rgb * 4.0);
        // light spilling out of the pool
        let edge_d = max(max(abs(dp.x), abs(dp.z)) - 1.0, 0.0);
        stone = stone + palette(0.3) * exp(-edge_d * 5.0) * 0.12 * (0.5 + G.energy);
        let refl = reflect(ray, vec3f(0.0, 1.0, 0.0));
        stone = stone + sky(refl) * 0.08;
        let fog = exp(-max(td - 1.5, 0.0) * 0.9);
        return vec4f(mix(sky(vec3f(ray.x, 0.02, ray.z)) * 0.6, stone, fog), 1.0);
    }
    if (abs(hit.x) > 1.0 || abs(hit.z) > 1.0) {
        // coping around the edge
        return vec4f(vec3f(0.5, 0.48, 0.45) * (0.4 + 0.6 * G.bg_top.rgb * 3.0) + G.accent.rgb * 0.05, 1.0);
    }

    let n = normal_at(hit.xz);
    let refl = reflect(ray, n);
    let refr = refract(ray, n, 1.0 / 1.333);
    let cosi = clamp(dot(-ray, n), 0.0, 1.0);
    let fresnel = mix(0.02, 1.0, pow(1.0 - cosi, 5.0)) * R.reflections;
    var col = mix(underwater(hit, refr), sky(refl), fresnel);
    // sparkle on wave crests
    col = col + palette(0.95) * pow(max(height_at(hit.xz), 0.0) * 6.0, 2.0) * 0.08 * G.treble;
    return vec4f(col, 1.0);
}
