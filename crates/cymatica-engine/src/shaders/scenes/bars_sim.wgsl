@group(1) @binding(0) var<uniform> B: BarsU;
@group(1) @binding(1) var<storage, read_write> state: array<vec4f>;   // pos, vel, cap, cap_vel
@group(1) @binding(2) var<storage, read_write> parts: array<Particle>;
@group(1) @binding(3) var<storage, read_write> counter: array<atomic<u32>>;

fn bar_max_px() -> f32 {
    if (B.arrange == 2u) { return min(G.resolution.x, G.resolution.y) * 0.5 * (0.92 - B.radial_radius); }
    if (B.arrange == 1u) { return G.resolution.y * 0.46 * B.height; }
    return G.resolution.y * 0.74 * B.height;
}

// Pixel position of the top of bar i at height h (0..1), plus the outward direction.
fn bar_tip(i: u32, h: f32) -> vec4f {
    let n = f32(max(B.count, 1u));
    let maxh = bar_max_px();
    if (B.arrange == 2u) {
        let a = (f32(i) + 0.5) / n * TAU + G.time * B.spin + PI * 0.5;
        let dir = vec2f(cos(a), sin(a));
        let r0 = min(G.resolution.x, G.resolution.y) * 0.5 * B.radial_radius;
        return vec4f(G.resolution * 0.5 + dir * (r0 + h * maxh), dir);
    }
    let margin = G.resolution.x * 0.04;
    let slot = (G.resolution.x - 2.0 * margin) / n;
    let x = margin + (f32(i) + 0.5) * slot;
    if (B.arrange == 1u) {
        return vec4f(x, G.resolution.y * 0.5 + h * maxh, 0.0, 1.0);
    }
    let floor_y = G.resolution.y * select(0.06, 0.2, B.reflection > 0.001);
    return vec4f(x, floor_y + h * maxh, 0.0, 1.0);
}

fn spawn(i: u32, tip: vec4f, speed: f32, seed: u32) {
    let slot = atomicAdd(&counter[0], 1u) % B.max_particles;
    let r1 = hash_u(seed * 3u + 1u);
    let r2 = hash_u(seed * 7u + 5u);
    let r3 = hash_u(seed * 13u + 11u);
    var p: Particle;
    let side = vec2f(-tip.w, tip.z);
    let spread = (r1 - 0.5) * 0.9;
    let dir = normalize(tip.zw + side * spread);
    p.pos = tip.xy;
    p.vel = dir * (speed * (0.5 + r2)) + side * (r3 - 0.5) * 80.0;
    p.color_t = (f32(i) + 0.5) / f32(max(B.count, 1u));
    p.max_life = 0.8 + 1.4 * r3;
    p.life = p.max_life;
    p.size = 1.2 + 2.2 * r2 * r2;
    parts[slot] = p;
}

@compute @workgroup_size(64)
fn cs_bars(@builtin(global_invocation_id) gid: vec3u) {
    let i = gid.x;
    if (i >= B.count) { return; }
    let target_h = bars[i];
    var s = state[i];
    let dt = clamp(G.dt, 0.0, 1.0 / 20.0);
    if (B.style == 0u) {
        s.y = (target_h - s.x) / max(dt, 1e-4);
        s.x = target_h;
    } else {
        // soft-body bar: a damped spring toward cava's value, coupled to its neighbours
        let k = mix(90.0, 900.0, B.spring);
        let c = mix(26.0, 3.0, B.wobble);
        let left = state[max(i, 1u) - 1u].x;
        let right = state[min(i + 1u, B.count - 1u)].x;
        let steps = 4;
        let h = dt / f32(steps);
        for (var k2 = 0; k2 < steps; k2 = k2 + 1) {
            let acc = k * (target_h - s.x) - c * s.y + 60.0 * B.wobble * ((left + right) * 0.5 - s.x);
            s.y = s.y + acc * h;
            s.x = s.x + s.y * h;
        }
        s.x = clamp(s.x, -0.05, 1.3);
    }
    // falling caps
    if (B.caps != 0u) {
        let g = mix(0.6, 6.0, B.cap_gravity);
        s.w = s.w - g * dt;
        s.z = s.z + s.w * dt;
        if (s.z < s.x) {
            s.z = s.x;
            // jelly bars toss their caps a little; classic caps just ride the bar
            s.w = select(clamp(s.y * 0.35, 0.0, 1.2), 0.0, B.style == 0u);
        }
    } else {
        s.z = s.x;
        s.w = 0.0;
    }
    state[i] = s;

    // sparks fly off bars that jump up, and off every bar on a kick
    if (B.style == 2u && B.sparks > 0.0) {
        let maxh = bar_max_px();
        let seed = pcg(i * 9781u + G.frame * 6271u);
        let rise = max(s.y, 0.0);
        var expected = B.sparks * (max(rise - 0.6, 0.0) * 0.35 + B.burst * target_h * 1.2) * 60.0 * dt;
        var k3 = 0u;
        loop {
            if (expected <= 0.0 || k3 >= 3u) { break; }
            if (hash_u(seed + k3 * 31u) < expected) {
                let tip = bar_tip(i, s.x);
                spawn(i, tip, maxh * (0.25 + 0.2 * min(rise, 2.0) + 0.5 * B.burst), seed + k3 * 97u);
            }
            expected = expected - 1.0;
            k3 = k3 + 1u;
        }
    }
}

@compute @workgroup_size(256)
fn cs_particles(@builtin(global_invocation_id) gid: vec3u) {
    let i = gid.x;
    if (i >= B.max_particles) { return; }
    var p = parts[i];
    if (p.life <= 0.0) { return; }
    let dt = clamp(G.dt, 0.0, 1.0 / 20.0);
    var gravity = vec2f(0.0, -G.resolution.y * mix(0.4, 3.0, B.spark_gravity));
    if (B.arrange == 2u) {
        // radial: pull back toward the ring
        gravity = normalize(G.resolution * 0.5 - p.pos) * G.resolution.y * mix(0.1, 1.2, B.spark_gravity);
    }
    p.vel = p.vel + gravity * dt;
    p.vel = p.vel * (1.0 - 0.6 * dt);
    p.pos = p.pos + p.vel * dt;
    let floor_y = G.resolution.y * select(0.06, 0.2, B.reflection > 0.001);
    if (B.arrange == 0u && p.pos.y < floor_y) {
        p.pos.y = floor_y + (floor_y - p.pos.y) * 0.5;
        p.vel.y = abs(p.vel.y) * 0.45;
        p.vel.x = p.vel.x * 0.8;
    }
    p.life = p.life - dt;
    parts[i] = p;
}
