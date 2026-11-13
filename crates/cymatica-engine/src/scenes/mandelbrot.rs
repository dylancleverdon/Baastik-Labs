//! Mandelbrot: an endless dive into the Mandelbrot set. The view centre and the reference orbits
//! are computed on the CPU in double-double precision (~32 digits); the GPU iterates every pixel
//! as a small f32 offset from the reference orbit (perturbation, with rebasing), so it can zoom
//! about 10^20 times before the precision runs out. A background thread keeps steering the dive
//! towards the most intricate part of the picture; when a dive bottoms out (or runs into a
//! featureless patch), it zooms back out and dives again somewhere else. The bass pushes the dive,
//! beats and highs drive the colours.

use crate::globals::GlobalsGpu;
use crate::gpu::{self, Bind, Gpu, HDR_FORMAT};
use crate::params::{choice, float, ParamDef};
use crate::scene::{scene_source, Scene, SceneCtx};
use bytemuck::{Pod, Zeroable};
use std::sync::mpsc;
use std::sync::Arc;

pub static PARAMS: &[ParamDef] = &[
    float("speed", "Zoom speed", "Dive", 0.0, 1.0, 0.45, "How fast it dives (the bass pushes it)."),
    choice(
        "region",
        "Where to dive",
        "Dive",
        &[
            ("anywhere", "Anywhere"),
            ("seahorse", "Seahorse valley"),
            ("elephant", "Elephant valley"),
            ("spirals", "Spirals"),
            ("filaments", "Filaments"),
        ],
        "anywhere",
        "Where each dive starts; from there it follows the detail.",
    ),
    float("spin", "Spin", "Dive", -1.0, 1.0, 0.1, "Slow rotation."),
    float("bands", "Colour bands", "Colour", 0.0, 1.0, 0.5, "How close together the colour bands are."),
    float("cycle", "Colour flow", "Colour", 0.0, 1.0, 0.5, "How fast the colours flow (beats push them)."),
    float("relief", "Relief", "Colour", 0.0, 1.0, 0.6, "Embossed 3D lighting."),
    float("glow", "Edge glow", "Colour", 0.0, 1.0, 0.5, "Light along the set's edge (the bass swells it)."),
    choice(
        "quality",
        "Detail",
        "Quality",
        &[("low", "Low"), ("medium", "Medium"), ("high", "High")],
        "medium",
        "More iterations: finer detail deep down, more GPU work.",
    )
    .fixed(),
];

/// The deepest a dive goes (view half-height in the complex plane): double-double reference
/// orbits stay accurate to about here.
const MIN_SCALE: f64 = 1e-20;
const START_SCALE: f64 = 1.6;
/// Coarse grid the steering samples (over the middle of the view).
const STEER_W: usize = 24;
const STEER_H: usize = 14;

// ---------------------------------------------------------------- double-double arithmetic

/// A number as the unevaluated sum of two f64s (~32 significant digits).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Dd {
    hi: f64,
    lo: f64,
}

fn two_sum(a: f64, b: f64) -> (f64, f64) {
    let s = a + b;
    let bb = s - a;
    (s, (a - (s - bb)) + (b - bb))
}

fn quick_two_sum(a: f64, b: f64) -> (f64, f64) {
    let s = a + b;
    (s, b - (s - a))
}

impl Dd {
    pub const ZERO: Dd = Dd { hi: 0.0, lo: 0.0 };

    pub fn new(x: f64) -> Self {
        Dd { hi: x, lo: 0.0 }
    }

    pub fn f64(self) -> f64 {
        self.hi + self.lo
    }

    pub fn plus(self, o: Dd) -> Dd {
        let (s, e) = two_sum(self.hi, o.hi);
        let (t, f) = two_sum(self.lo, o.lo);
        let (s, e) = quick_two_sum(s, e + t);
        let (hi, lo) = quick_two_sum(s, e + f);
        Dd { hi, lo }
    }

    pub fn minus(self, o: Dd) -> Dd {
        self.plus(Dd { hi: -o.hi, lo: -o.lo })
    }

    pub fn times(self, o: Dd) -> Dd {
        let p = self.hi * o.hi;
        let e = self.hi.mul_add(o.hi, -p) + (self.hi * o.lo + self.lo * o.hi);
        let (hi, lo) = quick_two_sum(p, e);
        Dd { hi, lo }
    }

    pub fn mul_f64(self, b: f64) -> Dd {
        let p = self.hi * b;
        let e = self.hi.mul_add(b, -p) + self.lo * b;
        let (hi, lo) = quick_two_sum(p, e);
        Dd { hi, lo }
    }
}

/// A point of the complex plane in double-double.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Cdd {
    pub re: Dd,
    pub im: Dd,
}

impl Cdd {
    pub fn new(re: f64, im: f64) -> Self {
        Cdd { re: Dd::new(re), im: Dd::new(im) }
    }

    fn plus(self, o: Cdd) -> Cdd {
        Cdd { re: self.re.plus(o.re), im: self.im.plus(o.im) }
    }

    fn minus(self, o: Cdd) -> Cdd {
        Cdd { re: self.re.minus(o.re), im: self.im.minus(o.im) }
    }

    fn offset(self, d: (f64, f64)) -> Cdd {
        self.plus(Cdd::new(d.0, d.1))
    }

    fn to_f64(self) -> (f64, f64) {
        (self.re.f64(), self.im.f64())
    }
}

/// The reference orbit of `c` (z -> z^2 + c from 0), until it escapes or after `max_iter` steps.
pub fn reference_orbit(c: Cdd, max_iter: usize) -> Vec<(f64, f64)> {
    let mut out = Vec::with_capacity(max_iter + 1);
    let (mut zr, mut zi) = (Dd::ZERO, Dd::ZERO);
    out.push((0.0, 0.0));
    for _ in 0..max_iter {
        let zr2 = zr.times(zr);
        let zi2 = zi.times(zi);
        let zri = zr.times(zi);
        zr = zr2.minus(zi2).plus(c.re);
        zi = zri.mul_f64(2.0).plus(c.im);
        let z = (zr.f64(), zi.f64());
        out.push(z);
        if z.0 * z.0 + z.1 * z.1 > 1e6 {
            break;
        }
    }
    out
}

/// Smooth escape count of the point at offset `dc` from the reference orbit's point
/// (perturbation with rebasing, like the shader), or None if it doesn't escape.
pub fn escape_count(dc: (f64, f64), orbit: &[(f64, f64)], max_iter: usize) -> Option<f64> {
    if orbit.len() < 2 {
        return None;
    }
    let (mut dr, mut di) = (0.0f64, 0.0f64);
    let mut m = 0usize;
    for i in 0..max_iter {
        let (zr, zi) = orbit[m];
        let nr = 2.0 * (zr * dr - zi * di) + (dr * dr - di * di) + dc.0;
        let ni = 2.0 * (zr * di + zi * dr) + 2.0 * dr * di + dc.1;
        dr = nr;
        di = ni;
        m += 1;
        let (zr, zi) = orbit[m];
        let (fr, fi) = (zr + dr, zi + di);
        let r2 = fr * fr + fi * fi;
        if r2 > 1e4 {
            return Some(i as f64 + 1.0 - (0.5 * r2.ln()).log2().max(1e-9).log2());
        }
        if r2 < dr * dr + di * di || m + 1 >= orbit.len() {
            dr = fr;
            di = fi;
            m = 0;
        }
    }
    None
}

// ---------------------------------------------------------------- steering (background thread)

struct SteerRequest {
    center: Cdd,
    scale: f64,
    angle: f64,
    aspect: f64,
    max_iter: usize,
    reference: Cdd,
    orbit: Arc<Vec<(f64, f64)>>,
}

struct Steered {
    /// where to head next (None: nothing interesting in view)
    target: Option<Cdd>,
    reference: Cdd,
    orbit: Arc<Vec<(f64, f64)>>,
    /// the slowest escape seen (close to max_iter: the dive needs more iterations)
    deepest: f64,
}

fn steer(req: SteerRequest) -> Steered {
    let (sin, cos) = req.angle.sin_cos();
    let offset = req.center.minus(req.reference).to_f64();
    // escape counts on a coarse grid over the middle of the view (None = inside the set)
    let mut grid = vec![None; STEER_W * STEER_H];
    let mut dcs = vec![(0.0, 0.0); STEER_W * STEER_H];
    let mut deepest = 0.0f64;
    for gy in 0..STEER_H {
        for gx in 0..STEER_W {
            let px = ((gx as f64 + 0.5) / STEER_W as f64 * 2.0 - 1.0) * req.aspect * 0.7;
            let py = ((gy as f64 + 0.5) / STEER_H as f64 * 2.0 - 1.0) * 0.7;
            let (rx, ry) = (px * cos - py * sin, px * sin + py * cos);
            let dc = (offset.0 + rx * req.scale, offset.1 + ry * req.scale);
            let n = escape_count(dc, &req.orbit, req.max_iter);
            if let Some(n) = n {
                deepest = deepest.max(n);
            }
            grid[gy * STEER_W + gx] = n;
            dcs[gy * STEER_W + gx] = dc;
        }
    }
    // the most intricate spot is where points take longest to escape, best right next to a
    // piece of the set (the edges of bulbs and mini-Mandelbrots, where the spirals are) rather
    // than in a featureless tangle of filaments; ones near the middle keep the dive steady
    let mut best: Option<(f64, (f64, f64))> = None;
    // (points that take most of the iteration budget may be wrongly "inside" next to it: heading
    // there leads into dust the budget can't resolve)
    let resolvable = 0.6 * req.max_iter as f64;
    for gy in 0..STEER_H {
        for gx in 0..STEER_W {
            let Some(n) = grid[gy * STEER_W + gx] else { continue };
            if n > resolvable {
                continue;
            }
            let inside_next = [(-1i64, 0i64), (1, 0), (0, -1), (0, 1)].iter().any(|(dx, dy)| {
                let (x, y) = (gx as i64 + dx, gy as i64 + dy);
                x >= 0
                    && y >= 0
                    && (x as usize) < STEER_W
                    && (y as usize) < STEER_H
                    && grid[y as usize * STEER_W + x as usize].is_none()
            });
            let (px, py) =
                ((gx as f64 + 0.5) / STEER_W as f64 * 2.0 - 1.0, (gy as f64 + 0.5) / STEER_H as f64 * 2.0 - 1.0);
            let d = (px * px + py * py).sqrt();
            let score = n * (1.0 - 0.4 * d) * if inside_next { 1.0 } else { 0.55 };
            if best.is_none_or(|b| score > b.0) {
                best = Some((score, dcs[gy * STEER_W + gx]));
            }
        }
    }
    match best {
        // nothing escapes (all inside the set) or everything escapes at once: featureless
        Some((_, dc)) if deepest > 12.0 => {
            let target = req.reference.offset(dc);
            let orbit = Arc::new(reference_orbit(target, req.max_iter));
            Steered { target: Some(target), reference: target, orbit, deepest }
        }
        _ => Steered { target: None, reference: req.reference, orbit: req.orbit, deepest },
    }
}

// ---------------------------------------------------------------- the scene

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct MandelUniform {
    offset: [f32; 2],
    scale: f32,
    angle: f32,
    max_iter: u32,
    orbit_len: u32,
    bands: f32,
    color_offset: f32,
    relief: f32,
    glow: f32,
    pixel: f32,
    light: f32,
}

const MAX_ORBIT: usize = 8001;
// (MAX_ORBIT must hold the highest quality's iterations)

/// Dive start points (shallow, so f64 is plenty).
fn region_point(region: usize, dive: u32) -> (f64, f64) {
    const SEAHORSE: [(f64, f64); 3] = [(-0.7436, 0.1318), (-0.7453, 0.1127), (-0.7488, 0.0636)];
    const ELEPHANT: [(f64, f64); 3] = [(0.2756, 0.0068), (0.2855, 0.0102), (0.3245, 0.0425)];
    const SPIRALS: [(f64, f64); 3] = [(-0.0880, 0.6551), (-0.1600, 1.0378), (0.4245, 0.2075)];
    const FILAMENTS: [(f64, f64); 3] = [(-1.7685, 0.0017), (-1.2560, 0.3820), (-0.1011, 0.9563)];
    let k = (dive as usize) % 3;
    match region {
        1 => SEAHORSE[k],
        2 => ELEPHANT[k],
        3 => SPIRALS[k],
        4 => FILAMENTS[k],
        _ => [SEAHORSE, ELEPHANT, SPIRALS, FILAMENTS][(dive as usize / 3 + dive as usize) % 4][k],
    }
}

pub struct Mandelbrot {
    uniform: wgpu::Buffer,
    orbit_buf: wgpu::Buffer,
    bg: wgpu::BindGroup,
    pipeline: wgpu::RenderPipeline,
    center: Cdd,
    target: Cdd,
    scale: f64,
    angle: f64,
    reference: Cdd,
    orbit: Arc<Vec<(f64, f64)>>,
    orbit_dirty: bool,
    color_offset: f32,
    /// > 0 while zooming back out between dives
    surfacing: bool,
    dive: u32,
    since_steer: f32,
    jobs: mpsc::Sender<SteerRequest>,
    results: mpsc::Receiver<Steered>,
    pending: bool,
    punch: f64,
    last_kick: u32,
    /// more iterations for this dive, when points near the target take very long to escape
    iter_boost: f64,
    /// the first dive starts on the first frame (when the region setting is known)
    started: bool,
}

pub fn create(gpu: &Gpu, globals: &GlobalsGpu) -> Box<dyn Scene> {
    let module = gpu.shader("mandelbrot", scene_source(include_str!("../shaders/scenes/mandelbrot.wgsl")));
    let layout = gpu.layout("mandelbrot", &[Bind::Uniform, Bind::StorageRead]);
    let pl = gpu.pipeline_layout("mandelbrot", &[&globals.layout, &layout]);
    let uniform = gpu.uniform_buffer("mandel-u", std::mem::size_of::<MandelUniform>() as u64);
    let orbit_buf = gpu.storage_buffer("mandel-orbit", (MAX_ORBIT * 8) as u64);
    let (job_tx, job_rx) = mpsc::channel::<SteerRequest>();
    let (res_tx, res_rx) = mpsc::channel::<Steered>();
    let _ = std::thread::Builder::new().name("cymatica-mandelbrot".into()).spawn(move || {
        while let Ok(req) = job_rx.recv() {
            if res_tx.send(steer(req)).is_err() {
                break;
            }
        }
    });
    let start = Cdd::new(-0.6, 0.0);
    Box::new(Mandelbrot {
        bg: gpu.bind_group("mandelbrot", &layout, &[uniform.as_entire_binding(), orbit_buf.as_entire_binding()]),
        pipeline: gpu.fullscreen_pipeline("mandelbrot", &module, "fs_mandel", &pl, HDR_FORMAT, None),
        uniform,
        orbit_buf,
        center: start,
        target: start,
        scale: START_SCALE,
        angle: 0.0,
        reference: start,
        orbit: Arc::new(reference_orbit(start, 400)),
        orbit_dirty: true,
        color_offset: 0.0,
        surfacing: false,
        dive: 0,
        since_steer: 10.0,
        jobs: job_tx,
        results: res_rx,
        pending: false,
        punch: 0.0,
        last_kick: 0,
        iter_boost: 1.0,
        started: false,
    })
}

impl Mandelbrot {
    fn new_dive(&mut self, region: usize) {
        self.dive = self.dive.wrapping_add(1);
        let (x, y) = region_point(region, self.dive);
        self.target = Cdd::new(x, y);
        self.surfacing = false;
        self.iter_boost = 1.0;
    }
}

impl Scene for Mandelbrot {
    fn reset(&mut self, _gpu: &Gpu) {
        self.center = Cdd::new(-0.6, 0.0);
        self.scale = START_SCALE;
        self.surfacing = false;
        self.since_steer = 10.0;
        self.started = false;
    }

    fn render(&mut self, gpu: &Gpu, ctx: &mut SceneCtx) {
        let p = ctx.params;
        let a = &ctx.info.audio;
        let dt = (ctx.info.dt as f64).clamp(0.0, 0.1);
        let region = p.choice_index("region");
        if !self.started {
            self.started = true;
            self.new_dive(region);
        }
        let cap = [2500.0, 5000.0, 8000.0][p.choice_index("quality").min(2)];
        let max_iter = {
            let octaves = (START_SCALE / self.scale).log2().max(0.0);
            ((300.0 + 120.0 * octaves) * self.iter_boost).min(cap) as usize
        };
        if a.hits[0] != self.last_kick {
            self.last_kick = a.hits[0];
            self.punch += a.onset[0] as f64 * 0.6;
        }
        let spent = self.punch * (dt * 6.0).min(1.0);
        self.punch -= spent;

        // the dive: octaves per second
        if self.surfacing {
            self.scale *= (dt * 6.0).exp2();
            if self.scale >= START_SCALE * 0.5 {
                self.new_dive(region);
            }
        } else {
            let music = (a.loudness.max(a.bass * 0.85)) as f64;
            let rate = p.f("speed") as f64 * (0.2 + 0.9 * music) + spent;
            self.scale *= (-dt * rate).exp2();
            if self.scale < MIN_SCALE {
                self.surfacing = true;
            }
        }
        // head for the target (in double-double), faster when zoomed out
        let k = (dt * 1.5).min(1.0);
        let d = self.target.minus(self.center);
        self.center = self.center.plus(Cdd { re: d.re.mul_f64(k), im: d.im.mul_f64(k) });
        self.angle += dt * p.f("spin") as f64 * 0.15;

        // steering: ask for a new target (and reference orbit) every so often
        while let Ok(res) = self.results.try_recv() {
            self.pending = false;
            // the slowest points are near the limit: there's detail the iterations can't reach
            let starved = res.deepest > 0.7 * max_iter as f64;
            if starved && (max_iter as f64) < cap {
                self.iter_boost = (self.iter_boost * 1.6).min(16.0);
            }
            match res.target {
                Some(t) if !self.surfacing => {
                    self.target = t;
                    self.reference = res.reference;
                    self.orbit = res.orbit;
                    self.orbit_dirty = true;
                }
                // nothing escapes in view: more iterations first, and only then give up the dive
                None if !self.surfacing && self.scale < 0.05 => {
                    if (max_iter as f64) < cap {
                        self.iter_boost = (self.iter_boost * 2.0).min(16.0);
                    } else {
                        self.surfacing = true;
                    }
                }
                _ => {}
            }
        }
        self.since_steer += dt as f32;
        if !self.pending && self.since_steer > 0.7 && !self.surfacing {
            self.since_steer = 0.0;
            // near the start the region's point leads; deeper down, the detail does
            if self.scale < 0.08 {
                self.pending = self
                    .jobs
                    .send(SteerRequest {
                        center: self.center,
                        scale: self.scale,
                        angle: self.angle,
                        aspect: ctx.size.0 as f64 / ctx.size.1.max(1) as f64,
                        max_iter,
                        reference: self.reference,
                        orbit: self.orbit.clone(),
                    })
                    .is_ok();
            } else if self.reference != self.target {
                // shallow: the region's point is the reference (f64 is plenty up here)
                self.reference = self.target;
                self.orbit = Arc::new(reference_orbit(self.target, max_iter));
                self.orbit_dirty = true;
            }
        }
        // the orbit must be long enough for this depth
        if self.orbit.len() < max_iter.min(MAX_ORBIT - 1) + 1 && self.orbit.len() > max_iter / 2 && !self.pending {
            let escaped = self.orbit.last().is_some_and(|z| z.0 * z.0 + z.1 * z.1 > 1e6);
            if !escaped {
                self.orbit = Arc::new(reference_orbit(self.reference, max_iter));
                self.orbit_dirty = true;
            }
        }
        if self.orbit_dirty {
            let data: Vec<[f32; 2]> = self.orbit.iter().take(MAX_ORBIT).map(|z| [z.0 as f32, z.1 as f32]).collect();
            gpu.queue.write_buffer(&self.orbit_buf, 0, bytemuck::cast_slice(&data));
            self.orbit_dirty = false;
        }

        self.color_offset = (self.color_offset
            + ctx.info.dt * p.f("cycle") * (0.04 + a.treble * 0.3)
            + a.beat * ctx.info.dt * p.f("cycle") * 0.4)
            .rem_euclid(1.0);
        let offset = self.center.minus(self.reference).to_f64();
        let u = MandelUniform {
            offset: [offset.0 as f32, offset.1 as f32],
            scale: self.scale as f32,
            angle: self.angle as f32,
            max_iter: max_iter as u32,
            orbit_len: self.orbit.len().min(MAX_ORBIT) as u32,
            bands: 0.02 + p.f("bands") * 0.12,
            color_offset: self.color_offset,
            relief: p.f("relief"),
            glow: p.f("glow") * (0.5 + a.bass * 1.5),
            pixel: (2.0 * self.scale / ctx.size.1.max(1) as f64) as f32,
            light: self.angle as f32 * 0.5 + 0.8,
        };
        gpu.queue.write_buffer(&self.uniform, 0, bytemuck::bytes_of(&u));
        gpu::fullscreen(
            ctx.encoder,
            "mandelbrot",
            ctx.target,
            &self.pipeline,
            &[&ctx.globals.bind_group, &self.bg],
            Some(wgpu::Color::BLACK),
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn double_double_keeps_32_digits() {
        // (1 + 1e-20)^2 - 1 = 2e-20 + 1e-40: plain f64 says 0
        let x = Dd::new(1.0).plus(Dd::new(1e-20));
        let y = x.times(x).minus(Dd::new(1.0));
        assert!((y.f64() - 2e-20).abs() < 1e-30, "{y:?}");
    }

    #[test]
    fn perturbation_matches_direct_iteration() {
        // a point near the reference, counted both ways
        let c = Cdd::new(-0.7436, 0.1318);
        let orbit = reference_orbit(c, 2000);
        for dc in [(1e-4, 2e-4), (-3e-3, 1e-3), (2e-2, -1e-2)] {
            let (cr, ci) = (c.re.f64() + dc.0, c.im.f64() + dc.1);
            let (mut zr, mut zi) = (0.0f64, 0.0f64);
            let mut direct = None;
            for i in 0..2000 {
                let t = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = t;
                if zr * zr + zi * zi > 1e4 {
                    direct = Some(i);
                    break;
                }
            }
            let pert = escape_count(dc, &orbit, 2000).map(|n| n.floor() as i64);
            match (direct, pert) {
                (Some(d), Some(p)) => assert!((d as i64 - p).abs() <= 1, "{dc:?}: {d} vs {p}"),
                (None, None) => {}
                other => panic!("{dc:?}: {other:?}"),
            }
        }
    }

    #[test]
    fn steering_heads_for_detail() {
        let c = Cdd::new(-0.7436, 0.1318);
        let orbit = Arc::new(reference_orbit(c, 1500));
        let res = steer(SteerRequest {
            center: c,
            scale: 0.01,
            angle: 0.0,
            aspect: 1.6,
            max_iter: 1500,
            reference: c,
            orbit,
        });
        let t = res.target.expect("seahorse valley is full of detail");
        let d = t.minus(c).to_f64();
        assert!(d.0.abs() < 0.02 && d.1.abs() < 0.02);
        assert!(res.orbit.len() > 20);
    }
}
