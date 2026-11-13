@group(1) @binding(1) var<storage, read_write> grains: array<Grain>;

@compute @workgroup_size(256)
fn cs_grains(@builtin(global_invocation_id) gid: vec3u) {
    let i = gid.x;
    if (i >= C.count) { return; }
    var g = grains[i];
    let dt = clamp(G.dt, 0.0, 1.0 / 20.0);
    let a = abs(amp(g.pos));
    // vibration: random hops proportional to how much the plate moves here
    let s = pcg(i * 1664525u + C.seed);
    let rnd = vec2f(f32(s & 0xffffu), f32(s >> 16u)) / 65535.0 - 0.5;
    let shake = C.vibration * (0.25 + 1.6 * G.energy + 0.8 * G.kick);
    g.vel = g.vel * 0.82 + rnd * a * shake * 0.6;
    // drift down the gradient of |A|^2 toward the still lines
    let e = 0.002;
    let gx = abs(amp(g.pos + vec2f(e, 0.0))) - abs(amp(g.pos - vec2f(e, 0.0)));
    let gy = abs(amp(g.pos + vec2f(0.0, e))) - abs(amp(g.pos - vec2f(0.0, e)));
    g.vel = g.vel - vec2f(gx, gy) / (2.0 * e) * a * C.drift * 0.02;
    g.pos = g.pos + g.vel * dt;
    if (C.plate == 1u) {
        let q = g.pos - 0.5;
        let r = length(q);
        if (r > 0.49) { g.pos = 0.5 + q / r * 0.49; g.vel = g.vel * -0.3; }
    } else {
        if (g.pos.x < 0.005 || g.pos.x > 0.995) { g.vel.x = -g.vel.x * 0.3; }
        if (g.pos.y < 0.005 || g.pos.y > 0.995) { g.vel.y = -g.vel.y * 0.3; }
        g.pos = clamp(g.pos, vec2f(0.005), vec2f(0.995));
    }
    grains[i] = g;
}

