//! Acid: psychedelic feedback. Every frame redraws the last one slightly zoomed, twisted, melted
//! and hue-shifted, then lights new shapes on top (the waveform as a ring, the spectrum, kick
//! shockwaves, hi-hat sparkles), so rainbow trails pour outward forever. Modes: Melt (wobbling
//! feedback), Tunnel (an endless log-polar tunnel) and Fractal (the last frame drawn twice, so it
//! grows copies of itself).

use crate::globals::GlobalsGpu;
use crate::gpu::{self, Bind, Gpu, HDR_FORMAT};
use crate::params::{choice, float, int, ParamDef};
use crate::scene::{scene_source, Scene, SceneCtx};
use bytemuck::{Pod, Zeroable};

pub static PARAMS: &[ParamDef] = &[
    choice(
        "mode",
        "Trip",
        "Acid",
        &[("melt", "Melt"), ("tunnel", "Tunnel"), ("fractal", "Fractal")],
        "melt",
        "Wobbling feedback, an endless tunnel, or a picture that grows copies of itself.",
    ),
    float("zoom", "Zoom", "Motion", 0.0, 1.0, 0.5, "How fast everything pours outward (the bass pumps it)."),
    float("twist", "Twist", "Motion", -1.0, 1.0, 0.3, "Spiral twist (snares kick it)."),
    float("melt", "Melt", "Motion", 0.0, 1.0, 0.5, "How much the picture wobbles and drips."),
    float("trails", "Trails", "Look", 0.0, 1.0, 0.8, "How long the trails last."),
    float("hue_speed", "Rainbow", "Look", 0.0, 1.0, 0.45, "How fast the colours cycle."),
    int("symmetry", "Mirrors", "Look", 0, 12, 0, "Mirror the trails into N segments (0 = off)."),
    float("intensity", "Intensity", "Look", 0.2, 3.0, 1.0, "How bright the new shapes are."),
];

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct AcidUniform {
    mode: u32,
    symmetry: f32,
    zoom: f32,
    twist: f32,
    melt: f32,
    decay: f32,
    hue: f32,
    hue_step: f32,
    intensity: f32,
    time: f32,
    depth: f32,
    shock: f32,
    shock_amp: f32,
    sparkle_seed: f32,
    sparkle: f32,
    _p: f32,
}

type Tex = (wgpu::Texture, wgpu::TextureView);
/// (size, ping-pong feedback textures, their bind groups)
type Targets = ((u32, u32), [Tex; 2], [wgpu::BindGroup; 2]);

pub struct Acid {
    layout: wgpu::BindGroupLayout,
    uniform: wgpu::Buffer,
    sampler: wgpu::Sampler,
    p_feedback: wgpu::RenderPipeline,
    p_show: wgpu::RenderPipeline,
    tex: Option<Targets>,
    cur: usize,
    time: f32,
    depth: f32,
    hue: f32,
    twist_kick: f32,
    shock: f32,
    shock_amp: f32,
    sparkle: f32,
    last_hits: [u32; 3],
}

pub fn create(gpu: &Gpu, globals: &GlobalsGpu) -> Box<dyn Scene> {
    let module = gpu.shader("acid", scene_source(include_str!("../shaders/scenes/acid.wgsl")));
    let layout = gpu.layout("acid", &[Bind::Uniform, Bind::Texture, Bind::Sampler]);
    let pl = gpu.pipeline_layout("acid", &[&globals.layout, &layout]);
    Box::new(Acid {
        p_feedback: gpu.fullscreen_pipeline("acid-feedback", &module, "fs_feedback", &pl, HDR_FORMAT, None),
        p_show: gpu.fullscreen_pipeline("acid-show", &module, "fs_show", &pl, HDR_FORMAT, None),
        uniform: gpu.uniform_buffer("acid-u", std::mem::size_of::<AcidUniform>() as u64),
        sampler: gpu.sampler(wgpu::FilterMode::Linear, wgpu::AddressMode::ClampToEdge),
        layout,
        tex: None,
        cur: 0,
        time: 0.0,
        depth: 0.0,
        hue: 0.0,
        twist_kick: 0.0,
        shock: 10.0,
        shock_amp: 0.0,
        sparkle: 0.0,
        last_hits: [0; 3],
    })
}

impl Scene for Acid {
    fn reset(&mut self, _gpu: &Gpu) {
        self.tex = None;
    }

    fn render(&mut self, gpu: &Gpu, ctx: &mut SceneCtx) {
        let p = ctx.params;
        let a = &ctx.info.audio;
        let dt = ctx.info.dt.clamp(0.0, 0.1);
        if self.tex.as_ref().map(|t| t.0) != Some(ctx.size) {
            let mk = |n: &str| {
                let t = gpu.render_texture(n, ctx.size, HDR_FORMAT);
                let v = t.create_view(&Default::default());
                (t, v)
            };
            let t = [mk("acid-a"), mk("acid-b")];
            let bg = |v: &wgpu::TextureView| {
                gpu.bind_group(
                    "acid",
                    &self.layout,
                    &[
                        self.uniform.as_entire_binding(),
                        wgpu::BindingResource::TextureView(v),
                        wgpu::BindingResource::Sampler(&self.sampler),
                    ],
                )
            };
            let groups = [bg(&t[0].1), bg(&t[1].1)];
            for x in &t {
                gpu::pass(ctx.encoder, "acid-clear", &x.1, Some(wgpu::Color::BLACK));
            }
            self.tex = Some((ctx.size, t, groups));
        }
        // music
        if a.hits[0] != self.last_hits[0] {
            self.shock = 0.0;
            self.shock_amp = a.onset[0];
        }
        if a.hits[1] != self.last_hits[1] {
            self.twist_kick += a.onset[1] * 0.8 * if self.twist_kick >= 0.0 { 1.0 } else { -1.0 };
        }
        if a.hits[2] != self.last_hits[2] {
            self.sparkle = a.onset[2].max(0.4);
        }
        self.last_hits = a.hits;
        self.shock += dt * 0.9;
        self.shock_amp *= (-dt * 2.5).exp();
        self.sparkle *= (-dt * 10.0).exp();
        self.twist_kick *= (-dt * 3.0).exp();
        self.time += dt;
        let zoom = p.f("zoom");
        self.depth += dt * zoom * (0.4 + a.bass * 1.6 + self.shock_amp * 2.0);
        self.hue = (self.hue + dt * p.f("hue_speed") * (0.08 + a.treble * 0.25)).rem_euclid(1.0);
        // per-frame warp amounts follow dt, so the look doesn't depend on the frame rate
        let f60 = dt * 60.0;
        let trails = p.f("trails");
        let u = AcidUniform {
            mode: p.choice_index("mode") as u32,
            symmetry: p.i("symmetry").clamp(0, 12) as f32,
            zoom: (0.004 + zoom * 0.02 * (0.5 + a.bass * 1.5)) * f60,
            twist: (p.f("twist") * 0.012 + self.twist_kick * 0.02) * f60,
            melt: p.f("melt"),
            decay: (0.85 + trails * 0.13).powf(f60),
            hue: self.hue,
            hue_step: p.f("hue_speed") * 0.012 * f60,
            intensity: p.f("intensity"),
            time: self.time % 10000.0,
            depth: self.depth % 10000.0,
            shock: self.shock,
            shock_amp: self.shock_amp,
            sparkle_seed: (ctx.info.frame % 100_000) as f32,
            sparkle: self.sparkle,
            _p: 0.0,
        };
        gpu.queue.write_buffer(&self.uniform, 0, bytemuck::bytes_of(&u));
        let (_, t, groups) = self.tex.as_ref().unwrap();
        let (src, dst) = (self.cur, 1 - self.cur);
        let globals = &ctx.globals.bind_group;
        gpu::fullscreen(ctx.encoder, "acid-feedback", &t[dst].1, &self.p_feedback, &[globals, &groups[src]], None);
        gpu::fullscreen(
            ctx.encoder,
            "acid-show",
            ctx.target,
            &self.p_show,
            &[globals, &groups[dst]],
            Some(wgpu::Color::BLACK),
        );
        self.cur = dst;
    }
}
