//! Phosphor: an oscilloscope drawn by a simulated CRT beam (dwell-time brightness, phosphor
//! persistence, curved glass).

use crate::globals::GlobalsGpu;
use crate::gpu::{self, Bind, Gpu, HDR_FORMAT};
use crate::params::{choice, float, toggle, ParamDef};
use crate::scene::{scene_source, Scene, SceneCtx};
use bytemuck::{Pod, Zeroable};

pub static PARAMS: &[ParamDef] = &[
    choice("mode", "Display", "Scope", &[("xy", "Lissajous (X/Y)"), ("gonio", "Goniometer"), ("scope", "Waveform"), ("polar", "Polar")], "xy", "How the stereo signal is drawn."),
    choice("phosphor", "Phosphor", "Scope", &[("green", "P1 green"), ("amber", "P3 amber"), ("blue", "P11 blue"), ("white", "P4 white"), ("theme", "Theme colours")], "green", "The tube's phosphor colour."),
    float("gain", "Zoom", "Scope", 0.2, 4.0, 1.2, "Signal size."),
    toggle("auto_gain", "Auto zoom", "Scope", true, "Keep the trace filling the screen."),
    float("persistence", "Persistence", "Tube", 0.0, 0.98, 0.82, "How long the phosphor keeps glowing."),
    float("beam", "Beam width", "Tube", 0.5, 4.0, 1.5, "Electron beam thickness (px)."),
    float("intensity", "Intensity", "Tube", 0.1, 3.0, 0.6, "Beam brightness."),
    float("curvature", "Glass curve", "Tube", 0.0, 1.0, 0.35, "CRT screen bulge."),
    toggle("graticule", "Graticule", "Tube", true, "Measurement grid."),
];

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct PhUniform {
    mode: u32,
    count: u32,
    start: u32,
    graticule: u32,
    gain: f32,
    beam: f32,
    intensity: f32,
    persistence: f32,
    curvature: f32,
    color_mode: u32,
    time: f32,
    _p: f32,
}

type Tex = (wgpu::Texture, wgpu::TextureView);

pub struct Phosphor {
    layout: wgpu::BindGroupLayout,
    uniform: wgpu::Buffer,
    sampler: wgpu::Sampler,
    p_decay: wgpu::RenderPipeline,
    p_beam: wgpu::RenderPipeline,
    p_screen: wgpu::RenderPipeline,
    tex: Option<((u32, u32), [Tex; 2], [wgpu::BindGroup; 2])>,
    cur: usize,
    auto_gain: f32,
}

pub fn create(gpu: &Gpu, globals: &GlobalsGpu) -> Box<dyn Scene> {
    let module = gpu.shader("phosphor", scene_source(include_str!("../shaders/scenes/phosphor.wgsl")));
    let layout = gpu.layout("phosphor", &[Bind::Uniform, Bind::Texture, Bind::Sampler]);
    let pl = gpu.pipeline_layout("phosphor", &[&globals.layout, &layout]);
    Box::new(Phosphor {
        p_decay: gpu.fullscreen_pipeline("ph-decay", &module, "fs_decay", &pl, HDR_FORMAT, None),
        p_beam: gpu.render_pipeline("ph-beam", &module, "vs_beam", "fs_beam", &pl, HDR_FORMAT, Some(gpu::ADDITIVE), wgpu::PrimitiveTopology::TriangleList),
        p_screen: gpu.fullscreen_pipeline("ph-screen", &module, "fs_screen", &pl, HDR_FORMAT, None),
        uniform: gpu.uniform_buffer("ph-u", std::mem::size_of::<PhUniform>() as u64),
        sampler: gpu.sampler(wgpu::FilterMode::Linear, wgpu::AddressMode::ClampToEdge),
        layout,
        tex: None,
        cur: 0,
        auto_gain: 1.0,
    })
}

impl Scene for Phosphor {
    fn reset(&mut self, _gpu: &Gpu) {
        self.tex = None;
    }

    fn render(&mut self, gpu: &Gpu, ctx: &mut SceneCtx) {
        let p = ctx.params;
        let a = &ctx.info.audio;
        if self.tex.as_ref().map(|t| t.0) != Some(ctx.size) {
            let mk = |n: &str| {
                let t = gpu.render_texture(n, ctx.size, HDR_FORMAT);
                let v = t.create_view(&Default::default());
                (t, v)
            };
            let t = [mk("ph-a"), mk("ph-b")];
            let bg = |v: &wgpu::TextureView| {
                gpu.bind_group("phosphor", &self.layout, &[self.uniform.as_entire_binding(), wgpu::BindingResource::TextureView(v), wgpu::BindingResource::Sampler(&self.sampler)])
            };
            let groups = [bg(&t[0].1), bg(&t[1].1)];
            for x in &t {
                gpu::pass(ctx.encoder, "ph-clear", &x.1, Some(wgpu::Color::BLACK));
            }
            self.tex = Some((ctx.size, t, groups));
        }
        let mode = p.choice_index("mode") as u32;
        let wave = &a.waveform;
        let n = wave.len();
        // samples since the last frame (plus a little overlap), the beam's path this frame
        let per_frame = ((ctx.info.dt.max(1.0 / 240.0) * 48000.0) as usize + 64).clamp(256, n.max(2) - 1);
        let mut start = n.saturating_sub(per_frame + 1);
        let mut count = per_frame;
        if mode == 2 {
            // waveform: trigger on a rising zero crossing so the trace stands still
            count = 1024.min(n.saturating_sub(2));
            let search_end = n.saturating_sub(count + 1);
            let mut trig = search_end;
            for i in (1..search_end).rev() {
                let m0 = wave[i - 1][0] + wave[i - 1][1];
                let m1 = wave[i][0] + wave[i][1];
                if m0 <= 0.0 && m1 > 0.0 {
                    trig = i;
                    break;
                }
            }
            start = trig;
        }
        let peak = wave.iter().rev().take(2048).fold(0.0f32, |m, s| m.max(s[0].abs()).max(s[1].abs()));
        let target = if peak > 1e-3 { (0.8 / peak).clamp(0.5, 12.0) } else { self.auto_gain };
        self.auto_gain += (target - self.auto_gain) * (ctx.info.dt * 2.0).min(1.0);
        let gain = p.f("gain") * if p.b("auto_gain") { self.auto_gain } else { 1.0 };
        let u = PhUniform {
            mode,
            count: count as u32,
            start: start as u32,
            graticule: p.b("graticule") as u32,
            gain,
            beam: p.f("beam"),
            intensity: p.f("intensity"),
            persistence: p.f("persistence").powf((ctx.info.dt * 60.0).max(0.0)),
            curvature: p.f("curvature"),
            color_mode: p.choice_index("phosphor") as u32,
            time: ctx.info.time,
            _p: 0.0,
        };
        gpu.queue.write_buffer(&self.uniform, 0, bytemuck::bytes_of(&u));
        let (_, t, groups) = self.tex.as_ref().unwrap();
        let (src, dst) = (self.cur, 1 - self.cur);
        let globals = &ctx.globals.bind_group;
        gpu::fullscreen(ctx.encoder, "ph-decay", &t[dst].1, &self.p_decay, &[globals, &groups[src]], None);
        {
            let mut rp = gpu::pass(ctx.encoder, "ph-beam", &t[dst].1, None);
            rp.set_pipeline(&self.p_beam);
            rp.set_bind_group(0, globals, &[]);
            rp.set_bind_group(1, &groups[src], &[]);
            if !a.silent || a.peak > 0.0 {
                rp.draw(0..6, 0..(count.saturating_sub(1)) as u32);
            }
        }
        gpu::fullscreen(ctx.encoder, "ph-screen", ctx.target, &self.p_screen, &[globals, &groups[dst]], Some(wgpu::Color::BLACK));
        self.cur = dst;
    }
}
