// Liquid: 3D MLS-MPM fluid (moving least squares material point method).
// Particles carry position, velocity and an affine velocity field (APIC); each substep transfers
// mass/momentum to a grid (fixed-point atomics), adds pressure from an equation of state, applies
// forces and walls on the grid, and gathers velocities back.

struct SimU {
    grid: vec4<u32>,       // gx, gy, gz, particle count
    dt: f32,
    stiffness: f32,
    rest_density: f32,
    viscosity: f32,
    gravity: vec4f,        // xyz (tilted by the music)
    geyser: vec4f,         // x, z centre, radius, strength
    vortex: vec4f,         // strength, radius
    paddle: vec4f,         // x: paddle position, y: paddle velocity
    frame: u32,
    _p0: u32,
    _p1: u32,
    _p2: u32,
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

@group(1) @binding(0) var<uniform> S: SimU;
@group(1) @binding(1) var<storage, read_write> particles: array<Particle>;
@group(1) @binding(2) var<storage, read_write> cells: array<atomic<i32>>;   // 4 per cell: px py pz mass
@group(1) @binding(3) var<storage, read_write> grid_v: array<vec4f>;      // velocity after the grid update

const FP: f32 = 1000000.0;   // fixed-point scale for the atomics

fn encode(x: f32) -> i32 { return i32(clamp(x, -2000.0, 2000.0) * FP); }
fn decode(x: i32) -> f32 { return f32(x) / FP; }

fn cell_index(c: vec3<i32>) -> u32 {
    return u32(c.x) + u32(c.y) * S.grid.x + u32(c.z) * S.grid.x * S.grid.y;
}

fn in_grid(c: vec3<i32>) -> bool {
    return all(c >= vec3<i32>(0)) && all(c < vec3<i32>(S.grid.xyz));
}

// Quadratic B-spline weights for the 3 nodes around p (nodes at cell centres).
struct Kernel {
    base: vec3<i32>,
    w0: vec3f,
    w1: vec3f,
    w2: vec3f,
};

fn kernel(p: vec3f) -> Kernel {
    var k: Kernel;
    let cell = floor(p);
    let d = p - (cell + 0.5);
    k.base = vec3<i32>(cell) - vec3<i32>(1);
    k.w0 = 0.5 * (0.5 - d) * (0.5 - d);
    k.w1 = 0.75 - d * d;
    k.w2 = 0.5 * (0.5 + d) * (0.5 + d);
    return k;
}

fn weight(k: Kernel, o: vec3<u32>) -> f32 {
    let wx = select(select(k.w2.x, k.w1.x, o.x == 1u), k.w0.x, o.x == 0u);
    let wy = select(select(k.w2.y, k.w1.y, o.y == 1u), k.w0.y, o.y == 0u);
    let wz = select(select(k.w2.z, k.w1.z, o.z == 1u), k.w0.z, o.z == 0u);
    return wx * wy * wz;
}

@compute @workgroup_size(256)
fn cs_clear(@builtin(global_invocation_id) gid: vec3u) {
    let n = S.grid.x * S.grid.y * S.grid.z;
    if (gid.x >= n) { return; }
    atomicStore(&cells[gid.x * 4u + 0u], 0);
    atomicStore(&cells[gid.x * 4u + 1u], 0);
    atomicStore(&cells[gid.x * 4u + 2u], 0);
    atomicStore(&cells[gid.x * 4u + 3u], 0);
}

// P2G 1: mass and APIC momentum.
@compute @workgroup_size(256)
fn cs_p2g_mass(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= S.grid.w) { return; }
    let p = particles[gid.x];
    let k = kernel(p.pos);
    let C = mat3x3f(p.c0, p.c1, p.c2);
    for (var x = 0u; x < 3u; x = x + 1u) {
        for (var y = 0u; y < 3u; y = y + 1u) {
            for (var z = 0u; z < 3u; z = z + 1u) {
                let c = k.base + vec3<i32>(vec3u(x, y, z));
                if (!in_grid(c)) { continue; }
                let w = weight(k, vec3u(x, y, z));
                let dist = (vec3f(c) + 0.5) - p.pos;
                let q = C * dist;
                let mass = w;  // particle mass = 1
                let mom = mass * (p.vel + q);
                let i = cell_index(c) * 4u;
                atomicAdd(&cells[i + 0u], encode(mom.x));
                atomicAdd(&cells[i + 1u], encode(mom.y));
                atomicAdd(&cells[i + 2u], encode(mom.z));
                atomicAdd(&cells[i + 3u], encode(mass));
            }
        }
    }
}

// P2G 2: pressure (equation of state from the gathered density) + viscosity.
@compute @workgroup_size(256)
fn cs_p2g_stress(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= S.grid.w) { return; }
    let p = particles[gid.x];
    let k = kernel(p.pos);
    var density = 0.0;
    for (var x = 0u; x < 3u; x = x + 1u) {
        for (var y = 0u; y < 3u; y = y + 1u) {
            for (var z = 0u; z < 3u; z = z + 1u) {
                let c = k.base + vec3<i32>(vec3u(x, y, z));
                if (!in_grid(c)) { continue; }
                let w = weight(k, vec3u(x, y, z));
                density = density + w * decode(atomicLoad(&cells[cell_index(c) * 4u + 3u]));
            }
        }
    }
    let volume = 1.0 / max(density, 1e-3);
    let ratio = density / S.rest_density;
    let pressure = max(-0.1, S.stiffness * (ratio * ratio * ratio * ratio * ratio - 1.0));
    let C = mat3x3f(p.c0, p.c1, p.c2);
    var stress = mat3x3f(-pressure, 0.0, 0.0, 0.0, -pressure, 0.0, 0.0, 0.0, -pressure);
    stress = stress + S.viscosity * (C + transpose(C));
    let term = -volume * 4.0 * S.dt * stress;
    for (var x = 0u; x < 3u; x = x + 1u) {
        for (var y = 0u; y < 3u; y = y + 1u) {
            for (var z = 0u; z < 3u; z = z + 1u) {
                let c = k.base + vec3<i32>(vec3u(x, y, z));
                if (!in_grid(c)) { continue; }
                let w = weight(k, vec3u(x, y, z));
                let dist = (vec3f(c) + 0.5) - p.pos;
                let mom = w * (term * dist);
                let i = cell_index(c) * 4u;
                atomicAdd(&cells[i + 0u], encode(mom.x));
                atomicAdd(&cells[i + 1u], encode(mom.y));
                atomicAdd(&cells[i + 2u], encode(mom.z));
            }
        }
    }
}

// Grid: momentum → velocity, forces from the music, walls.
@compute @workgroup_size(256)
fn cs_grid(@builtin(global_invocation_id) gid: vec3u) {
    let n = S.grid.x * S.grid.y * S.grid.z;
    if (gid.x >= n) { return; }
    let i = gid.x;
    let mass = decode(atomicLoad(&cells[i * 4u + 3u]));
    if (mass <= 1e-6) {
        grid_v[i] = vec4f(0.0);
        return;
    }
    var v = vec3f(decode(atomicLoad(&cells[i * 4u + 0u])), decode(atomicLoad(&cells[i * 4u + 1u])), decode(atomicLoad(&cells[i * 4u + 2u]))) / mass;
    v = v + S.gravity.xyz * S.dt;

    let gx = i % S.grid.x;
    let gy = (i / S.grid.x) % S.grid.y;
    let gz = i / (S.grid.x * S.grid.y);
    let pos = vec3f(f32(gx), f32(gy), f32(gz)) + 0.5;

    // kick: a geyser from the "subwoofer" in the floor
    let dg = pos.xz - S.geyser.xy;
    if (S.geyser.w > 0.0 && dot(dg, dg) < S.geyser.z * S.geyser.z && pos.y < 7.0) {
        let falloff = 1.0 - sqrt(dot(dg, dg)) / S.geyser.z;
        v.y = v.y + S.geyser.w * falloff * S.dt;
    }
    // mids: a whirlpool around the vertical axis
    if (S.vortex.x != 0.0) {
        let centre = vec2f(f32(S.grid.x), f32(S.grid.z)) * 0.5;
        let r = pos.xz - centre;
        let dist = length(r) + 1e-3;
        let tang = vec2f(-r.y, r.x) / dist;
        let falloff = exp(-dist / S.vortex.y);
        v = v + vec3f(tang.x, 0.0, tang.y) * S.vortex.x * falloff * S.dt;
        v = v - vec3f(r.x, 0.0, r.y) / dist * S.vortex.x * 0.15 * falloff * S.dt;
    }
    // bass: a wave-machine paddle sweeping along x
    if (pos.x < S.paddle.x + 1.5 && S.paddle.y > 0.0) {
        v.x = max(v.x, S.paddle.y);
    }

    // walls (free slip)
    let lo = 2.0;
    let hi = vec3f(S.grid.xyz) - 2.0;
    if (pos.x < lo && v.x < 0.0) { v.x = 0.0; }
    if (pos.x > hi.x && v.x > 0.0) { v.x = 0.0; }
    if (pos.y < lo && v.y < 0.0) { v.y = 0.0; }
    if (pos.y > hi.y && v.y > 0.0) { v.y = 0.0; }
    if (pos.z < lo && v.z < 0.0) { v.z = 0.0; }
    if (pos.z > hi.z && v.z > 0.0) { v.z = 0.0; }
    grid_v[i] = vec4f(v, mass);
}

// G2P: gather velocity and the affine field, move particles.
@compute @workgroup_size(256)
fn cs_g2p(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= S.grid.w) { return; }
    var p = particles[gid.x];
    let k = kernel(p.pos);
    var v = vec3f(0.0);
    var B = mat3x3f(vec3f(0.0), vec3f(0.0), vec3f(0.0));
    for (var x = 0u; x < 3u; x = x + 1u) {
        for (var y = 0u; y < 3u; y = y + 1u) {
            for (var z = 0u; z < 3u; z = z + 1u) {
                let c = k.base + vec3<i32>(vec3u(x, y, z));
                if (!in_grid(c)) { continue; }
                let w = weight(k, vec3u(x, y, z));
                let dist = (vec3f(c) + 0.5) - p.pos;
                let gv = grid_v[cell_index(c)].xyz;
                let wv = w * gv;
                v = v + wv;
                B = B + mat3x3f(wv * dist.x, wv * dist.y, wv * dist.z);
            }
        }
    }
    let C = B * 4.0;
    p.c0 = C[0];
    p.c1 = C[1];
    p.c2 = C[2];
    // CFL safety: never move more than about a cell per step
    let speed = length(v);
    if (speed * S.dt > 0.9) { v = v * (0.9 / (speed * S.dt)); }
    p.vel = v;
    p.speed = speed;
    p.pos = clamp(p.pos + v * S.dt, vec3f(1.0), vec3f(S.grid.xyz) - 1.0);
    particles[gid.x] = p;
}
