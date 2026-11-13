// Ink: 2D incompressible Navier-Stokes ("stable fluids", Stam) with vorticity confinement.
// Velocity lives on a coarse grid; dye on a fine grid. Every pass is a fullscreen fragment pass.

struct InkU {
    sim_texel: vec2f,
    dye_texel: vec2f,
    dt: f32,
    vel_dissipation: f32,
    dye_dissipation: f32,
    curl_strength: f32,
    pressure_decay: f32,
    shading: f32,
    splat_count: u32,
    brightness: f32,
    swirl: f32,
    time: f32,
    aspect: f32,
    mode: u32,          // 0 = vector field pass, 1 = dye pass (splat pass)
};

struct Splat {
    pos_radius: vec4f,  // xy uv, z radius, w unused
    force_color: vec4f, // xy force (uv/s), z unused
    color: vec4f,
};

@group(1) @binding(0) var<uniform> U: InkU;
@group(1) @binding(1) var<storage, read> splats: array<Splat>;
@group(1) @binding(2) var tex_a: texture_2d<f32>;
@group(1) @binding(3) var tex_b: texture_2d<f32>;
@group(1) @binding(4) var samp: sampler;

fn s_a(uv: vec2f) -> vec4f { return textureSampleLevel(tex_a, samp, uv, 0.0); }
fn s_b(uv: vec2f) -> vec4f { return textureSampleLevel(tex_b, samp, uv, 0.0); }

// ---- splats: add velocity (mode 0) or dye (mode 1) ----
@fragment
fn fs_splat(in: FsIn) -> @location(0) vec4f {
    var base = s_a(in.uv);
    for (var i = 0u; i < U.splat_count; i = i + 1u) {
        let s = splats[i];
        var d = in.uv - s.pos_radius.xy;
        d.x = d.x * U.aspect;
        let g = exp(-dot(d, d) / max(s.pos_radius.z, 1e-6));
        if (U.mode == 0u) {
            base = vec4f(base.xy + s.force_color.xy * g, base.zw);
        } else {
            base = vec4f(base.rgb + s.color.rgb * g, 1.0);
        }
    }
    return base;
}

// ---- semi-Lagrangian advection: tex_a = velocity, tex_b = quantity ----
@fragment
fn fs_advect(in: FsIn) -> @location(0) vec4f {
    let vel = s_a(in.uv).xy;
    let back = in.uv - U.dt * vel * vec2f(1.0 / U.aspect, 1.0);
    let q = s_b(back);
    var diss = U.vel_dissipation;
    if (U.mode == 1u) { diss = U.dye_dissipation; }
    return q / (1.0 + diss * U.dt);
}

// ---- curl of velocity ----
@fragment
fn fs_curl(in: FsIn) -> @location(0) vec4f {
    let t = U.sim_texel;
    let l = s_a(in.uv - vec2f(t.x, 0.0)).y;
    let r = s_a(in.uv + vec2f(t.x, 0.0)).y;
    let b = s_a(in.uv + vec2f(0.0, t.y)).x;
    let tp = s_a(in.uv - vec2f(0.0, t.y)).x;
    // y points down: curl = dv/dx - du/dy = (r - l)/2 - (below - above)/2
    return vec4f(0.5 * ((r - l) - (b - tp)), 0.0, 0.0, 1.0);
}

// ---- vorticity confinement + a music-driven swirl: tex_a = velocity, tex_b = curl ----
@fragment
fn fs_vorticity(in: FsIn) -> @location(0) vec4f {
    let t = U.sim_texel;
    let l = s_b(in.uv - vec2f(t.x, 0.0)).x;
    let r = s_b(in.uv + vec2f(t.x, 0.0)).x;
    let b = s_b(in.uv + vec2f(0.0, t.y)).x;
    let tp = s_b(in.uv - vec2f(0.0, t.y)).x;
    let c = s_b(in.uv).x;
    // N = grad|curl| = (|r|-|l|, |below|-|above|)/2; confinement force = eps * (N.y * w, -N.x * w)
    var force = 0.5 * vec2f(abs(b) - abs(tp), abs(r) - abs(l));
    force = force / (length(force) + 1e-4);
    force = force * U.curl_strength * c * vec2f(1.0, -1.0);
    var vel = s_a(in.uv).xy + force * U.dt;
    // bass swirl around the centre
    var p = in.uv - 0.5;
    p.x = p.x * U.aspect;
    let swirl = vec2f(-p.y, p.x) * U.swirl * exp(-dot(p, p) * 2.0);
    vel = vel + swirl * U.dt;
    return vec4f(clamp(vel, vec2f(-1000.0), vec2f(1000.0)), 0.0, 1.0);
}

// ---- divergence: tex_a = velocity ----
@fragment
fn fs_divergence(in: FsIn) -> @location(0) vec4f {
    let t = U.sim_texel;
    var l = s_a(in.uv - vec2f(t.x, 0.0)).x;
    var r = s_a(in.uv + vec2f(t.x, 0.0)).x;
    var b = s_a(in.uv + vec2f(0.0, t.y)).y;
    var tp = s_a(in.uv - vec2f(0.0, t.y)).y;
    let c = s_a(in.uv).xy;
    // solid walls
    if (in.uv.x - t.x < 0.0) { l = -c.x; }
    if (in.uv.x + t.x > 1.0) { r = -c.x; }
    if (in.uv.y + t.y > 1.0) { b = -c.y; }
    if (in.uv.y - t.y < 0.0) { tp = -c.y; }
    return vec4f(0.5 * ((r - l) + (b - tp)), 0.0, 0.0, 1.0);
}

// ---- pressure: decay (tex_a = pressure) ----
@fragment
fn fs_pressure_decay(in: FsIn) -> @location(0) vec4f {
    return vec4f(s_a(in.uv).x * U.pressure_decay, 0.0, 0.0, 1.0);
}

// ---- Jacobi iteration: tex_a = pressure, tex_b = divergence ----
@fragment
fn fs_jacobi(in: FsIn) -> @location(0) vec4f {
    let t = U.sim_texel;
    let l = s_a(in.uv - vec2f(t.x, 0.0)).x;
    let r = s_a(in.uv + vec2f(t.x, 0.0)).x;
    let b = s_a(in.uv + vec2f(0.0, t.y)).x;
    let tp = s_a(in.uv - vec2f(0.0, t.y)).x;
    let div = s_b(in.uv).x;
    return vec4f((l + r + b + tp - div) * 0.25, 0.0, 0.0, 1.0);
}

// ---- subtract the pressure gradient: tex_a = pressure, tex_b = velocity ----
@fragment
fn fs_gradient(in: FsIn) -> @location(0) vec4f {
    let t = U.sim_texel;
    let l = s_a(in.uv - vec2f(t.x, 0.0)).x;
    let r = s_a(in.uv + vec2f(t.x, 0.0)).x;
    let b = s_a(in.uv + vec2f(0.0, t.y)).x;
    let tp = s_a(in.uv - vec2f(0.0, t.y)).x;
    var vel = s_b(in.uv).xy;
    vel = vel - vec2f(r - l, b - tp) * 0.5;
    return vec4f(vel, 0.0, 1.0);
}

// ---- display: tex_a = dye, tex_b = velocity ----
@fragment
fn fs_display(in: FsIn) -> @location(0) vec4f {
    let t = U.dye_texel * 2.0;
    let c = s_a(in.uv).rgb;
    // glossy ink: light from the dye's density gradient
    let dl = luma(s_a(in.uv - vec2f(t.x, 0.0)).rgb);
    let dr = luma(s_a(in.uv + vec2f(t.x, 0.0)).rgb);
    let db = luma(s_a(in.uv + vec2f(0.0, t.y)).rgb);
    let dt = luma(s_a(in.uv - vec2f(0.0, t.y)).rgb);
    let n = normalize(vec3f(dl - dr, db - dt, 0.15 + (1.0 - U.shading) * 2.0));
    let light = normalize(vec3f(-0.4, 0.6, 0.7));
    let diffuse = clamp(dot(n, light) + 0.5, 0.0, 1.4);
    let spec = pow(clamp(dot(reflect(-light, n), vec3f(0.0, 0.0, 1.0)), 0.0, 1.0), 24.0) * U.shading;
    var col = c * mix(1.0, diffuse, U.shading) + spec * luma(c) * 2.0;
    col = col * U.brightness;
    // background shows through thin ink
    let bg = background(in.uv.y) * 0.6;
    let density = clamp(luma(c) * 3.0, 0.0, 1.0);
    return vec4f(mix(bg, vec3f(0.0), density * 0.6) + col, 1.0);
}
