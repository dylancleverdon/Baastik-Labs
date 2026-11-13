// ---- post-processing: feedback trails, bloom, composite ----

struct PostU {
    resolution: vec2f,
    texel: vec2f,
    bloom: f32,
    threshold: f32,
    knee: f32,
    radius: f32,
    exposure: f32,
    saturation: f32,
    contrast: f32,
    hue_shift: f32,
    tonemap: u32,
    output_mode: u32,
    headroom: f32,
    chromatic: f32,
    zoom: f32,
    kaleido: f32,
    kaleido_spin: f32,
    vignette: f32,
    grain: f32,
    scanlines: f32,
    time: f32,
    trails: f32,
    trail_zoom: f32,
    trail_rotate: f32,
    note_tint: f32,
    frame: u32,
    note_color: vec4f,
};

@group(0) @binding(0) var<uniform> U: PostU;
@group(0) @binding(1) var src: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
@group(0) @binding(3) var aux: texture_2d<f32>;

// ---------- feedback trails: scene + warped previous frame ----------
@fragment
fn fs_trails(in: FsIn) -> @location(0) vec4f {
    let scene = vec4f(safe(textureSampleLevel(src, samp, in.uv, 0.0).rgb), 1.0);
    var p = in.uv - 0.5;
    p.x = p.x * U.resolution.x / U.resolution.y;
    p = rot2(U.trail_rotate) * p * (1.0 - U.trail_zoom);
    p.x = p.x * U.resolution.y / U.resolution.x;
    let prev = textureSampleLevel(aux, samp, p + 0.5, 0.0);
    // persistence (like phosphor): never brighter than the scene or the decayed past
    return vec4f(max(scene.rgb, prev.rgb * U.trails), 1.0);
}

// Non-finite or negative pixels never reach the glow or the screen (GPUs clamp NaN to a bound).
fn safe(c: vec3f) -> vec3f { return clamp(c, vec3f(0.0), vec3f(64000.0)); }

// ---------- bloom ----------
fn karis(c: vec3f) -> f32 { return 1.0 / (1.0 + luma(c)); }

fn prefilter(c: vec3f) -> vec3f {
    let br = max(c.r, max(c.g, c.b));
    var rq = clamp(br - U.threshold + U.knee, 0.0, 2.0 * U.knee);
    rq = rq * rq / (4.0 * U.knee + 1e-5);
    let w = max(rq, br - U.threshold) / max(br, 1e-5);
    return c * w;
}

// 13-tap downsample (Jimenez, "Next Generation Post Processing in Call of Duty: AW").
fn down13(uv: vec2f, first: bool) -> vec3f {
    let t = U.texel;
    let a = textureSampleLevel(src, samp, uv + t * vec2f(-2.0, -2.0), 0.0).rgb;
    let b = textureSampleLevel(src, samp, uv + t * vec2f(0.0, -2.0), 0.0).rgb;
    let c = textureSampleLevel(src, samp, uv + t * vec2f(2.0, -2.0), 0.0).rgb;
    let d = textureSampleLevel(src, samp, uv + t * vec2f(-2.0, 0.0), 0.0).rgb;
    let e = textureSampleLevel(src, samp, uv, 0.0).rgb;
    let f = textureSampleLevel(src, samp, uv + t * vec2f(2.0, 0.0), 0.0).rgb;
    let g = textureSampleLevel(src, samp, uv + t * vec2f(-2.0, 2.0), 0.0).rgb;
    let h = textureSampleLevel(src, samp, uv + t * vec2f(0.0, 2.0), 0.0).rgb;
    let i = textureSampleLevel(src, samp, uv + t * vec2f(2.0, 2.0), 0.0).rgb;
    let j = textureSampleLevel(src, samp, uv + t * vec2f(-1.0, -1.0), 0.0).rgb;
    let k = textureSampleLevel(src, samp, uv + t * vec2f(1.0, -1.0), 0.0).rgb;
    let l = textureSampleLevel(src, samp, uv + t * vec2f(-1.0, 1.0), 0.0).rgb;
    let m = textureSampleLevel(src, samp, uv + t * vec2f(1.0, 1.0), 0.0).rgb;
    if (first) {
        let a = safe(a); let b = safe(b); let c = safe(c); let d = safe(d); let e = safe(e);
        let f = safe(f); let g = safe(g); let h = safe(h); let i = safe(i); let j = safe(j);
        let k = safe(k); let l = safe(l); let m = safe(m);
        // Karis average per 2x2 group to kill fireflies, then threshold.
        var g0 = (j + k + l + m) * 0.25; var g1 = (a + b + d + e) * 0.25; var g2 = (b + c + e + f) * 0.25;
        var g3 = (d + e + g + h) * 0.25; var g4 = (e + f + h + i) * 0.25;
        let w0 = karis(g0); let w1 = karis(g1); let w2 = karis(g2); let w3 = karis(g3); let w4 = karis(g4);
        let sum = g0 * w0 * 0.5 + (g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) * 0.125;
        let wsum = w0 * 0.5 + (w1 + w2 + w3 + w4) * 0.125;
        return prefilter(sum / wsum);
    }
    var o = e * 0.125;
    o = o + (a + c + g + i) * 0.03125;
    o = o + (b + d + f + h) * 0.0625;
    o = o + (j + k + l + m) * 0.125;
    return o;
}

@fragment
fn fs_down_first(in: FsIn) -> @location(0) vec4f {
    return vec4f(min(down13(in.uv, true), vec3f(64000.0)), 1.0);
}

@fragment
fn fs_down(in: FsIn) -> @location(0) vec4f {
    return vec4f(down13(in.uv, false), 1.0);
}

// 9-tap tent upsample, blended additively onto the next larger mip.
@fragment
fn fs_up(in: FsIn) -> @location(0) vec4f {
    let t = U.texel * (0.5 + U.radius);
    var s = textureSampleLevel(src, samp, in.uv, 0.0).rgb * 4.0;
    s = s + (textureSampleLevel(src, samp, in.uv + vec2f(-t.x, 0.0), 0.0).rgb
        + textureSampleLevel(src, samp, in.uv + vec2f(t.x, 0.0), 0.0).rgb
        + textureSampleLevel(src, samp, in.uv + vec2f(0.0, -t.y), 0.0).rgb
        + textureSampleLevel(src, samp, in.uv + vec2f(0.0, t.y), 0.0).rgb) * 2.0;
    s = s + textureSampleLevel(src, samp, in.uv + vec2f(-t.x, -t.y), 0.0).rgb
        + textureSampleLevel(src, samp, in.uv + vec2f(t.x, -t.y), 0.0).rgb
        + textureSampleLevel(src, samp, in.uv + vec2f(-t.x, t.y), 0.0).rgb
        + textureSampleLevel(src, samp, in.uv + vec2f(t.x, t.y), 0.0).rgb;
    return vec4f(s / 16.0 * (0.6 + 0.4 * U.radius), 1.0);
}

// ---------- tone mapping ----------
fn agx_contrast(x: vec3f) -> vec3f {
    let x2 = x * x;
    let x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

// Minimal AgX (Benjamin Wrensch), returns linear.
fn tonemap_agx(c: vec3f) -> vec3f {
    let m = mat3x3f(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                    0.0784335999999992, 0.878468636469772, 0.0784336,
                    0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    let mi = mat3x3f(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                     -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                     -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    let min_ev = -12.47393;
    let max_ev = 4.026069;
    var v = m * c;
    v = clamp(log2(max(v, vec3f(1e-10))), vec3f(min_ev), vec3f(max_ev));
    v = (v - min_ev) / (max_ev - min_ev);
    v = agx_contrast(v);
    // punchy look
    let l = luma(v);
    v = l + (v - l) * 1.25;
    v = mi * v;
    return pow(max(v, vec3f(0.0)), vec3f(2.2));
}

fn tonemap_aces(x: vec3f) -> vec3f {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), vec3f(0.0), vec3f(1.0));
}

fn tonemap(c: vec3f) -> vec3f {
    switch U.tonemap {
        case 0u: { return tonemap_agx(c); }
        case 1u: { return tonemap_aces(c); }
        case 2u: { return c / (1.0 + luma(c)); }
        default: { return clamp(c, vec3f(0.0), vec3f(1.0)); }
    }
}

// Extended range for EDR displays: SDR-like up to 1, soft shoulder up to the headroom.
fn tonemap_edr(c: vec3f) -> vec3f {
    let sdr = tonemap(c);
    let h = max(U.headroom, 1.0);
    let l = luma(c);
    let over = max(l - 0.85, 0.0);
    let boost = 1.0 + (h - 1.0) * (1.0 - exp(-over * 1.2));
    return sdr * boost;
}

fn hue_rotate(c: vec3f, a: f32) -> vec3f {
    let k = vec3f(0.57735);
    let ca = cos(a);
    return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}

fn kaleido_uv(uv: vec2f) -> vec2f {
    if (U.kaleido < 1.5) { return uv; }
    var p = uv - 0.5;
    p.x = p.x * U.resolution.x / U.resolution.y;
    let r = length(p);
    var a = atan2(p.y, p.x) + U.kaleido_spin;
    let seg = TAU / U.kaleido;
    a = abs(fract(a / seg + 0.5) - 0.5) * seg;
    p = vec2f(cos(a), sin(a)) * r;
    p.x = p.x * U.resolution.y / U.resolution.x;
    return p + 0.5;
}

@fragment
fn fs_composite(in: FsIn) -> @location(0) vec4f {
    // zoom pulse + kaleidoscope
    var uv = (in.uv - 0.5) / U.zoom + 0.5;
    uv = kaleido_uv(uv);

    // chromatic aberration
    let dir = (uv - 0.5);
    let ca = U.chromatic * 0.012;
    var col: vec3f;
    col.r = textureSampleLevel(src, samp, uv + dir * ca, 0.0).r;
    col.g = textureSampleLevel(src, samp, uv, 0.0).g;
    col.b = textureSampleLevel(src, samp, uv - dir * ca, 0.0).b;
    col = safe(col);
    let bloom = safe(textureSampleLevel(aux, samp, uv, 0.0).rgb);
    col = col + bloom * U.bloom;

    // synesthesia tint
    let l0 = luma(col);
    col = mix(col, U.note_color.rgb * l0 * 1.4, U.note_tint * 0.6);

    // grade
    col = col * U.exposure;
    col = hue_rotate(col, U.hue_shift * TAU);
    let l = luma(col);
    col = max(l + (col - l) * U.saturation, vec3f(0.0));
    col = max(pow(col / 0.18, vec3f(U.contrast)) * 0.18, vec3f(0.0));

    var outc: vec3f;
    if (U.output_mode == 2u) {
        // EDR: extended linear, Display P3 primaries
        let p3 = mat3x3f(0.8225, 0.0332, 0.0171, 0.1774, 0.9669, 0.0724, 0.0, 0.0, 0.9108);
        outc = p3 * tonemap_edr(col);
    } else {
        outc = tonemap(col);
    }

    // vignette
    var v = in.uv - 0.5;
    v.x = v.x * U.resolution.x / U.resolution.y;
    outc = outc * mix(1.0, smoothstep(1.25, 0.25, length(v)), U.vignette);

    // scanlines (CRT)
    if (U.scanlines > 0.001) {
        let line = 0.5 + 0.5 * sin(in.pos.y * PI * 0.5);
        outc = outc * mix(1.0, 0.55 + 0.45 * line, U.scanlines);
    }

    // film grain + dither
    let n = hash21(in.pos.xy + vec2f(f32(U.frame % 1024u) * 17.0, f32(U.frame % 777u) * 3.0)) - 0.5;
    outc = outc * (1.0 + n * U.grain * 0.5);
    if (U.output_mode == 1u) {
        outc = linear_to_srgb(saturate3(outc));
        outc = outc + n / 255.0;
    } else if (U.output_mode == 0u) {
        outc = outc + n * outc * 0.004;
    }
    return vec4f(outc, 1.0);
}
