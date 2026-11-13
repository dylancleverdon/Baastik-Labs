//! Stardust: a galaxy of particles. Gravity wells orbit each other (bass makes them heavier),
//! treble stirs turbulence, kicks set off supernova shockwaves.

use crate::globals::GlobalsGpu;
use crate::gpu::{self, Bind, Gpu, HDR_FORMAT};
use crate::params::{choice, float, int, ParamDef};
use crate::scene::{scene_source, Scene, SceneCtx};
use bytemuck::{Pod, Zeroable};
use glam::Vec3;

pub static PARAMS: &[ParamDef] = &[
    int("wells", "Gravity wells", "Galaxy", 1, 6, 3, "How many attractors pull the stars."),
    float("gravity", "Gravity", "Galaxy", 0.0, 1.0, 0.5, "Pull of the wells (the bass adds to it)."),
    float("spin", "Spin", "Galaxy", 0.0, 1.0, 0.4, "Galactic rotation."),
    float("turbulence", "Turbulence", "Galaxy", 0.0, 1.0, 0.35, "Swirling motion (the treble adds to it)."),
    float("nova", "Supernovas", "Galaxy", 0.0, 1.0, 0.6, "Shockwaves on kicks."),
    float("damping", "Drag", "Galaxy", 0.0, 1.0, 0.15, "Slows everything down."),
    float("size", "Star size", "Look", 0.2, 3.0, 1.0, "Size of each star."),
    float("brightness", "Brightness", "Look", 0.1, 3.0, 1.0, "Overall star brightness."),
    choice(
        "color_by",
        "Colour by",
        "Look",
        &[("speed", "Speed"), ("star", "Each star"), ("radius", "Distance")],
        "speed",
        "How stars get their colour.",
    ),
    float("orbit", "Camera orbit", "Camera", -1.0, 1.0, 0.15, "Circles the galaxy."),
    float("height", "Camera height", "Camera", 0.0, 1.0, 0.35, "Edge-on (0) to top-down (1)."),
    float("zoom", "Zoom", "Camera", 0.0, 1.0, 0.45, "Closer or further."),
    choice(
        "quality",
        "Stars",
        "Quality",
        &[("low", "250k"), ("medium", "1M"), ("high", "2M"), ("insane", "4M")],
        "medium",
        "Number of stars.",
    )
    .fixed(),
];

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct StarUniform {
    view: [[f32; 4]; 4],
    proj: [[f32; 4]; 4],
    attractors: [[f32; 4]; 6],
    count: u32,
    n_attractors: u32,
    seed: u32,
    color_by: u32,
    gravity: f32,
    turbulence: f32,
    spin: f32,
    damping: f32,
    nova: [f32; 4],
    size: f32,
    brightness: f32,
    noise_scale: f32,
    time: f32,
}

pub struct Stardust {
    sim_layout: wgpu::BindGroupLayout,
    draw_layout: wgpu::BindGroupLayout,
    uniform: wgpu::Buffer,
    cs: wgpu::ComputePipeline,
    p_space: wgpu::RenderPipeline,
    p_star: wgpu::RenderPipeline,
    stars: Option<(usize, u32, wgpu::Buffer, wgpu::BindGroup, wgpu::BindGroup)>,
    yaw: f32,
    nova: [f32; 4],
    last_kick: u32,
    frame: u32,
    phase: f32,
}

pub fn create(gpu: &Gpu, globals: &GlobalsGpu) -> Box<dyn Scene> {
    let common = include_str!("../shaders/scenes/stardust_common.wgsl");
    let sim = gpu.shader(
        "stardust-sim",
        scene_source(&format!("{common}\n{}", include_str!("../shaders/scenes/stardust_sim.wgsl"))),
    );
    let draw = gpu.shader(
        "stardust-draw",
        scene_source(&format!("{common}\n{}", include_str!("../shaders/scenes/stardust_draw.wgsl"))),
    );
    let sim_layout = gpu.layout("stardust-sim", &[Bind::Uniform, Bind::StorageRw]);
    let draw_layout = gpu.layout("stardust-draw", &[Bind::Uniform, Bind::StorageRead]);
    let spl = gpu.pipeline_layout("stardust-sim", &[&globals.layout, &sim_layout]);
    let dpl = gpu.pipeline_layout("stardust-draw", &[&globals.layout, &draw_layout]);
    Box::new(Stardust {
        cs: gpu.compute_pipeline("stardust", &sim, "cs_stars", &spl),
        p_space: gpu.fullscreen_pipeline("stardust-space", &draw, "fs_space", &dpl, HDR_FORMAT, None),
        p_star: gpu.render_pipeline(
            "stardust-star",
            &draw,
            "vs_star",
            "fs_star",
            &dpl,
            HDR_FORMAT,
            Some(gpu::ADDITIVE),
            wgpu::PrimitiveTopology::TriangleList,
        ),
        uniform: gpu.uniform_buffer("stardust-u", std::mem::size_of::<StarUniform>() as u64),
        sim_layout,
        draw_layout,
        stars: None,
        yaw: 0.0,
        nova: [0.0; 4],
        last_kick: 0,
        frame: 0,
        phase: 0.0,
    })
}

fn initial_stars(n: u32) -> Vec<f32> {
    let mut rng = 0xA511_E9B3u32;
    let mut rand = || {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        (rng >> 8) as f32 / (1u32 << 24) as f32
    };
    let mut v = Vec::with_capacity(n as usize * 8);
    for _ in 0..n {
        // a two-armed spiral disc
        let arm = if rand() < 0.5 { 0.0 } else { std::f32::consts::PI };
        let r = 1.0 + (-rand().max(1e-6).ln()) * 5.0;
        let a = arm + r * 0.35 + (rand() - 0.5) * 0.9;
        let y = (rand() - 0.5) * 0.6 * (1.0 + 2.0 / r);
        let pos = [a.cos() * r, y, a.sin() * r];
        let speed = (6.0 / r).sqrt() * 2.2;
        let vel = [-a.sin() * speed, 0.0, a.cos() * speed];
        v.extend_from_slice(&[pos[0], pos[1], pos[2], rand(), vel[0], vel[1], vel[2], 0.6 + rand() * 0.8]);
    }
    v
}

impl Scene for Stardust {
    fn reset(&mut self, _gpu: &Gpu) {
        self.stars = None;
    }

    fn render(&mut self, gpu: &Gpu, ctx: &mut SceneCtx) {
        let p = ctx.params;
        let a = &ctx.info.audio;
        let q = p.choice_index("quality");
        if self.stars.as_ref().map(|s| s.0) != Some(q) {
            let n: u32 = [250_000, 1_000_000, 2_000_000, 4_000_000][q.min(3)];
            let buf = gpu.storage_buffer("stardust-stars", n as u64 * 32);
            gpu.queue.write_buffer(&buf, 0, bytemuck::cast_slice(&initial_stars(n)));
            let sbg = gpu.bind_group(
                "stardust-sim",
                &self.sim_layout,
                &[self.uniform.as_entire_binding(), buf.as_entire_binding()],
            );
            let dbg = gpu.bind_group(
                "stardust-draw",
                &self.draw_layout,
                &[self.uniform.as_entire_binding(), buf.as_entire_binding()],
            );
            self.stars = Some((q, n, buf, sbg, dbg));
        }
        let dt = ctx.info.dt.clamp(0.0, 0.1);
        let t = ctx.info.time;
        self.phase += dt * (0.2 + a.mid * 0.5);
        self.frame = self.frame.wrapping_add(1);
        // attractors orbit on Lissajous paths; each one breathes with a band
        let wells = p.i("wells").clamp(1, 6) as usize;
        let mut attractors = [[0.0f32; 4]; 6];
        for (k, at) in attractors.iter_mut().enumerate().take(wells) {
            let f = k as f32;
            let ph = self.phase * (0.6 + 0.17 * f) + f * 2.1;
            let r = if wells == 1 { 0.0 } else { 4.0 + 2.0 * (f * 1.3).sin() };
            let band = a.bands[(k * 7 / wells).min(6)];
            *at = [ph.cos() * r, (ph * 0.7).sin() * 1.5, (ph * 1.1).sin() * r, 6.0 + 18.0 * band];
        }
        // supernova on kick hits
        if a.hits[0] != self.last_kick {
            let k = (self.frame as usize) % wells;
            self.nova = [attractors[k][0], attractors[k][1], attractors[k][2], p.f("nova") * a.onset[0]];
            self.last_kick = a.hits[0];
        }
        self.nova[3] *= (-dt * 6.0).exp();

        self.yaw += dt * p.f("orbit") * 0.3;
        let pitch = 0.05 + p.f("height") * 1.4;
        let dist = 18.0 + (1.0 - p.f("zoom")) * 30.0;
        let eye = Vec3::new(self.yaw.sin() * pitch.cos(), pitch.sin(), self.yaw.cos() * pitch.cos()) * dist;
        let view = crate::camera::look_at_rh(eye, Vec3::ZERO, Vec3::Y);
        let aspect = ctx.size.0 as f32 / ctx.size.1 as f32;
        let proj = crate::camera::perspective_rh(45f32.to_radians(), aspect, 0.1, 500.0);
        let (_, n, _, sbg, dbg) = self.stars.as_ref().unwrap();
        let u = StarUniform {
            view: view.to_cols_array_2d(),
            proj: proj.to_cols_array_2d(),
            attractors,
            count: *n,
            n_attractors: wells as u32,
            seed: self.frame.wrapping_mul(747796405),
            color_by: p.choice_index("color_by") as u32,
            gravity: p.f("gravity") * (0.5 + a.bass * 1.5),
            turbulence: p.f("turbulence") * (0.3 + a.treble * 2.0),
            spin: p.f("spin") * 2.0,
            damping: p.f("damping") * 0.8,
            nova: self.nova,
            size: p.f("size"),
            brightness: p.f("brightness") * (0.7 + a.loudness * 0.6) * (250_000.0 / *n as f32).powf(0.6),
            noise_scale: 0.12,
            time: t,
        };
        gpu.queue.write_buffer(&self.uniform, 0, bytemuck::bytes_of(&u));
        {
            let mut cp = ctx
                .encoder
                .begin_compute_pass(&wgpu::ComputePassDescriptor { label: Some("stardust"), timestamp_writes: None });
            cp.set_pipeline(&self.cs);
            cp.set_bind_group(0, &ctx.globals.bind_group, &[]);
            cp.set_bind_group(1, sbg, &[]);
            cp.dispatch_workgroups(n.div_ceil(256), 1, 1);
        }
        let mut rp = gpu::pass(ctx.encoder, "stardust", ctx.target, Some(wgpu::Color::BLACK));
        rp.set_bind_group(0, &ctx.globals.bind_group, &[]);
        rp.set_bind_group(1, dbg, &[]);
        rp.set_pipeline(&self.p_space);
        rp.draw(0..3, 0..1);
        rp.set_pipeline(&self.p_star);
        rp.draw(0..6, 0..*n);
    }
}
