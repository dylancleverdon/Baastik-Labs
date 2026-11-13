//! Singularity: a ray-traced Schwarzschild black hole with a turbulent accretion disk, lensed
//! starfield, Doppler beaming, and relativistic jets on big kicks. (A nod to BlackHole, the audio
//! driver: the music falls in, light comes out.)

use crate::globals::GlobalsGpu;
use crate::gpu::{self, Bind, Gpu, HDR_FORMAT};
use crate::params::{choice, float, ParamDef};
use crate::scene::{scene_source, Scene, SceneCtx};
use bytemuck::{Pod, Zeroable};
use glam::Vec3;

pub static PARAMS: &[ParamDef] = &[
    float("disk_size", "Disk size", "Disk", 0.0, 1.0, 0.5, "Outer edge of the accretion disk."),
    float("brightness", "Brightness", "Disk", 0.2, 3.0, 1.0, "How bright the disk burns (the bass heats it)."),
    float("turbulence", "Turbulence", "Disk", 0.0, 1.0, 0.7, "Streaks and clumps in the disk."),
    float("doppler", "Doppler beaming", "Disk", 0.0, 1.0, 0.8, "The side coming toward you is brighter (physics)."),
    choice(
        "color_mode",
        "Colour",
        "Disk",
        &[("physical", "Physical (blackbody)"), ("theme", "Theme colours")],
        "physical",
        "Real temperature colours or the theme.",
    ),
    float("jets", "Jets", "Music", 0.0, 1.0, 0.6, "Relativistic jets on big kicks."),
    float("shock", "Shockwaves", "Music", 0.0, 1.0, 0.7, "Kicks send rings through the disk."),
    float("stars", "Stars", "Space", 0.0, 2.0, 1.0, "Background starfield brightness."),
    float("tilt", "Camera tilt", "Camera", 0.0, 1.0, 0.18, "Edge-on (0) to top-down (1)."),
    float("orbit", "Camera orbit", "Camera", -1.0, 1.0, 0.05, "Circles the black hole."),
    float("zoom", "Zoom", "Camera", 0.0, 1.0, 0.5, "Closer or further."),
    choice(
        "quality",
        "Ray steps",
        "Quality",
        &[("low", "Low"), ("medium", "Medium"), ("high", "High"), ("insane", "Insane")],
        "high",
        "More steps = more accurate light bending.",
    )
    .fixed(),
];

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct BhUniform {
    cam_pos: [f32; 4],
    cam_fwd: [f32; 4],
    cam_right: [f32; 4],
    cam_up: [f32; 4],
    disk_in: f32,
    disk_out: f32,
    brightness: f32,
    turbulence: f32,
    doppler: f32,
    jets: f32,
    stars: f32,
    steps: u32,
    color_mode: u32,
    shock: f32,
    shock_amp: f32,
    disk_time: f32,
    tan_half_fov: f32,
    heat: f32,
    _p: [f32; 2],
}

pub struct Singularity {
    uniform: wgpu::Buffer,
    bg: wgpu::BindGroup,
    pipeline: wgpu::RenderPipeline,
    yaw: f32,
    disk_time: f32,
    shock: f32,
    shock_amp: f32,
    jets: f32,
    last_kick: u32,
}

pub fn create(gpu: &Gpu, globals: &GlobalsGpu) -> Box<dyn Scene> {
    let module = gpu.shader("singularity", scene_source(include_str!("../shaders/scenes/singularity.wgsl")));
    let layout = gpu.layout("singularity", &[Bind::Uniform]);
    let pl = gpu.pipeline_layout("singularity", &[&globals.layout, &layout]);
    let uniform = gpu.uniform_buffer("bh-u", std::mem::size_of::<BhUniform>() as u64);
    Box::new(Singularity {
        bg: gpu.bind_group("singularity", &layout, &[uniform.as_entire_binding()]),
        pipeline: gpu.fullscreen_pipeline("singularity", &module, "fs_bh", &pl, HDR_FORMAT, None),
        uniform,
        yaw: 0.4,
        disk_time: 0.0,
        shock: 100.0,
        shock_amp: 0.0,
        jets: 0.0,
        last_kick: 0,
    })
}

impl Scene for Singularity {
    fn render(&mut self, gpu: &Gpu, ctx: &mut SceneCtx) {
        let p = ctx.params;
        let a = &ctx.info.audio;
        let dt = ctx.info.dt.clamp(0.0, 0.1);
        self.yaw += dt * p.f("orbit") * 0.2;
        self.disk_time += dt * (0.6 + a.mid * 1.5);
        let disk_out = 6.0 + p.f("disk_size") * 14.0;
        if a.hits[0] != self.last_kick {
            self.last_kick = a.hits[0];
            self.shock = 3.0;
            self.shock_amp = p.f("shock") * a.onset[0];
            if a.onset[0] > 0.8 {
                self.jets = self.jets.max(p.f("jets") * a.onset[0]);
            }
        }
        self.shock += dt * 9.0;
        self.shock_amp *= (-dt * 1.5).exp();
        self.jets *= (-dt * 1.8).exp();
        let tilt = 0.04 + p.f("tilt") * 1.4;
        let dist = 14.0 + (1.0 - p.f("zoom")) * 22.0;
        let pos = Vec3::new(self.yaw.sin() * tilt.cos(), tilt.sin(), self.yaw.cos() * tilt.cos()) * dist;
        let fwd = (-pos).normalize();
        let right = fwd.cross(Vec3::Y).normalize();
        let up = right.cross(fwd);
        let steps = [80u32, 130, 200, 300][p.choice_index("quality").min(3)];
        let u = BhUniform {
            cam_pos: [pos.x, pos.y, pos.z, 0.0],
            cam_fwd: [fwd.x, fwd.y, fwd.z, 0.0],
            cam_right: [right.x, right.y, right.z, 0.0],
            cam_up: [up.x, up.y, up.z, 0.0],
            disk_in: 3.0,
            disk_out,
            brightness: p.f("brightness"),
            turbulence: p.f("turbulence"),
            doppler: p.f("doppler"),
            jets: self.jets,
            stars: p.f("stars"),
            steps,
            color_mode: p.choice_index("color_mode") as u32,
            shock: self.shock,
            shock_amp: self.shock_amp,
            disk_time: self.disk_time,
            tan_half_fov: (40f32.to_radians() * 0.5).tan(),
            heat: a.bass * 1.2 + a.loudness * 0.3,
            _p: [0.0; 2],
        };
        gpu.queue.write_buffer(&self.uniform, 0, bytemuck::bytes_of(&u));
        gpu::fullscreen(
            ctx.encoder,
            "singularity",
            ctx.target,
            &self.pipeline,
            &[&ctx.globals.bind_group, &self.bg],
            Some(wgpu::Color::BLACK),
        );
    }
}
