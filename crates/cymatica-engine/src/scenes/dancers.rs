//! Dancers: a crowd of neon stick figures dancing to the beat on a disco stage. Each figure's
//! hands, feet, head and hips are springs chasing the dance's pose (so limbs lag and swing), and
//! elbows and knees follow by inverse kinematics. Eight moves, synced to the tempo tracker,
//! change every few bars; kicks make them hop, snares make them clap.

use crate::globals::GlobalsGpu;
use crate::gpu::{self, Bind, Gpu, HDR_FORMAT};
use crate::params::{choice, float, int, toggle, ParamDef};
use crate::scene::{scene_source, Scene, SceneCtx};
use bytemuck::{Pod, Zeroable};
use glam::Vec2;
use std::f32::consts::{PI, TAU};

pub static PARAMS: &[ParamDef] = &[
    int("crowd", "Dancers", "Crowd", 1, 40, 9, "How many stick figures."),
    choice(
        "dance",
        "Dance",
        "Crowd",
        &[
            ("mix", "Mix it up"),
            ("bounce", "Bounce"),
            ("disco", "Disco"),
            ("robot", "Robot"),
            ("jacks", "Jumping jacks"),
            ("wave", "The wave"),
            ("headbang", "Headbang"),
            ("running", "Running man"),
            ("twist", "The twist"),
        ],
        "mix",
        "One move, or a new one every few bars.",
    ),
    choice(
        "sync",
        "Together",
        "Crowd",
        &[("together", "In sync"), ("loose", "Loosely together"), ("solo", "Everyone their own")],
        "loose",
        "Same move at the same time, roughly together, or each their own.",
    ),
    int("change_bars", "Change every (bars)", "Crowd", 1, 16, 4, "How many bars before the next move."),
    float("energy", "Energy", "Moves", 0.0, 1.0, 0.7, "How big the moves are (the music adds to it)."),
    float("floppy", "Floppiness", "Moves", 0.0, 1.0, 0.45, "Stiff robots or wobbly noodles (spring physics)."),
    float("width", "Line width", "Look", 0.5, 3.0, 1.0, "How thick the neon tubes are."),
    float("glow", "Glow", "Look", 0.0, 2.0, 1.0, "How brightly they shine."),
    toggle("lights", "Stage lights", "Stage", true, "Spotlights and a flashing dance floor."),
    toggle("mirror", "Mirror floor", "Stage", true, "Reflections on the floor."),
];

const MOVES: usize = 8;
const MAX_DANCERS: usize = 40;
/// Primitives per dancer: spine, 2 x upper arm, forearm, thigh, shin, and the head ring.
const PRIMS: usize = 10;

// body proportions (figure height 1, origin between the feet, y up)
const HIP: Vec2 = Vec2::new(0.0, 0.5);
const SHOULDER: f32 = 0.32; // spine length, hip to shoulders
const NECK: f32 = 0.06;
const HEAD_R: f32 = 0.075;
const ARM: [f32; 2] = [0.17, 0.16];
const LEG: [f32; 2] = [0.25, 0.24];

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable, Default)]
struct Prim {
    /// segment ends in screen units (height 2, y up), or the head's centre twice
    ab: [f32; 4],
    /// radius, ring radius (0 = a tube), palette position, brightness
    style: [f32; 4],
}

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct StageUniform {
    floor_y: f32,
    lights: f32,
    horizon: f32,
    beat_phase: f32,
    beat_count: f32,
    light_angle: f32,
    flash: f32,
    count: u32,
}

/// Where a move wants the body: hand and foot targets relative to the shoulders and hips.
#[derive(Clone, Copy)]
struct Pose {
    /// hips' offset (sway, bounce)
    root: Vec2,
    /// spine lean (radians, + leans right)
    lean: f32,
    /// head offset from the top of the spine's line
    head: Vec2,
    hands: [Vec2; 2],
    feet: [Vec2; 2],
}

/// A point at `angle` from straight down (positive = outward for the right side) at `reach`.
fn limb(angle: f32, reach: f32, side: f32) -> Vec2 {
    Vec2::new(angle.sin() * side, -angle.cos()) * reach
}

fn pose(mv: usize, beat: f32, e: f32, side_phase: f32) -> Pose {
    let ph = beat.fract();
    let n = beat.floor() as i64;
    let even = n.rem_euclid(2) == 0;
    let s = (TAU * ph).sin();
    let c = (TAU * ph).cos();
    let half = (PI * beat).sin();
    let e = e.clamp(0.0, 1.5);
    let mut p = Pose {
        root: Vec2::ZERO,
        lean: 0.0,
        head: Vec2::ZERO,
        hands: [limb(0.25, 0.3, -1.0), limb(0.25, 0.3, 1.0)],
        feet: [limb(0.08, 0.48, -1.0), limb(0.08, 0.48, 1.0)],
    };
    match mv {
        // bounce: knees on the beat, arms swinging
        0 => {
            p.root.y = -0.06 * e * (0.5 + 0.5 * c);
            p.hands = [limb(0.35 + 0.35 * half * e, 0.28, -1.0), limb(0.35 - 0.35 * half * e, 0.28, 1.0)];
            p.head.y = -0.015 * e * (0.5 + 0.5 * c);
        }
        // disco: point up to the right, then down across, hips swaying
        1 => {
            let up = if even { 1.0 } else { 0.0 };
            p.hands[1] = limb(0.4 + 1.95 * up, 0.32, 1.0);
            p.hands[0] = limb(0.7, 0.16, -1.0);
            p.root.x = 0.04 * e * if even { 1.0 } else { -1.0 };
            p.lean = 0.12 * e * if even { -1.0 } else { 1.0 };
            p.root.y = -0.03 * e * (0.5 + 0.5 * c);
        }
        // robot: a new stiff pose every beat
        2 => {
            let k = n.rem_euclid(4);
            let (l, r) = match k {
                0 => ((PI * 0.5, 0.22), (PI * 0.5, 0.22)),
                1 => ((PI * 0.9, 0.23), (0.3, 0.22)),
                2 => ((0.3, 0.22), (PI * 0.9, 0.23)),
                _ => ((PI * 0.5, 0.32), (PI * 0.5, 0.32)),
            };
            p.hands = [limb(l.0, l.1, -1.0), limb(r.0, r.1, 1.0)];
            p.head.x = 0.02 * if even { 1.0 } else { -1.0 } * e;
            p.root.y = -0.02 * e;
        }
        // jumping jacks
        3 => {
            let open = if ph < 0.5 { 1.0 } else { 0.0 };
            p.hands = [limb(0.3 + 2.5 * open * e, 0.32, -1.0), limb(0.3 + 2.5 * open * e, 0.32, 1.0)];
            p.feet = [limb(0.05 + 0.3 * open * e, 0.48, -1.0), limb(0.05 + 0.3 * open * e, 0.48, 1.0)];
            p.root.y = 0.05 * e * (PI * (ph * 2.0).fract()).sin().max(0.0);
        }
        // the wave: arms up, swaying, rippling through the crowd
        4 => {
            let sway = (PI * beat * 0.5 + side_phase).sin();
            p.lean = 0.16 * e * sway;
            p.hands = [limb(2.6 + 0.3 * sway, 0.32, -1.0), limb(2.6 - 0.3 * sway, 0.32, 1.0)];
            p.root.x = 0.03 * sway;
        }
        // headbang with air guitar
        5 => {
            let bang = (0.5 + 0.5 * c) * e;
            p.head.y = -0.05 * bang;
            p.head.x = 0.01 * bang;
            p.root.y = -0.04 * bang;
            p.hands[0] = limb(1.3, 0.27, -1.0);
            p.hands[1] = limb(0.5 + 0.3 * s * e, 0.2, 1.0);
        }
        // running man: knees up in turn, arms pumping
        6 => {
            let left = if even { 1.0 } else { 0.0 };
            let lift = (PI * ph).sin() * e;
            p.feet[0] = Vec2::new(-0.05, -0.48 + 0.22 * lift * left);
            p.feet[1] = Vec2::new(0.05, -0.48 + 0.22 * lift * (1.0 - left));
            p.hands = [limb(0.6 + 0.8 * (left - 0.5) * e, 0.22, -1.0), limb(0.6 - 0.8 * (left - 0.5) * e, 0.22, 1.0)];
            p.root.y = 0.02 * lift;
        }
        // the twist: knees bent, swinging side to side, elbows in
        _ => {
            p.root.y = -0.07 * e;
            p.lean = 0.18 * e * half;
            p.root.x = -0.03 * e * half;
            p.hands = [limb(0.9 - 0.5 * half * e, 0.18, -1.0), limb(0.9 + 0.5 * half * e, 0.18, 1.0)];
            p.feet = [limb(0.12, 0.44, -1.0), limb(0.12, 0.44, 1.0)];
        }
    }
    p
}

/// A spring-damped point chasing its target.
#[derive(Clone, Copy, Default)]
struct Spring {
    x: Vec2,
    v: Vec2,
}

impl Spring {
    fn step(&mut self, target: Vec2, k: f32, dt: f32) {
        let damping = 2.0 * 0.45 * k.sqrt();
        self.v += (k * (target - self.x) - damping * self.v) * dt;
        self.x += self.v * dt;
    }
}

#[derive(Clone)]
struct Dancer {
    root: Spring,
    head: Spring,
    hands: [Spring; 2],
    feet: [Spring; 2],
    lean: f32,
    phase: f32,
    amp: f32,
    seed: u32,
}

/// Elbow or knee between `base` and `end` for bones `l`, bending towards `bend` (+1/-1).
fn ik(base: Vec2, end: Vec2, l: [f32; 2], bend: f32) -> (Vec2, Vec2) {
    let mut d = end - base;
    let len = d.length().max(1e-4);
    let reach = (l[0] + l[1]) * 0.999;
    if len > reach {
        d *= reach / len;
    }
    let len = d.length().max(1e-4);
    let cos_a = ((l[0] * l[0] + len * len - l[1] * l[1]) / (2.0 * l[0] * len)).clamp(-1.0, 1.0);
    let a = cos_a.acos() * bend;
    let dir = d / len;
    let joint = base + Vec2::new(dir.x * a.cos() - dir.y * a.sin(), dir.x * a.sin() + dir.y * a.cos()) * l[0];
    (joint, base + d)
}

fn hash(n: u32) -> u32 {
    let mut x = n.wrapping_mul(0x9E37_79B9) ^ 0x85EB_CA6B;
    x ^= x >> 15;
    x = x.wrapping_mul(0x2C1B_3C6D);
    x ^= x >> 12;
    x
}

pub struct Dancers {
    uniform: wgpu::Buffer,
    prims: wgpu::Buffer,
    bg: wgpu::BindGroup,
    p_stage: wgpu::RenderPipeline,
    p_figures: wgpu::RenderPipeline,
    dancers: Vec<Dancer>,
    /// our own beat clock, gently pulled towards the tempo tracker's (so moves never jump)
    beat: f32,
    light_angle: f32,
    flash: f32,
    clap: f32,
    last_hits: [u32; 3],
}

pub fn create(gpu: &Gpu, globals: &GlobalsGpu) -> Box<dyn Scene> {
    let module = gpu.shader("dancers", scene_source(include_str!("../shaders/scenes/dancers.wgsl")));
    let layout = gpu.layout("dancers", &[Bind::Uniform, Bind::StorageRead]);
    let pl = gpu.pipeline_layout("dancers", &[&globals.layout, &layout]);
    let uniform = gpu.uniform_buffer("dancers-u", std::mem::size_of::<StageUniform>() as u64);
    let prims = gpu.storage_buffer("dancers-prims", (MAX_DANCERS * PRIMS * 2 * std::mem::size_of::<Prim>()) as u64);
    Box::new(Dancers {
        bg: gpu.bind_group("dancers", &layout, &[uniform.as_entire_binding(), prims.as_entire_binding()]),
        p_stage: gpu.fullscreen_pipeline("dancers-stage", &module, "fs_stage", &pl, HDR_FORMAT, None),
        p_figures: gpu.render_pipeline(
            "dancers-figures",
            &module,
            "vs_prim",
            "fs_prim",
            &pl,
            HDR_FORMAT,
            Some(gpu::ADDITIVE),
            wgpu::PrimitiveTopology::TriangleList,
        ),
        uniform,
        prims,
        dancers: Vec::new(),
        beat: 0.0,
        light_angle: 0.0,
        flash: 0.0,
        clap: 0.0,
        last_hits: [0; 3],
    })
}

impl Dancers {
    fn ensure_crowd(&mut self, n: usize) {
        while self.dancers.len() < n {
            let i = self.dancers.len() as u32;
            let h = hash(i + 1);
            let start = Pose {
                root: Vec2::ZERO,
                lean: 0.0,
                head: Vec2::ZERO,
                hands: [limb(0.25, 0.3, -1.0), limb(0.25, 0.3, 1.0)],
                feet: [limb(0.08, 0.48, -1.0), limb(0.08, 0.48, 1.0)],
            };
            let at = |p: Vec2| Spring { x: p, v: Vec2::ZERO };
            self.dancers.push(Dancer {
                root: at(start.root),
                head: at(Vec2::ZERO),
                hands: [at(start.hands[0]), at(start.hands[1])],
                feet: [at(start.feet[0]), at(start.feet[1])],
                lean: 0.0,
                phase: (h % 1000) as f32 / 1000.0,
                amp: 0.85 + ((h >> 10) % 1000) as f32 / 1000.0 * 0.3,
                seed: h,
            });
        }
        self.dancers.truncate(n);
    }
}

/// The front row's feet and the floor's horizon (screen units, y up).
const FLOOR_Y: f32 = -0.66;
const HORIZON: f32 = -0.12;

/// Rows for a crowd of `n`: as few as possible with at most 7 in each, evenly filled.
fn rows(n: usize) -> (usize, usize) {
    let rows = n.div_ceil(7).max(1);
    (rows, n.div_ceil(rows))
}

/// Screen placement for dancer `i` of `n`: rows from the front (big, low) to the back, in
/// perspective on the floor.
fn placement(i: usize, n: usize, aspect: f32) -> (Vec2, f32, usize) {
    let (_, per_row) = rows(n);
    let row = i / per_row;
    let in_row = i % per_row;
    let count = (n - row * per_row).min(per_row);
    let depth = 1.0 + row as f32 * 0.6;
    let scale = 0.72 / depth;
    let span = (aspect * 1.75 / depth).min(count as f32 * 0.5 * scale + 0.15);
    let x = if count > 1 { (in_row as f32 / (count - 1) as f32 - 0.5) * span } else { 0.0 };
    // further back = closer to the horizon
    let y = HORIZON - (HORIZON - FLOOR_Y) / depth;
    (Vec2::new(x + if row % 2 == 1 { 0.18 * scale } else { 0.0 }, y), scale, row)
}

impl Scene for Dancers {
    fn reset(&mut self, _gpu: &Gpu) {
        self.dancers.clear();
    }

    fn render(&mut self, gpu: &Gpu, ctx: &mut SceneCtx) {
        let p = ctx.params;
        let a = &ctx.info.audio;
        let dt = ctx.info.dt.clamp(0.0, 0.05);
        // (the same "energy" the shaders get)
        let music = a.loudness.max(a.bass * 0.85);
        let n = p.i("crowd").clamp(1, MAX_DANCERS as i32) as usize;
        self.ensure_crowd(n);

        // the beat clock: run at the detected tempo, and pull gently towards the tracker's phase
        let bpm = if a.tempo_confidence > 0.2 && a.bpm > 40.0 { a.bpm } else { 120.0 };
        self.beat += dt * bpm / 60.0;
        if a.tempo_confidence > 0.2 {
            let target = a.beat_count as f32 + a.beat_phase;
            let mut err = target - self.beat;
            if err.abs() > 8.0 {
                self.beat = target;
                err = 0.0;
            }
            // only the phase matters: pull towards the nearest matching beat
            let err_phase = err - err.round();
            self.beat += err_phase * (dt * 3.0).min(1.0);
        }

        // hits
        if a.hits[0] != self.last_hits[0] {
            let jump = a.onset[0] * p.f("energy") * 0.9;
            for d in &mut self.dancers {
                d.root.v.y += jump * d.amp;
            }
            self.flash = 1.0;
        }
        if a.hits[1] != self.last_hits[1] {
            self.clap = 1.0;
        }
        self.last_hits = a.hits;
        self.flash *= (-dt * 6.0).exp();
        self.clap *= (-dt * 7.0).exp();
        self.light_angle += dt * (0.4 + music * 1.5);

        // choreography
        let bars = p.i("change_bars").clamp(1, 16) as f32;
        let section = (self.beat / (4.0 * bars)).floor() as u32;
        let dance = p.choice_index("dance");
        let sync = p.choice_index("sync");
        let energy = p.f("energy") * (0.55 + music * 0.9);
        let floppy = p.f("floppy");
        let aspect = ctx.size.0 as f32 / ctx.size.1.max(1) as f32;
        let base_k = 900.0 * (1.0 - floppy) + 70.0 * floppy;

        let mut prims: Vec<Prim> = Vec::with_capacity(n * PRIMS * 2);
        let width = p.f("width");
        let glow = p.f("glow");
        // back rows first (they're drawn dimmer, behind)
        let mut order: Vec<usize> = (0..n).collect();
        order.sort_by_key(|&i| std::cmp::Reverse(i / rows(n).1));
        for &i in &order {
            let (pos, scale, row) = placement(i, n, aspect);
            let d = &mut self.dancers[i];
            let mv = if dance == 0 {
                let who = if sync == 2 { d.seed } else { 0 };
                (hash(section.wrapping_mul(7919) ^ who) as usize) % MOVES
            } else {
                dance - 1
            };
            let offset = match sync {
                0 => 0.0,
                1 => (d.phase - 0.5) * 0.12,
                _ => d.phase * 0.5,
            };
            let side_phase = pos.x * 2.0;
            let mut target = pose(mv, self.beat + offset, energy * d.amp, side_phase);
            // snare claps: hands meet in front of the chest
            let clap_at = [Vec2::new(-0.02, 0.0), Vec2::new(0.02, 0.0)];
            for (h, c) in target.hands.iter_mut().zip(clap_at) {
                *h = h.lerp(c, self.clap * 0.7);
            }
            let k = if mv == 2 { base_k * 3.0 } else { base_k };
            // physics (two sub-steps keep the springs stable at low frame rates)
            for _ in 0..2 {
                let h = dt * 0.5;
                d.root.step(target.root, k * 0.6, h);
                d.head.step(target.head, k * 1.2, h);
                for s in 0..2 {
                    d.hands[s].step(target.hands[s], k, h);
                    d.feet[s].step(target.feet[s], k * 1.5, h);
                }
                d.lean += (target.lean - d.lean) * (h * 10.0).min(1.0);
            }
            // the floor holds the feet up, and the body can't sink into it
            d.root.x.y = d.root.x.y.max(-0.12);

            // the skeleton, in body space
            let hip = HIP + d.root.x;
            let up = Vec2::new(d.lean.sin(), d.lean.cos());
            let shoulders = hip + up * SHOULDER;
            let neck = shoulders + up * NECK;
            let head = neck + up * HEAD_R + d.head.x;
            let to_screen = |b: Vec2| pos + b * scale;
            let mut feet = [hip + d.feet[0].x, hip + d.feet[1].x];
            for f in &mut feet {
                f.y = f.y.max(0.0);
            }
            // (elbows and knees bend outward, away from the body)
            let (le, lh) = ik(shoulders, shoulders + d.hands[0].x, ARM, -1.0);
            let (re, rh) = ik(shoulders, shoulders + d.hands[1].x, ARM, 1.0);
            let (lk, lf) = ik(hip, feet[0], LEG, -1.0);
            let (rk, rf) = ik(hip, feet[1], LEG, 1.0);
            let tint = i as f32 / n as f32;
            let bright = glow * (0.9 + 0.5 * a.beat) / (1.0 + row as f32 * 0.35);
            let r = 0.011 * width * scale;
            let mut seg = |a: Vec2, b: Vec2| {
                let (a, b) = (to_screen(a), to_screen(b));
                prims.push(Prim { ab: [a.x, a.y, b.x, b.y], style: [r, 0.0, tint, bright] });
            };
            seg(hip, neck);
            seg(shoulders, le);
            seg(le, lh);
            seg(shoulders, re);
            seg(re, rh);
            seg(hip, lk);
            seg(lk, lf);
            seg(hip, rk);
            seg(rk, rf);
            let hc = to_screen(head);
            prims.push(Prim { ab: [hc.x, hc.y, hc.x, hc.y], style: [r, HEAD_R * scale, tint, bright] });
        }
        let count = prims.len() as u32;
        if p.b("mirror") {
            // reflections: mirrored about each dancer's own floor line, dimmer
            let mirrored: Vec<Prim> = prims
                .iter()
                .enumerate()
                .map(|(k, q)| {
                    let i = order[k / PRIMS];
                    let (pos, _, _) = placement(i, n, aspect);
                    let fy = pos.y;
                    let mut m = *q;
                    m.ab[1] = 2.0 * fy - q.ab[1];
                    m.ab[3] = 2.0 * fy - q.ab[3];
                    m.style[3] *= -0.22; // negative = a reflection (fades with depth)
                    m
                })
                .collect();
            prims.extend(mirrored);
        }
        let u = StageUniform {
            floor_y: FLOOR_Y,
            lights: if p.b("lights") { 1.0 } else { 0.0 },
            horizon: HORIZON,
            beat_phase: self.beat.fract(),
            beat_count: (self.beat.floor() % 4096.0).max(0.0),
            light_angle: self.light_angle % 1000.0,
            flash: self.flash,
            count,
        };
        gpu.queue.write_buffer(&self.uniform, 0, bytemuck::bytes_of(&u));
        gpu.queue.write_buffer(&self.prims, 0, bytemuck::cast_slice(&prims));
        let globals = &ctx.globals.bind_group;
        gpu::fullscreen(ctx.encoder, "dancers-stage", ctx.target, &self.p_stage, &[globals, &self.bg], None);
        let mut rp = gpu::pass(ctx.encoder, "dancers-figures", ctx.target, None);
        rp.set_pipeline(&self.p_figures);
        rp.set_bind_group(0, globals, &[]);
        rp.set_bind_group(1, &self.bg, &[]);
        rp.draw(0..6, 0..prims.len() as u32);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_move_keeps_limbs_reachable_and_finite() {
        for mv in 0..MOVES {
            for k in 0..64 {
                let beat = k as f32 * 0.137;
                let p = pose(mv, beat, 1.2, 0.3);
                for v in p.hands.iter().chain(p.feet.iter()).chain([p.root, p.head].iter()) {
                    assert!(v.is_finite(), "move {mv} gave {v:?}");
                }
                for h in p.hands {
                    assert!(h.length() <= ARM[0] + ARM[1] + 1e-3, "move {mv}: hand out of reach {h:?}");
                }
            }
        }
    }

    #[test]
    fn ik_keeps_bone_lengths() {
        let (j, e) = ik(Vec2::ZERO, Vec2::new(0.2, -0.1), ARM, 1.0);
        assert!((j.length() - ARM[0]).abs() < 1e-4);
        assert!(((e - j).length() - ARM[1]).abs() < 1e-4);
        // out of reach: the limb is fully stretched towards the target
        let (_, e) = ik(Vec2::ZERO, Vec2::new(5.0, 0.0), ARM, 1.0);
        assert!((e.length() - (ARM[0] + ARM[1]) * 0.999).abs() < 1e-3);
    }
}
