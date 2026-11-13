// ---- Cymatica common prelude: no bindings, just helpers ----
const PI: f32 = 3.14159265358979;
const TAU: f32 = 6.28318530717959;

struct FsIn {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
};

// One triangle covering the screen; uv (0,0) = top-left.
@vertex
fn vs_fullscreen(@builtin(vertex_index) i: u32) -> FsIn {
    var out: FsIn;
    let x = f32((i << 1u) & 2u);
    let y = f32(i & 2u);
    out.pos = vec4f(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
    out.uv = vec2f(x, y);
    return out;
}

fn saturate(x: f32) -> f32 { return clamp(x, 0.0, 1.0); }
fn saturate3(x: vec3f) -> vec3f { return clamp(x, vec3f(0.0), vec3f(1.0)); }
fn luma(c: vec3f) -> f32 { return dot(c, vec3f(0.2126, 0.7152, 0.0722)); }
fn rot2(a: f32) -> mat2x2f { let c = cos(a); let s = sin(a); return mat2x2f(c, s, -s, c); }

// ---- hashing & noise ----
fn pcg(v: u32) -> u32 {
    let state = v * 747796405u + 2891336453u;
    let word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
fn hash11(p: f32) -> f32 { return f32(pcg(bitcast<u32>(p))) / 4294967295.0; }
fn hash_u(n: u32) -> f32 { return f32(pcg(n)) / 4294967295.0; }
fn hash21(p: vec2f) -> f32 {
    let q = vec2u(bitcast<u32>(p.x), bitcast<u32>(p.y));
    return f32(pcg(q.x ^ pcg(q.y))) / 4294967295.0;
}
fn hash22(p: vec2f) -> vec2f {
    let a = hash21(p);
    return vec2f(a, hash21(p + vec2f(a * 17.3, 3.1)));
}

// 3D simplex noise (Ashima / Ian McEwan, MIT).
fn mod289_3(x: vec3f) -> vec3f { return x - floor(x * (1.0 / 289.0)) * 289.0; }
fn mod289_4(x: vec4f) -> vec4f { return x - floor(x * (1.0 / 289.0)) * 289.0; }
fn permute4(x: vec4f) -> vec4f { return mod289_4(((x * 34.0) + 10.0) * x); }
fn snoise(v: vec3f) -> f32 {
    let C = vec2f(1.0 / 6.0, 1.0 / 3.0);
    let D = vec4f(0.0, 0.5, 1.0, 2.0);
    var i = floor(v + dot(v, C.yyy));
    let x0 = v - i + dot(i, C.xxx);
    let g = step(x0.yzx, x0.xyz);
    let l = 1.0 - g;
    let i1 = min(g.xyz, l.zxy);
    let i2 = max(g.xyz, l.zxy);
    let x1 = x0 - i1 + C.xxx;
    let x2 = x0 - i2 + C.yyy;
    let x3 = x0 - D.yyy;
    i = mod289_3(i);
    let p = permute4(permute4(permute4(i.z + vec4f(0.0, i1.z, i2.z, 1.0)) + i.y + vec4f(0.0, i1.y, i2.y, 1.0)) + i.x + vec4f(0.0, i1.x, i2.x, 1.0));
    let n_ = 0.142857142857;
    let ns = n_ * D.wyz - D.xzx;
    let j = p - 49.0 * floor(p * ns.z * ns.z);
    let x_ = floor(j * ns.z);
    let y_ = floor(j - 7.0 * x_);
    let x = x_ * ns.x + ns.yyyy;
    let y = y_ * ns.x + ns.yyyy;
    let h = 1.0 - abs(x) - abs(y);
    let b0 = vec4f(x.xy, y.xy);
    let b1 = vec4f(x.zw, y.zw);
    let s0 = floor(b0) * 2.0 + 1.0;
    let s1 = floor(b1) * 2.0 + 1.0;
    let sh = -step(h, vec4f(0.0));
    let a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    let a1 = b1.xzyw + s1.xzyw * sh.zzww;
    var p0 = vec3f(a0.xy, h.x);
    var p1 = vec3f(a0.zw, h.y);
    var p2 = vec3f(a1.xy, h.z);
    var p3 = vec3f(a1.zw, h.w);
    let norm = 1.79284291400159 - 0.85373472095314 * vec4f(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3));
    p0 = p0 * norm.x; p1 = p1 * norm.y; p2 = p2 * norm.z; p3 = p3 * norm.w;
    var m = max(0.5 - vec4f(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), vec4f(0.0));
    m = m * m;
    return 105.0 * dot(m * m, vec4f(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

fn fbm(p: vec3f, octaves: i32) -> f32 {
    var a = 0.5;
    var f = 0.0;
    var q = p;
    for (var i = 0; i < octaves; i = i + 1) {
        f = f + a * snoise(q);
        q = q * 2.03 + vec3f(1.7, 9.2, 3.1);
        a = a * 0.5;
    }
    return f;
}

// Divergence-free 3D curl noise (curl of a simplex-noise vector potential).
fn noise_potential(p: vec3f) -> vec3f {
    return vec3f(snoise(p), snoise(p + vec3f(31.416, -47.853, 12.793)), snoise(p + vec3f(-233.145, -113.408, -185.31)));
}
fn curl_noise(p: vec3f) -> vec3f {
    let e = 0.05;
    let dx = vec3f(e, 0.0, 0.0);
    let dy = vec3f(0.0, e, 0.0);
    let dz = vec3f(0.0, 0.0, e);
    let px0 = noise_potential(p - dx); let px1 = noise_potential(p + dx);
    let py0 = noise_potential(p - dy); let py1 = noise_potential(p + dy);
    let pz0 = noise_potential(p - dz); let pz1 = noise_potential(p + dz);
    let x = (py1.z - py0.z) - (pz1.y - pz0.y);
    let y = (pz1.x - pz0.x) - (px1.z - px0.z);
    let z = (px1.y - px0.y) - (py1.x - py0.x);
    return vec3f(x, y, z) / (2.0 * e);
}

// ---- colour ----
fn srgb_to_linear(c: vec3f) -> vec3f {
    let lo = c / 12.92;
    let hi = pow((c + 0.055) / 1.055, vec3f(2.4));
    return select(hi, lo, c <= vec3f(0.04045));
}
fn linear_to_srgb(c: vec3f) -> vec3f {
    let lo = c * 12.92;
    let hi = 1.055 * pow(max(c, vec3f(0.0)), vec3f(1.0 / 2.4)) - 0.055;
    return select(hi, lo, c <= vec3f(0.0031308));
}
fn hsv2rgb(c: vec3f) -> vec3f {
    let k = vec4f(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    let p = abs(fract(c.xxx + k.xyz) * 6.0 - k.www);
    return c.z * mix(k.xxx, clamp(p - k.xxx, vec3f(0.0), vec3f(1.0)), c.y);
}
// Blackbody colour (approximate, linear), temperature in Kelvin.
fn blackbody(t: f32) -> vec3f {
    let x = clamp(t, 1000.0, 40000.0) / 100.0;
    var c: vec3f;
    if (x <= 66.0) {
        c.r = 1.0;
        c.g = clamp(0.39008157 * log(x) - 0.63184144, 0.0, 1.0);
    } else {
        c.r = clamp(1.29293618 * pow(x - 60.0, -0.1332047), 0.0, 1.0);
        c.g = clamp(1.12989086 * pow(x - 60.0, -0.0755148), 0.0, 1.0);
    }
    if (x >= 66.0) { c.b = 1.0; }
    else if (x <= 19.0) { c.b = 0.0; }
    else { c.b = clamp(0.54320678 * log(x - 10.0) - 1.19625408, 0.0, 1.0); }
    return srgb_to_linear(c);
}
