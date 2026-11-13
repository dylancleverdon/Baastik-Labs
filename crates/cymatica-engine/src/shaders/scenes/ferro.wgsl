// Ferrofluid: a black, mirror-glossy magnetic fluid in a dish. The surface is a dense mesh displaced
// by a dome plus Rosensweig spikes on a hex lattice; each spike's height comes from the music
// (spring physics on the CPU).

struct FerroU {
    view_proj: mat4x4f,
    eye: vec4f,
    lattice: vec4f,     // spacing, columns, rows, spike width
    blob: vec4f,        // radius, height, swell, sharpness
    grid: vec4u,        // mesh resolution, spike count, color mode, _
    reflect_tint: vec4f,
    gloss: f32,
    time: f32,
    rim: f32,
    _p: f32,
};

@group(1) @binding(0) var<uniform> F: FerroU;
@group(1) @binding(1) var<storage, read> spikes: array<f32>;

const SQRT3: f32 = 1.7320508;

// hex lattice: row j offset by half a column on odd rows
fn spike_pos(i: i32, j: i32) -> vec2f {
    let s = F.lattice.x;
    let cols = i32(F.lattice.y);
    let rows = i32(F.lattice.z);
    let x = (f32(i) - f32(cols - 1) * 0.5 + select(0.0, 0.5, (j & 1) == 1)) * s;
    let z = (f32(j) - f32(rows - 1) * 0.5) * s * SQRT3 * 0.5;
    return vec2f(x, z);
}

fn height(xz: vec2f) -> f32 {
    let r = length(xz);
    let R = F.blob.x;
    if (r >= R) { return 0.0; }
    let t = r / R;
    var h = F.blob.y * (1.0 + F.blob.z) * sqrt(max(1.0 - t * t, 0.0));
    // nearest lattice points
    let s = F.lattice.x;
    let cols = i32(F.lattice.y);
    let rows = i32(F.lattice.z);
    let jf = xz.y / (s * SQRT3 * 0.5) + f32(rows - 1) * 0.5;
    let j0 = i32(round(jf));
    var spike = 0.0;
    for (var dj = -1; dj <= 1; dj = dj + 1) {
        let j = j0 + dj;
        if (j < 0 || j >= rows) { continue; }
        let off = select(0.0, 0.5, (j & 1) == 1);
        let i0 = i32(round(xz.x / s + f32(cols - 1) * 0.5 - off));
        for (var di = -1; di <= 1; di = di + 1) {
            let i = i0 + di;
            if (i < 0 || i >= cols) { continue; }
            let c = spike_pos(i, j);
            let d = length(xz - c);
            let w = F.lattice.w;
            if (d < w) {
                let hs = spikes[u32(j * cols + i)];
                let k = 1.0 - d / w;
                spike = max(spike, hs * pow(k, F.blob.w) * smoothstep(1.0, 0.85, length(c) / R));
            }
        }
    }
    // spikes grow out of the dome, fading near the rim
    return h + spike * smoothstep(1.0, 0.7, t);
}

fn env(dir: vec3f) -> vec3f {
    let y = dir.y;
    var c = mix(G.bg_bottom.rgb * 0.6, G.bg_top.rgb * 1.2, smoothstep(-0.2, 0.8, y));
    // softboxes in theme colours
    let a = atan2(dir.z, dir.x);
    let box1 = smoothstep(0.75, 0.95, cos(a - F.time * 0.05)) * smoothstep(0.15, 0.35, y) * smoothstep(0.75, 0.55, y);
    let box2 = smoothstep(0.85, 0.97, cos(a + 2.2 - F.time * 0.05)) * smoothstep(0.05, 0.25, y) * smoothstep(0.6, 0.4, y);
    let box3 = smoothstep(0.9, 0.98, cos(a - 2.0)) * smoothstep(0.3, 0.45, y);
    c = c + palette(0.25) * box1 * 3.0 * F.reflect_tint.x;
    c = c + palette(0.75) * box2 * 2.5 * F.reflect_tint.x;
    c = c + vec3f(1.0) * box3 * 2.0;
    // overhead light
    c = c + vec3f(1.0) * pow(max(y, 0.0), 40.0) * 3.0;
    return c;
}

// ---------- floor & background ----------
@fragment
fn fs_room(in: FsIn) -> @location(0) vec4f {
    // a dark studio: soft gradient, a pool of light under the dish
    let p = (in.uv - vec2f(0.5, 0.62)) * vec2f(G.aspect, 1.0);
    var c = background(in.uv.y) * 0.18;
    c = c + G.accent.rgb * exp(-dot(p, p) * 5.0) * 0.05 * (0.4 + G.energy);
    return vec4f(c, 1.0);
}

struct MeshOut {
    @builtin(position) pos: vec4f,
    @location(0) world: vec3f,
    @location(1) normal: vec3f,
};

@vertex
fn vs_mesh(@builtin(vertex_index) vi: u32) -> MeshOut {
    let n = F.grid.x;
    let quad = vi / 6u;
    let corner = array<vec2u, 6>(vec2u(0u, 0u), vec2u(1u, 0u), vec2u(1u, 1u), vec2u(0u, 0u), vec2u(1u, 1u), vec2u(0u, 1u));
    let g = vec2u(quad % n, quad / n) + corner[vi % 6u];
    // dish area: a square slightly larger than the dish
    let extent = F.blob.x * 1.6;
    let xz = (vec2f(g) / f32(n) * 2.0 - 1.0) * extent;
    let h = height(xz);
    let e = extent * 2.0 / f32(n) * 0.5;
    let hx = height(xz + vec2f(e, 0.0)) - height(xz - vec2f(e, 0.0));
    let hz = height(xz + vec2f(0.0, e)) - height(xz - vec2f(0.0, e));
    var out: MeshOut;
    out.world = vec3f(xz.x, h, xz.y);
    out.normal = normalize(vec3f(-hx, 2.0 * e, -hz));
    out.pos = F.view_proj * vec4f(out.world, 1.0);
    return out;
}

@fragment
fn fs_mesh(in: MeshOut) -> @location(0) vec4f {
    let n = normalize(in.normal);
    let v = normalize(in.world - F.eye.xyz);
    let r = length(in.world.xz);
    let fluid = smoothstep(F.blob.x, F.blob.x * 0.97, r) * step(0.0005, in.world.y);
    let refl = env(reflect(v, n));
    let cosi = clamp(dot(-v, n), 0.0, 1.0);
    // ferrofluid: black, very glossy (F0 ~ 0.05)
    let fres = 0.05 + 0.95 * pow(1.0 - cosi, 5.0);
    var ferro = vec3f(0.006) + refl * mix(0.08, 1.0, fres) * F.gloss;
    // iridescent rim on the spikes when the music is loud
    ferro = ferro + palette(fract(in.world.y * 3.0 + F.time * 0.1)) * pow(1.0 - cosi, 3.0) * F.rim * (0.2 + G.energy);
    // the dish: dark brushed metal with a lit rim
    let dish_r = F.blob.x * 1.45;
    var dish = vec3f(0.018) + env(reflect(v, vec3f(0.0, 1.0, 0.0))) * 0.06;
    dish = dish + G.accent.rgb * exp(-abs(r - dish_r) * 40.0) * (0.3 + G.kick);
    if (r > dish_r + 0.015) { discard; }
    return vec4f(mix(dish, ferro, fluid), 1.0);
}
