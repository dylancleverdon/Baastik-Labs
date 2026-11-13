@group(1) @binding(0) var<uniform> U: StarU;
@group(1) @binding(1) var<storage, read_write> stars: array<Star>;

@compute @workgroup_size(256)
fn cs_stars(@builtin(global_invocation_id) gid: vec3u) {
    let i = gid.x;
    if (i >= U.count) { return; }
    var s = stars[i];
    let dt = clamp(G.dt, 0.0, 1.0 / 20.0);
    var acc = vec3f(0.0);
    // softened gravity toward the moving attractors
    for (var k = 0u; k < U.n_attractors; k = k + 1u) {
        let a = U.attractors[k];
        let d = a.xyz - s.pos;
        let r2 = dot(d, d) + 0.6;
        acc = acc + d * (a.w / (r2 * sqrt(r2)));
    }
    acc = acc * U.gravity;
    // spin around the vertical axis (galaxy rotation)
    acc = acc + vec3f(-s.pos.z, 0.0, s.pos.x) * U.spin * 0.05;
    // turbulence (only every other frame per star to halve the noise cost)
    if (U.turbulence > 0.0 && ((i + G.frame) & 1u) == 0u) {
        acc = acc + curl_noise(s.pos * U.noise_scale + vec3f(0.0, U.time * 0.15, 0.0)) * U.turbulence * 2.0;
    }
    // supernova: a shockwave from the kick
    if (U.nova.w > 0.0) {
        let d = s.pos - U.nova.xyz;
        let r = length(d) + 0.3;
        acc = acc + d / r * U.nova.w * exp(-r * 0.12) * 30.0;
    }
    s.vel = (s.vel + acc * dt) * (1.0 - U.damping * dt);
    let speed = length(s.vel);
    if (speed > 25.0) { s.vel = s.vel * (25.0 / speed); }
    s.pos = s.pos + s.vel * dt;
    // strays respawn in a disc
    if (dot(s.pos, s.pos) > 60.0 * 60.0) {
        let h = pcg(i * 7u + U.seed);
        let a = f32(h & 0xffffu) / 65535.0 * TAU;
        let r = 2.0 + f32(h >> 16u) / 65535.0 * 12.0;
        s.pos = vec3f(cos(a) * r, 0.0, sin(a) * r);
        s.vel = vec3f(-sin(a), 0.0, cos(a)) * sqrt(8.0 / r) * 2.0;
    }
    stars[i] = s;
}
