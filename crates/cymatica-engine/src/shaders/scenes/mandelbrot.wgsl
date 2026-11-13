// ---- Mandelbrot: perturbation with rebasing around a high-precision reference orbit ----

struct MandelU {
    offset: vec2f,      // view centre - reference point
    scale: f32,         // half view height in the complex plane
    angle: f32,
    max_iter: u32,
    orbit_len: u32,
    bands: f32,
    color_offset: f32,
    relief: f32,
    glow: f32,
    pixel: f32,         // complex-plane size of a pixel
    light: f32,         // light direction (radians)
    ref_c: vec2f,       // the reference point itself
    shallow: f32,       // 1 when zoomed out enough for the closed-form interior tests
    p0: f32,
};

@group(1) @binding(0) var<uniform> M: MandelU;
@group(1) @binding(1) var<storage, read> orbit: array<vec2f>;
// the lower-resolution picture, for the upscale pass
@group(2) @binding(0) var low_tex: texture_2d<f32>;
@group(2) @binding(1) var low_samp: sampler;

@fragment
fn fs_up(in: FsIn) -> @location(0) vec4f {
    return vec4f(textureSampleLevel(low_tex, low_samp, in.uv, 0.0).rgb, 1.0);
}

// Inside the main cardioid or the period-2 bulb (where most of the set's area is when zoomed out).
fn in_main_bulbs(c: vec2f) -> bool {
    let x = c.x - 0.25;
    let q = x * x + c.y * c.y;
    let cardioid = q * (q + x) < 0.25 * c.y * c.y;
    let bulb = (c.x + 1.0) * (c.x + 1.0) + c.y * c.y < 0.0625;
    return cardioid || bulb;
}

fn cmul(a: vec2f, b: vec2f) -> vec2f { return vec2f(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }

@fragment
fn fs_mandel(in: FsIn) -> @location(0) vec4f {
    let p = (in.uv - 0.5) * vec2f(G.aspect, -1.0) * 2.0;
    let dc = M.offset + rot2(M.angle) * p * M.scale;
    var dz = vec2f(0.0);
    var z = vec2f(0.0);
    // derivative dz/dc of the full orbit, for the relief lighting and the distance to the set
    var der = vec2f(0.0);
    var m = 0u;
    var i = 0u;
    var escaped = false;
    var trap = 1e9;
    // points inside the set never escape and would run the whole budget: catch them early,
    // in closed form near the top, and by noticing their orbit repeat (Brent) everywhere
    var inside = M.shallow > 0.5 && in_main_bulbs(M.ref_c + dc);
    var saved = vec2f(1e30);
    var check_at = 16u;
    // how many times the derivative was scaled down by 1e15 (to stay in f32 range)
    var der_scale = 0.0;
    loop {
        if (i >= M.max_iter || inside) { break; }
        let zr = orbit[m];
        der = 2.0 * cmul(zr + dz, der) + vec2f(1.0, 0.0);
        dz = 2.0 * cmul(zr, dz) + cmul(dz, dz) + dc;
        m = m + 1u;
        z = orbit[m] + dz;
        let r2 = dot(z, z);
        trap = min(trap, r2);
        if (r2 > 1e4) {
            escaped = true;
            break;
        }
        let ds = z - saved;
        if (dot(ds, ds) < 1e-12 * max(r2, 1e-6)) {
            inside = true;
            break;
        }
        if (i == check_at) {
            saved = z;
            check_at = check_at * 2u;
        }
        // rebase onto the start of the orbit when the pixel's own value is the smaller one,
        // or when the reference orbit runs out
        if (r2 < dot(dz, dz) || m + 1u >= M.orbit_len) {
            dz = z;
            m = 0u;
        }
        // only the derivative's direction and relative size matter: keep it in f32 range
        let dl = dot(der, der);
        if (dl > 1e30) {
            der = der * 1e-15;
            der_scale = der_scale + 1.0;
        }
        i = i + 1u;
    }

    var col: vec3f;
    if (escaped) {
        let r = length(z);
        let mu = f32(i) + 1.0 - log2(max(log2(r), 1e-6));
        let t = mu * M.bands + M.color_offset;
        col = palette_cycle(t) * (0.55 + 0.45 * sin(t * TAU * 2.0 + 1.0) * 0.5 + 0.25);
        // relief: the normal from z / (dz/dc), lit from one side
        let u = z / max(length(z), 1e-20);
        let dv = der / max(length(der), 1e-30);
        let n = vec2f(u.x * dv.x + u.y * dv.y, u.y * dv.x - u.x * dv.y);
        let l = vec2f(cos(M.light), sin(M.light));
        let shade = clamp(0.5 + 0.5 * dot(n, l), 0.0, 1.0);
        col = col * mix(1.0, 0.25 + 1.1 * shade, M.relief);
        // distance to the set, in pixels (in logs: the derivative was scaled to stay in range)
        let log_de = log(r * log(r)) - log(max(length(der), 1e-30)) - der_scale * 34.538776 - log(max(M.pixel, 1e-38));
        let de = exp(clamp(log_de, -30.0, 30.0));
        let edge_col = mix(palette(fract(M.color_offset + 0.5)), vec3f(1.0), 0.3);
        // glow along the edge; and within a few pixels of dense filaments, where the bands would
        // change faster than the pixels can show (just noise), the dim glow instead of the bands
        col = col + edge_col * exp(-de * 0.6) * M.glow * 0.8;
        // (coloured by the distance itself, which changes smoothly where the bands can't)
        let dense = palette_cycle(log2(max(de, 1e-6)) * 0.12 + M.color_offset) * (0.1 + 0.25 * M.glow);
        col = mix(col, dense, smoothstep(5.0, 0.4, de) * 0.9);
    } else {
        // inside the set: dark, with a faint colour from how close the orbit came to 0
        col = palette(fract(sqrt(trap) * 0.8 + M.color_offset)) * 0.015 * (0.4 + G.bass);
    }
    return vec4f(col * (0.8 + 0.4 * G.energy), 1.0);
}
