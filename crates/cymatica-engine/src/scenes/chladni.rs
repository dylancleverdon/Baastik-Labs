//! Chladni: cymatics. Sand on a vibrating plate settles on the nodal lines of the standing wave;
//! the music's dominant note picks the mode, so figures re-form as the harmony moves.

use crate::globals::GlobalsGpu;
use crate::gpu::{self, Bind, Gpu, HDR_FORMAT};
use crate::params::{choice, float, toggle, ParamDef};
use crate::scene::{scene_source, Scene, SceneCtx};
use bytemuck::{Pod, Zeroable};

pub static PARAMS: &[ParamDef] = &[
    choice("plate", "Plate", "Plate", &[("square", "Square"), ("circle", "Round")], "square", "Plate shape."),
    choice("follow", "Mode follows", "Plate", &[("note", "The melody (dominant note)"), ("beat", "The beat"), ("bass", "Bass energy")], "note", "What picks the vibration pattern."),
    float("complexity", "Complexity", "Plate", 0.0, 1.0, 0.45, "How intricate the figures get."),
    float("vibration", "Vibration", "Sand", 0.0, 1.0, 0.55, "How hard the plate shakes the sand."),
    float("drift", "Settling", "Sand", 0.0, 1.0, 0.5, "How quickly sand finds the still lines."),
    float("grain", "Grain size", "Sand", 0.3, 3.0, 0.9, "Size of each grain."),
    float("glow", "Brightness", "Sand", 0.2, 3.0, 1.0, "How bright the sand glows."),
    choice("color_by", "Colour by", "Sand", &[("speed", "Movement"), ("grain", "Each grain"), ("radius", "Distance from centre")], "speed", "How the theme colours the sand."),
    toggle("show_plate", "Show plate", "Plate", true, "Draw the metal plate."),
    choice("quality", "Grains", "Quality", &[("low", "250k"), ("medium", "500k"), ("high", "1M"), ("insane", "2M")], "high", "Number of sand grains.").fixed(),
];

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct ChUniform {
    mode_a: [f32; 4],
    blend: f32,
    plate: u32,
    count: u32,
    vibration: f32,
    drift: f32,
    grain: f32,
    glow: f32,
    color_by: u32,
    seed: u32,
    show_plate: u32,
    _p: [u32; 2],
}

fn modes(circle: bool) -> Vec<[f32; 2]> {
    let mut v = Vec::new();
    if circle {
        for n in 0..9 {
            for m in 1..7 {
                v.push([n as f32, m as f32]);
            }
        }
        v.sort_by(|a, b| (a[0] + 2.0 * a[1]).partial_cmp(&(b[0] + 2.0 * b[1])).unwrap());
    } else {
        for n in 1..14 {
            for m in 1..n {
                if (n + m) % 2 == 1 || n - m > 1 {
                    v.push([n as f32, m as f32]);
                }
            }
        }
        v.sort_by(|a, b| (a[0] * a[0] + a[1] * a[1]).partial_cmp(&(b[0] * b[0] + b[1] * b[1])).unwrap());
    }
    v
}

pub struct Chladni {
    sim_layout: wgpu::BindGroupLayout,
    draw_layout: wgpu::BindGroupLayout,
    uniform: wgpu::Buffer,
    cs: wgpu::ComputePipeline,
    p_plate: wgpu::RenderPipeline,
    p_grain: wgpu::RenderPipeline,
    grains: Option<(usize, u32, wgpu::Buffer, wgpu::BindGroup, wgpu::BindGroup)>,
    current: [f32; 2],
    previous: [f32; 2],
    blend: f32,
    candidate: (usize, f32),
    last_beat: u32,
    frame: u32,
    plate: usize,
}

pub fn create(gpu: &Gpu, globals: &GlobalsGpu) -> Box<dyn Scene> {
    let common = include_str!("../shaders/scenes/chladni_common.wgsl");
    let sim = gpu.shader("chladni-sim", scene_source(&format!("{common}\n{}", include_str!("../shaders/scenes/chladni_sim.wgsl"))));
    let draw = gpu.shader("chladni-draw", scene_source(&format!("{common}\n{}", include_str!("../shaders/scenes/chladni_draw.wgsl"))));
    let sim_layout = gpu.layout("chladni-sim", &[Bind::Uniform, Bind::StorageRw]);
    let draw_layout = gpu.layout("chladni-draw", &[Bind::Uniform, Bind::StorageRead]);
    let spl = gpu.pipeline_layout("chladni-sim", &[&globals.layout, &sim_layout]);
    let dpl = gpu.pipeline_layout("chladni-draw", &[&globals.layout, &draw_layout]);
    Box::new(Chladni {
        cs: gpu.compute_pipeline("chladni", &sim, "cs_grains", &spl),
        p_plate: gpu.fullscreen_pipeline("chladni-plate", &draw, "fs_plate", &dpl, HDR_FORMAT, None),
        p_grain: gpu.render_pipeline("chladni-grain", &draw, "vs_grain", "fs_grain", &dpl, HDR_FORMAT, Some(gpu::ADDITIVE), wgpu::PrimitiveTopology::TriangleList),
        uniform: gpu.uniform_buffer("chladni-u", std::mem::size_of::<ChUniform>() as u64),
        sim_layout,
        draw_layout,
        grains: None,
        current: [3.0, 2.0],
        previous: [3.0, 2.0],
        blend: 1.0,
        candidate: (0, 0.0),
        last_beat: 0,
        frame: 0,
        plate: 0,
    })
}

impl Scene for Chladni {
    fn reset(&mut self, _gpu: &Gpu) {
        self.grains = None;
    }

    fn render(&mut self, gpu: &Gpu, ctx: &mut SceneCtx) {
        let p = ctx.params;
        let a = &ctx.info.audio;
        let q = p.choice_index("quality");
        if self.grains.as_ref().map(|g| g.0) != Some(q) {
            let n: u32 = [250_000, 500_000, 1_000_000, 2_000_000][q.min(3)];
            let mut rng = 0x9E37_79B9u32;
            let mut data = Vec::with_capacity(n as usize * 4);
            for _ in 0..n {
                for _ in 0..2 {
                    rng ^= rng << 13;
                    rng ^= rng >> 17;
                    rng ^= rng << 5;
                    data.push((rng >> 8) as f32 / (1u32 << 24) as f32);
                }
                data.push(0.0);
                data.push(0.0);
            }
            let buf = gpu.storage_buffer("chladni-grains", n as u64 * 16);
            gpu.queue.write_buffer(&buf, 0, bytemuck::cast_slice(&data));
            let sbg = gpu.bind_group("chladni-sim", &self.sim_layout, &[self.uniform.as_entire_binding(), buf.as_entire_binding()]);
            let dbg = gpu.bind_group("chladni-draw", &self.draw_layout, &[self.uniform.as_entire_binding(), buf.as_entire_binding()]);
            self.grains = Some((q, n, buf, sbg, dbg));
        }
        let plate = p.choice_index("plate");
        let table = modes(plate == 1);
        let max_idx = (4 + (p.f("complexity") * (table.len() as f32 - 4.0)) as usize).min(table.len());
        // pick the mode the music asks for
        let want = match p.choice_index("follow") {
            1 => (a.beat_count as usize / 4) % max_idx,
            2 => ((a.bass * 0.999) * max_idx as f32) as usize,
            _ => (a.note as usize * 5 + (a.centroid * 3.0) as usize) % max_idx,
        };
        let dt = ctx.info.dt.clamp(0.0, 0.1);
        if self.candidate.0 == want {
            self.candidate.1 += dt;
        } else {
            self.candidate = (want, 0.0);
        }
        let target = table[want.min(table.len() - 1)];
        let stable = self.candidate.1 > 0.35 && (a.note_strength > 0.15 || p.choice_index("follow") != 0);
        if (stable && target != self.current && self.blend >= 1.0) || plate != self.plate {
            self.previous = if plate != self.plate { target } else { self.current };
            self.current = target;
            self.blend = 0.0;
            self.plate = plate;
        }
        self.blend = (self.blend + dt / 0.9).min(1.0);
        self.frame = self.frame.wrapping_add(1);
        let (_, n, _, sbg, dbg) = self.grains.as_ref().unwrap();
        let s = self.blend * self.blend * (3.0 - 2.0 * self.blend);
        let u = ChUniform {
            mode_a: [self.previous[0], self.previous[1], self.current[0], self.current[1]],
            blend: s,
            plate: plate as u32,
            count: *n,
            vibration: p.f("vibration"),
            drift: p.f("drift"),
            grain: p.f("grain"),
            // brightness per grain falls with the grain count so dense plates don't blow out
            glow: p.f("glow") * (250_000.0 / *n as f32).powf(0.75),
            color_by: p.choice_index("color_by") as u32,
            seed: self.frame.wrapping_mul(2654435761),
            show_plate: p.b("show_plate") as u32,
            _p: [0; 2],
        };
        gpu.queue.write_buffer(&self.uniform, 0, bytemuck::bytes_of(&u));
        {
            let mut cp = ctx.encoder.begin_compute_pass(&wgpu::ComputePassDescriptor { label: Some("chladni"), timestamp_writes: None });
            cp.set_pipeline(&self.cs);
            cp.set_bind_group(0, &ctx.globals.bind_group, &[]);
            cp.set_bind_group(1, sbg, &[]);
            cp.dispatch_workgroups(n.div_ceil(256), 1, 1);
        }
        let mut rp = gpu::pass(ctx.encoder, "chladni", ctx.target, Some(wgpu::Color::BLACK));
        rp.set_bind_group(0, &ctx.globals.bind_group, &[]);
        rp.set_bind_group(1, dbg, &[]);
        rp.set_pipeline(&self.p_plate);
        rp.draw(0..3, 0..1);
        rp.set_pipeline(&self.p_grain);
        rp.draw(0..6, 0..*n);
        let _ = self.last_beat;
    }
}
