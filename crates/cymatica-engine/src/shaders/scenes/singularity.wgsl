// Singularity: a Schwarzschild black hole. Each pixel's light ray is integrated along its bent
// path (photon geodesic: a = -1.5 h^2 x / r^5 with r_s = 1), crossing a turbulent accretion disk
// (Keplerian rotation, Doppler beaming, gravitational redshift) and finally a lensed starfield.

struct BhU {
    cam_pos: vec4f,
    cam_fwd: vec4f,
    cam_right: vec4f,
    cam_up: vec4f,
    disk_in: f32,
    disk_out: f32,
    brightness: f32,
    turbulence: f32,
    doppler: f32,
    jets: f32,
    stars: f32,
    steps: u32,
    color_mode: u32,   // 0 physical (blackbody), 1 theme
    shock: f32,        // radius of the kick shockwave
    shock_amp: f32,
    disk_time: f32,
    tan_half_fov: f32,
    heat: f32,
    _p0: f32,
    _p1: f32,
};

@group(1) @binding(0) var<uniform> B: BhU;

fn starfield(d: vec3f) -> vec3f {
    // stars on a sphere: jittered cells in a cube-mapped grid
    var col = vec3f(0.0);
    let scales = array<f32, 3>(60.0, 140.0, 300.0);
    for (var k = 0; k < 3; k = k + 1) {
        let s = scales[k];
        let p = d * s;
        let cell = floor(p);
        let h = hash22(cell.xy + cell.z * 17.31 + f32(k) * 3.7);
        let h2 = hash21(cell.yz + cell.x * 7.13 + f32(k));
        let center = cell + vec3f(h, h2) ;
        let dist = length(p - center);
        let bright = pow(h2, 18.0) * (3.0 - f32(k)) * 1.5;
        let tint = mix(vec3f(0.7, 0.8, 1.0), vec3f(1.0, 0.85, 0.7), h.x);
        col = col + tint * bright * exp(-dist * dist * 40.0);
    }
    // nebula in theme colours
    let n = fbm(d * 2.5, 4) * 0.5 + 0.5;
    col = col + palette(n) * pow(n, 4.0) * 0.12;
    return col * B.stars;
}

fn disk_color(p: vec3f, ray_dir: vec3f) -> vec4f {
    let r = length(p.xz);
    if (r < B.disk_in || r > B.disk_out) { return vec4f(0.0); }
    // temperature profile (Novikov-Thorne-like)
    let x = B.disk_in / r;
    let temp = pow(x, 0.75) * pow(max(1.0 - sqrt(x), 0.0), 0.25) * 2.4;
    // turbulence that rotates with the Keplerian angular velocity
    let ang = atan2(p.z, p.x);
    let omega = pow(r, -1.5) * 2.2;
    let a2 = ang + B.disk_time * omega;
    // sheared turbulence: stretched along the orbit, fine across it
    let streak = fbm(vec3f(r * 2.2, cos(a2) * 1.6, sin(a2) * 1.6), 4) * 0.5 + 0.5;
    var dens = mix(1.0, smoothstep(0.25, 0.85, streak) * 1.6, B.turbulence);
    // hot spots on the treble
    let spots = smoothstep(0.8, 0.95, fbm(vec3f(r * 5.0, cos(a2) * 3.0, sin(a2) * 3.0 + 7.0), 3) * 0.5 + 0.5);
    dens = dens + spots * G.treble * 2.0;
    // shockwave ring from the kick
    dens = dens * (1.0 + B.shock_amp * exp(-pow((r - B.shock) * 1.4, 2.0)) * 3.0);
    // Doppler beaming: the disk orbits counter-clockwise (seen from +y)
    let vel = normalize(vec3f(-p.z, 0.0, p.x)) * min(sqrt(0.5 / max(r - 1.0, 0.2)), 0.7);
    let beta = length(vel);
    let gamma = 1.0 / sqrt(1.0 - beta * beta);
    let cosv = dot(normalize(vel), -ray_dir);
    let dop = 1.0 / (gamma * (1.0 - beta * cosv));
    let beaming = mix(1.0, pow(dop, 3.0), B.doppler);
    let redshift = sqrt(max(1.0 - 1.0 / r, 0.05));
    let intensity = temp * dens * beaming * redshift * B.brightness * (0.6 + B.heat) * 0.22;
    var col: vec3f;
    if (B.color_mode == 0u) {
        col = blackbody(1400.0 + 4200.0 * temp * mix(1.0, dop, B.doppler * 0.6)) * intensity;
    } else {
        col = palette(clamp(temp * mix(1.0, dop, B.doppler * 0.4) * 0.9, 0.0, 1.0)) * intensity * 1.4;
    }
    let alpha = clamp(dens * 0.75 * smoothstep(B.disk_out, B.disk_out * 0.75, r) * smoothstep(B.disk_in * 0.95, B.disk_in * 1.15, r), 0.0, 0.95);
    return vec4f(col, alpha);
}

@fragment
fn fs_bh(in: FsIn) -> @location(0) vec4f {
    let ndc = vec2f(in.uv.x * 2.0 - 1.0, 1.0 - in.uv.y * 2.0);
    var dir = normalize(B.cam_fwd.xyz + B.cam_right.xyz * ndc.x * G.aspect * B.tan_half_fov + B.cam_up.xyz * ndc.y * B.tan_half_fov);
    var pos = B.cam_pos.xyz;
    let h = cross(pos, dir);
    let h2 = dot(h, h);
    var col = vec3f(0.0);
    var trans = 1.0;
    var captured = false;
    var closest = 1e9;
    for (var i = 0u; i < B.steps; i = i + 1u) {
        let r2 = dot(pos, pos);
        let r = sqrt(r2);
        closest = min(closest, r);
        if (r < 1.0) { captured = true; break; }
        if (r > 80.0 && dot(pos, dir) > 0.0) { break; }
        let step = clamp((r - 0.9) * 0.09, 0.012, 1.6);
        // velocity Verlet on the photon geodesic
        let acc = -1.5 * h2 * pos / (r2 * r2 * r);
        let next = pos + dir * step + 0.5 * acc * step * step;
        let r_next = length(next);
        let acc2 = -1.5 * h2 * next / pow(r_next, 5.0);
        dir = normalize(dir + 0.5 * (acc + acc2) * step);
        // disk crossing
        if (pos.y * next.y < 0.0) {
            let t = pos.y / (pos.y - next.y);
            let hit = mix(pos, next, t);
            let d = disk_color(hit, dir);
            col = col + trans * d.rgb * d.a;
            trans = trans * (1.0 - d.a);
        }
        // relativistic jets along the spin axis
        if (B.jets > 0.001) {
            let axis_d = length(next.xz);
            let ay = abs(next.y);
            if (ay > 1.3 && axis_d < 1.6) {
                let w = exp(-axis_d * axis_d * 3.0) * exp(-ay * 0.08) * smoothstep(1.3, 3.0, ay);
                let jc = mix(vec3f(0.6, 0.75, 1.0), palette(0.85), 0.5);
                col = col + trans * jc * w * B.jets * step * 1.5;
            }
        }
        pos = next;
        if (trans < 0.01) { break; }
    }
    if (!captured) {
        col = col + trans * starfield(dir);
    }
    // photon ring shimmer on the beat
    col = col + palette(0.7) * exp(-pow((closest - 1.5) * 9.0, 2.0)) * 0.15 * G.beat * trans;
    return vec4f(col, 1.0);
}
