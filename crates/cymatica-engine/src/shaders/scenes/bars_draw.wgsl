@group(1) @binding(0) var<uniform> B: BarsU;
@group(1) @binding(1) var<storage, read> state: array<vec4f>;
@group(1) @binding(2) var<storage, read> parts: array<Particle>;

fn bar_max_px() -> f32 {
    if (B.arrange == 2u) { return min(G.resolution.x, G.resolution.y) * 0.5 * (0.92 - B.radial_radius); }
    if (B.arrange == 1u) { return G.resolution.y * 0.46 * B.height; }
    return G.resolution.y * 0.74 * B.height;
}
fn floor_px() -> f32 { return G.resolution.y * select(0.06, 0.2, B.reflection > 0.001); }

// ---------- background ----------
@fragment
fn fs_bg(in: FsIn) -> @location(0) vec4f {
    let uv = in.uv;
    var c = background(uv.y) * (0.35 + 0.65 * B.bg);
    var p = (uv - vec2f(0.5, 1.0)) * vec2f(G.aspect, 1.0);
    if (B.arrange != 0u) { p = (uv - 0.5) * vec2f(G.aspect, 1.0); }
    let glow = exp(-dot(p, p) * 3.0) * (0.015 + 0.07 * G.bass) * B.bg;
    c = c + G.accent.rgb * glow;
    // floor line
    if (B.arrange == 0u) {
        let fy = 1.0 - floor_px() / G.resolution.y;
        let d = abs(uv.y - fy) * G.resolution.y;
        c = c + G.accent.rgb * exp(-d * 0.6) * 0.15 * B.reflection * (0.5 + G.energy);
        if (uv.y > fy) {
            c = c * 0.75;
        }
    }
    return vec4f(c * G.bg_alpha, 1.0);
}

// ---------- bars, reflections and caps ----------
struct BarOut {
    @builtin(position) pos: vec4f,
    @location(0) local: vec2f,     // pixels from the rect centre (x across, y along)
    @location(1) half_size: vec2f,
    @location(2) t: vec2f,         // x: palette position, y: along-bar height fraction at local.y = +half
    @location(3) intensity: f32,
    @location(4) kind: f32,        // 0 bar, 1 reflection, 2 cap
    @location(5) radius: f32,
};

fn to_clip(p: vec2f) -> vec4f {
    return vec4f(p / G.resolution * 2.0 - 1.0, 0.0, 1.0);
}

@vertex
fn vs_bar(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> BarOut {
    let n = max(B.count, 1u);
    let i = ii % n;
    let kind = ii / n; // 0 bar, 1 reflection, 2 cap, 3 cap reflection
    let s = state[i];
    let maxh = bar_max_px();
    let value = clamp(select(s.x, s.z, kind >= 2u), 0.0, 1.3);
    let fn_ = f32(n);

    var center: vec2f;
    var dir = vec2f(0.0, 1.0);
    var hw: f32;
    var hl: f32;
    let min_len = 2.0;
    if (B.arrange == 2u) {
        let a = (f32(i) + 0.5) / fn_ * TAU + G.time * B.spin + PI * 0.5;
        dir = vec2f(cos(a), sin(a));
        let r0 = min(G.resolution.x, G.resolution.y) * 0.5 * B.radial_radius;
        let circ = TAU * r0;
        hw = max(circ / fn_ * (1.0 - B.gap) * 0.5, 0.75);
        if (kind >= 2u) {
            hl = max(hw * 0.35, 1.5);
            center = G.resolution * 0.5 + dir * (r0 + value * maxh + hl * 2.0);
        } else {
            hl = max(value * maxh, min_len) * 0.5;
            center = G.resolution * 0.5 + dir * (r0 + hl);
        }
    } else {
        let margin = G.resolution.x * 0.04;
        let slot = (G.resolution.x - 2.0 * margin) / fn_;
        hw = max(slot * (1.0 - B.gap) * 0.5, 0.75);
        let x = margin + (f32(i) + 0.5) * slot;
        if (B.arrange == 1u) {
            let base = G.resolution.y * 0.5;
            if (kind >= 2u) {
                hl = max(hw * 0.3, 1.5);
                let side = select(1.0, -1.0, kind == 3u);
                center = vec2f(x, base + side * (value * maxh + hl * 2.5));
            } else {
                hl = max(value * maxh, min_len);
                center = vec2f(x, base);
            }
        } else {
            let fy = floor_px();
            if (kind >= 2u) {
                hl = max(hw * 0.3, 1.5);
                let y = fy + value * maxh + hl * 2.5;
                center = vec2f(x, select(y, 2.0 * fy - y, kind == 3u));
            } else {
                hl = max(value * maxh, min_len) * 0.5;
                center = vec2f(x, select(fy + hl, fy - hl, kind == 1u));
            }
        }
    }
    let pad = 1.5;
    let corner = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
    let c = corner[vi];
    let local = c * vec2f(hw + pad, hl + pad);
    let perp = vec2f(dir.y, -dir.x);
    let p = center + perp * local.x + dir * local.y;

    var out: BarOut;
    out.pos = to_clip(p);
    out.local = local;
    out.half_size = vec2f(hw, hl);
    var color_t = 0.0;
    switch B.color_by {
        case 1u: { color_t = (f32(i) + 0.5) / fn_; }
        case 2u: { color_t = fract((f32(i) + 0.5) / fn_ * 0.5 + G.time * 0.05); }
        default: { color_t = value; }
    }
    out.t = vec2f(color_t, value);
    out.intensity = B.glow * (0.5 + 0.55 * value * value);
    out.kind = f32(kind);
    if (kind == 1u || kind == 3u) {
        out.intensity = out.intensity * B.reflection * 0.55;
    }
    if (B.arrange == 1u && kind < 2u) {
        // mirrored bars span both directions from the centre
        out.half_size = vec2f(hw, hl);
    }
    out.radius = select(min(hw, hl) * B.roundness, 0.0, B.style == 0u);
    return out;
}

fn rounded_rect(p: vec2f, half_size: vec2f, r: f32) -> f32 {
    let q = abs(p) - half_size + r;
    return length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

@fragment
fn fs_bar(in: BarOut) -> @location(0) vec4f {
    let d = rounded_rect(in.local, in.half_size, in.radius);
    let aa = 1.0 - smoothstep(-0.75, 0.75, d);
    if (aa <= 0.0) { discard; }
    let along = in.local.y / max(in.half_size.y, 1e-3); // -1..1
    var t = in.t.x;
    let kind = u32(in.kind + 0.5);
    if (B.color_by == 0u && kind < 2u) {
        // cava's gradient: colour follows screen height
        var frac = (along * 0.5 + 0.5);
        if (B.arrange == 1u) { frac = abs(along); }
        if (kind == 1u) { frac = 1.0 - frac; }
        t = frac * in.t.y;
    }
    var col = palette(t) * in.intensity;
    if (kind == 2u || kind == 3u) {
        col = mix(col, G.accent.rgb * in.intensity, 0.5) * 1.4;
    }
    var alpha = aa;
    if (kind == 1u || kind == 3u) {
        // reflection fades with distance below the floor
        let fade = select(along * 0.5 + 0.5, 1.0 - (along * 0.5 + 0.5), kind == 1u);
        alpha = alpha * pow(clamp(1.0 - fade, 0.0, 1.0), 2.0);
    }
    // a little sheen across the bar
    if (B.style != 0u) {
        let across = in.local.x / max(in.half_size.x, 1e-3);
        col = col * (1.0 + 0.25 * (1.0 - across * across));
    }
    return vec4f(col * alpha, alpha);
}

// ---------- sparks ----------
struct PartOut {
    @builtin(position) pos: vec4f,
    @location(0) local: vec2f,
    @location(1) color: vec3f,
};

@vertex
fn vs_particle(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> PartOut {
    let p = parts[ii];
    var out: PartOut;
    if (p.life <= 0.0) {
        out.pos = vec4f(2.0, 2.0, 0.0, 1.0);
        return out;
    }
    let corner = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
    let c = corner[vi];
    let life = clamp(p.life / p.max_life, 0.0, 1.0);
    let size = p.size * (0.4 + 0.6 * life) + 1.0;
    // stretch along velocity for motion blur
    let speed = length(p.vel);
    let dir = select(vec2f(0.0, 1.0), p.vel / speed, speed > 1e-3);
    let perp = vec2f(dir.y, -dir.x);
    let stretch = 1.0 + min(speed * 0.012, 4.0);
    let world = p.pos + perp * c.x * size + dir * c.y * size * stretch;
    out.pos = to_clip(world);
    out.local = c;
    let flicker = 0.75 + 0.25 * hash_u(ii * 31u + G.frame);
    out.color = palette(p.color_t) * (1.2 + 2.2 * life) * life * flicker;
    return out;
}

@fragment
fn fs_particle(in: PartOut) -> @location(0) vec4f {
    let r2 = dot(in.local, in.local);
    if (r2 > 1.0) { discard; }
    let a = exp(-r2 * 3.5);
    return vec4f(in.color * a, 0.0);
}
