//! Kaleidoscope: stained glass, petals and spectrum lace folded into mirror segments, streaming
//! outward forever. Kicks punch the flow outward, the mids turn the mirrors, every band lights
//! its own shards.

use crate::globals::GlobalsGpu;
use crate::gpu::{self, Bind, Gpu, HDR_FORMAT};
use crate::params::{choice, float, int, ParamDef};
use crate::scene::{scene_source, Scene, SceneCtx};
use bytemuck::{Pod, Zeroable};

pub static PARAMS: &[ParamDef] = &[
    int("segments", "Mirrors", "Kaleidoscope", 3, 24, 8, "How many mirror segments."),
    choice(
        "style",
        "Pattern",
        "Kaleidoscope",
        &[("glass", "Stained glass"), ("petals", "Petals"), ("lace", "Spectrum lace"), ("mix", "Everything")],
        "mix",
        "What the mirrors reflect.",
    ),
    float("flow", "Flow", "Motion", 0.0, 1.0, 0.4, "How fast the pattern streams outward."),
    float("spin", "Spin", "Motion", -1.0, 1.0, 0.15, "How fast the mirrors turn (the mids push it)."),
    float("punch", "Kick punch", "Music", 0.0, 1.0, 0.6, "Kicks shove the pattern outward."),
    float("detail", "Detail", "Look", 0.0, 1.0, 0.55, "Smaller, more intricate pieces."),
    float("brightness", "Brightness", "Look", 0.2, 3.0, 1.0, "How brightly the glass glows."),
    float("core", "Core glow", "Look", 0.0, 1.0, 0.5, "Light at the centre (the bass swells it)."),
];

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct KaleidoUniform {
    segments: f32,
    style: u32,
    flow: f32,
    spin: f32,
    detail: f32,
    brightness: f32,
    core: f32,
    shard_phase: f32,
    petal_phase: f32,
    _p: [f32; 3],
}

pub struct Kaleido {
    uniform: wgpu::Buffer,
    bg: wgpu::BindGroup,
    pipeline: wgpu::RenderPipeline,
    flow: f32,
    spin: f32,
    shard_phase: f32,
    petal_phase: f32,
    punch: f32,
    last_kick: u32,
}

pub fn create(gpu: &Gpu, globals: &GlobalsGpu) -> Box<dyn Scene> {
    let module = gpu.shader("kaleido", scene_source(include_str!("../shaders/scenes/kaleido.wgsl")));
    let layout = gpu.layout("kaleido", &[Bind::Uniform]);
    let pl = gpu.pipeline_layout("kaleido", &[&globals.layout, &layout]);
    let uniform = gpu.uniform_buffer("kaleido-u", std::mem::size_of::<KaleidoUniform>() as u64);
    Box::new(Kaleido {
        bg: gpu.bind_group("kaleido", &layout, &[uniform.as_entire_binding()]),
        pipeline: gpu.fullscreen_pipeline("kaleido", &module, "fs_kaleido", &pl, HDR_FORMAT, None),
        uniform,
        flow: 0.0,
        spin: 0.0,
        shard_phase: 0.0,
        petal_phase: 0.0,
        punch: 0.0,
        last_kick: 0,
    })
}

impl Scene for Kaleido {
    fn reset(&mut self, _gpu: &Gpu) {
        self.flow = 0.0;
        self.punch = 0.0;
    }

    fn render(&mut self, gpu: &Gpu, ctx: &mut SceneCtx) {
        let p = ctx.params;
        let a = &ctx.info.audio;
        let dt = ctx.info.dt.clamp(0.0, 0.1);
        if a.hits[0] != self.last_kick {
            self.last_kick = a.hits[0];
            self.punch += p.f("punch") * a.onset[0] * 0.35;
        }
        // the punch is spent over a few frames, so the pattern lurches outward and settles
        let spent = self.punch * (dt * 8.0).min(1.0);
        self.punch -= spent;
        let flow = p.f("flow");
        self.flow = (self.flow + dt * flow * (0.12 + a.bass * 0.35) + spent).rem_euclid(3000.0);
        self.spin = (self.spin + dt * p.f("spin") * (0.15 + a.mid * 0.8)).rem_euclid(std::f32::consts::TAU);
        self.shard_phase += dt * (0.15 + a.treble * 0.8);
        self.petal_phase += dt * (0.2 + a.mid * 0.6);
        let u = KaleidoUniform {
            segments: p.i("segments").clamp(3, 24) as f32,
            style: p.choice_index("style") as u32,
            flow: self.flow,
            spin: self.spin,
            detail: p.f("detail"),
            brightness: p.f("brightness"),
            core: p.f("core"),
            shard_phase: self.shard_phase % 1000.0,
            petal_phase: self.petal_phase % 1000.0,
            _p: [0.0; 3],
        };
        gpu.queue.write_buffer(&self.uniform, 0, bytemuck::bytes_of(&u));
        gpu::fullscreen(
            ctx.encoder,
            "kaleido",
            ctx.target,
            &self.pipeline,
            &[&ctx.globals.bind_group, &self.bg],
            Some(wgpu::Color::BLACK),
        );
    }
}
