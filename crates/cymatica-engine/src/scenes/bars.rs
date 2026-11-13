//! Bars: a tribute to cava. cava's own bars, rendered in HD, with optional soft-body physics,
//! falling caps, sparks and a glossy floor.

use crate::globals::{GlobalsGpu, MAX_BARS};
use crate::gpu::{self, Bind, Gpu, HDR_FORMAT};
use crate::params::{choice, float, toggle, ParamDef};
use crate::scene::{scene_source, Scene, SceneCtx};
use bytemuck::{Pod, Zeroable};

pub static PARAMS: &[ParamDef] = &[
    choice("style", "Style", "Bars", &[("classic", "Classic cava"), ("jelly", "Jelly"), ("sparks", "Sparks")], "sparks", "Classic is cava in HD; Jelly adds springy physics; Sparks also throws sparks."),
    choice("layout", "Layout", "Bars", &[("bottom", "Bottom"), ("mirror", "Mirror"), ("radial", "Radial")], "bottom", "Where the bars grow from."),
    choice("color_by", "Colour by", "Bars", &[("height", "Height (like cava)"), ("frequency", "Frequency"), ("time", "Flowing")], "height", "How the theme's colours map onto the bars."),
    float("gap", "Gap", "Bars", 0.0, 0.85, 0.28, "Space between bars."),
    float("height", "Height", "Bars", 0.2, 1.0, 0.85, "Tallest bar, relative to the window."),
    float("roundness", "Roundness", "Bars", 0.0, 1.0, 0.7, "Rounded corners."),
    float("glow", "Brightness", "Bars", 0.2, 3.0, 1.2, "How much light the bars give off."),
    float("reflection", "Floor reflection", "Bars", 0.0, 1.0, 0.45, "Glossy floor under the bars."),
    float("spring", "Stiffness", "Physics", 0.0, 1.0, 0.45, "Jelly: how tightly bars follow the music."),
    float("wobble", "Wobble", "Physics", 0.0, 1.0, 0.5, "Jelly: how much bars overshoot and jiggle."),
    toggle("caps", "Falling caps", "Physics", true, "Peak markers that fall with gravity."),
    float("cap_gravity", "Cap gravity", "Physics", 0.0, 1.0, 0.35, "How fast caps fall."),
    float("sparks", "Sparks", "Physics", 0.0, 1.0, 0.6, "Sparks thrown off rising bars and kicks."),
    float("spark_gravity", "Spark gravity", "Physics", 0.0, 1.0, 0.45, "How heavy the sparks are."),
    float("radial_radius", "Ring size", "Radial", 0.05, 0.6, 0.24, "Inner radius of the radial layout."),
    float("spin", "Spin", "Radial", -1.0, 1.0, 0.08, "Radial layout rotation speed."),
    float("background", "Background", "Bars", 0.0, 1.0, 0.7, "Background gradient and glow."),
];

const MAX_PARTICLES: u32 = 65536;

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct BarsUniform {
    count: u32,
    style: u32,
    arrange: u32,
    color_by: u32,
    gap: f32,
    height: f32,
    roundness: f32,
    glow: f32,
    reflection: f32,
    spring: f32,
    wobble: f32,
    caps: u32,
    cap_gravity: f32,
    sparks: f32,
    spark_gravity: f32,
    radial_radius: f32,
    spin: f32,
    bg: f32,
    burst: f32,
    max_particles: u32,
}

pub struct Bars {
    uniform: wgpu::Buffer,
    sim_bg: wgpu::BindGroup,
    draw_bg: wgpu::BindGroup,
    cs_bars: wgpu::ComputePipeline,
    cs_particles: wgpu::ComputePipeline,
    p_bg: wgpu::RenderPipeline,
    p_bars: wgpu::RenderPipeline,
    p_particles: wgpu::RenderPipeline,
    state: wgpu::Buffer,
    parts: wgpu::Buffer,
    counter: wgpu::Buffer,
    last_kick_hits: u32,
}

pub fn create(gpu: &Gpu, globals: &GlobalsGpu) -> Box<dyn Scene> {
    let common = include_str!("../shaders/scenes/bars_common.wgsl");
    let sim_src = scene_source(&format!("{common}\n{}", include_str!("../shaders/scenes/bars_sim.wgsl")));
    let draw_src = scene_source(&format!("{common}\n{}", include_str!("../shaders/scenes/bars_draw.wgsl")));
    let sim = gpu.shader("bars-sim", sim_src);
    let draw = gpu.shader("bars-draw", draw_src);

    let uniform = gpu.uniform_buffer("bars-u", std::mem::size_of::<BarsUniform>() as u64);
    let state = gpu.storage_buffer("bars-state", (MAX_BARS * 16) as u64);
    let parts = gpu.storage_buffer("bars-parts", MAX_PARTICLES as u64 * 32);
    let counter = gpu.storage_buffer("bars-counter", 16);

    let sim_layout = gpu.layout("bars-sim", &[Bind::Uniform, Bind::StorageRw, Bind::StorageRw, Bind::StorageRw]);
    let draw_layout = gpu.layout("bars-draw", &[Bind::Uniform, Bind::StorageRead, Bind::StorageRead]);
    let sim_bg = gpu.bind_group(
        "bars-sim",
        &sim_layout,
        &[uniform.as_entire_binding(), state.as_entire_binding(), parts.as_entire_binding(), counter.as_entire_binding()],
    );
    let draw_bg = gpu.bind_group(
        "bars-draw",
        &draw_layout,
        &[uniform.as_entire_binding(), state.as_entire_binding(), parts.as_entire_binding()],
    );
    let sim_pl = gpu.pipeline_layout("bars-sim", &[&globals.layout, &sim_layout]);
    let draw_pl = gpu.pipeline_layout("bars-draw", &[&globals.layout, &draw_layout]);
    Box::new(Bars {
        cs_bars: gpu.compute_pipeline("bars", &sim, "cs_bars", &sim_pl),
        cs_particles: gpu.compute_pipeline("bars-particles", &sim, "cs_particles", &sim_pl),
        p_bg: gpu.fullscreen_pipeline("bars-bg", &draw, "fs_bg", &draw_pl, HDR_FORMAT, None),
        p_bars: gpu.render_pipeline(
            "bars",
            &draw,
            "vs_bar",
            "fs_bar",
            &draw_pl,
            HDR_FORMAT,
            Some(gpu::PREMULTIPLIED),
            wgpu::PrimitiveTopology::TriangleList,
        ),
        p_particles: gpu.render_pipeline(
            "bars-particles",
            &draw,
            "vs_particle",
            "fs_particle",
            &draw_pl,
            HDR_FORMAT,
            Some(gpu::ADDITIVE),
            wgpu::PrimitiveTopology::TriangleList,
        ),
        uniform,
        sim_bg,
        draw_bg,
        state,
        parts,
        counter,
        last_kick_hits: 0,
    })
}

impl Scene for Bars {
    fn reset(&mut self, gpu: &Gpu) {
        let zeros = vec![0u8; (MAX_BARS * 16) as usize];
        gpu.queue.write_buffer(&self.state, 0, &zeros);
        let zp = vec![0u8; (MAX_PARTICLES * 32) as usize];
        gpu.queue.write_buffer(&self.parts, 0, &zp);
    }

    fn render(&mut self, gpu: &Gpu, ctx: &mut SceneCtx) {
        let p = ctx.params;
        let a = &ctx.info.audio;
        let count = a.bars.len().clamp(1, MAX_BARS) as u32;
        let burst = if a.hits[0] != self.last_kick_hits { a.onset[0] } else { 0.0 };
        self.last_kick_hits = a.hits[0];
        let u = BarsUniform {
            count,
            style: p.choice_index("style") as u32,
            arrange: p.choice_index("layout") as u32,
            color_by: p.choice_index("color_by") as u32,
            gap: p.f("gap"),
            height: p.f("height"),
            roundness: p.f("roundness"),
            glow: p.f("glow"),
            reflection: if p.choice_index("layout") == 0 { p.f("reflection") } else { 0.0 },
            spring: p.f("spring"),
            wobble: p.f("wobble"),
            caps: p.b("caps") as u32,
            cap_gravity: p.f("cap_gravity"),
            sparks: p.f("sparks"),
            spark_gravity: p.f("spark_gravity"),
            radial_radius: p.f("radial_radius"),
            spin: p.f("spin"),
            bg: p.f("background"),
            burst,
            max_particles: MAX_PARTICLES,
        };
        gpu.queue.write_buffer(&self.uniform, 0, bytemuck::bytes_of(&u));

        {
            let mut cp = ctx.encoder.begin_compute_pass(&wgpu::ComputePassDescriptor { label: Some("bars-sim"), timestamp_writes: None });
            cp.set_bind_group(0, &ctx.globals.bind_group, &[]);
            cp.set_bind_group(1, &self.sim_bg, &[]);
            cp.set_pipeline(&self.cs_bars);
            cp.dispatch_workgroups(count.div_ceil(64), 1, 1);
            cp.set_pipeline(&self.cs_particles);
            cp.dispatch_workgroups(MAX_PARTICLES.div_ceil(256), 1, 1);
        }

        let mut rp = gpu::pass(ctx.encoder, "bars", ctx.target, Some(wgpu::Color::BLACK));
        rp.set_bind_group(0, &ctx.globals.bind_group, &[]);
        rp.set_bind_group(1, &self.draw_bg, &[]);
        rp.set_pipeline(&self.p_bg);
        rp.draw(0..3, 0..1);
        rp.set_pipeline(&self.p_bars);
        if u.reflection > 0.001 {
            rp.draw(0..6, count..2 * count);
            if u.caps != 0 {
                rp.draw(0..6, 3 * count..4 * count);
            }
        }
        if u.arrange == 1 && u.caps != 0 {
            // mirror layout: caps on both sides
            rp.draw(0..6, 3 * count..4 * count);
        }
        rp.draw(0..6, 0..count);
        if u.caps != 0 {
            rp.draw(0..6, 2 * count..3 * count);
        }
        if u.style == 2 {
            rp.set_pipeline(&self.p_particles);
            rp.draw(0..6, 0..MAX_PARTICLES);
        }
    }
}
