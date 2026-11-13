//! Ripples: a water surface (wave equation) in a pool. The music drops pebbles into it; the
//! picture comes from real refraction, reflections and caustics.

use crate::globals::GlobalsGpu;
use crate::gpu::{self, Bind, Gpu, HDR_FORMAT};
use crate::params::{choice, float, ParamDef};
use crate::scene::{scene_source, Scene, SceneCtx};
use bytemuck::{Pod, Zeroable};

pub static PARAMS: &[ParamDef] = &[
    choice("mode", "Drops", "Water", &[("rings", "Rings (bass in the middle)"), ("rain", "Rain"), ("drums", "Drums only"), ("faraday", "Faraday waves")], "rings", "How the music disturbs the water."),
    float("drop_rate", "Drop rate", "Water", 0.0, 1.0, 0.45, "How often drops fall."),
    float("drop_size", "Drop size", "Water", 0.0, 1.0, 0.5, "How big drops are."),
    float("waves", "Wave height", "Water", 0.0, 1.0, 0.5, "How tall the waves get."),
    float("damping", "Calmness", "Water", 0.0, 1.0, 0.4, "How quickly waves die out."),
    float("depth", "Depth", "Pool", 0.1, 1.0, 0.45, "Pool depth."),
    choice("floor", "Floor", "Pool", &[("tiles", "Tiles"), ("sand", "Sand"), ("neon", "Neon grid")], "tiles", "What's at the bottom of the pool."),
    float("caustics", "Caustics", "Light", 0.0, 2.0, 1.0, "Dancing light on the floor."),
    float("reflections", "Reflections", "Light", 0.0, 1.0, 0.85, "Sky reflections on the surface."),
    float("tint", "Water colour", "Light", 0.0, 1.0, 0.45, "How strongly the water tints what's below."),
    float("tilt", "Camera height", "Camera", 0.0, 1.0, 0.6, "Low angle (0) to straight down (1)."),
    float("spin", "Camera orbit", "Camera", -1.0, 1.0, 0.06, "Slowly circles the pool."),
    choice("quality", "Simulation detail", "Quality", &[("low", "Low"), ("medium", "Medium"), ("high", "High")], "high", "Water grid resolution.").fixed(),
];

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct RipUniform {
    sim_size: [f32; 2],
    yaw: f32,
    tilt: f32,
    depth: f32,
    caustics: f32,
    reflections: f32,
    tint: f32,
    damping: f32,
    drop_count: u32,
    floor_style: u32,
    time: f32,
    faraday: f32,
    faraday_freq: f32,
    wave_gain: f32,
    _pad: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable, Default)]
struct Drop {
    pos_r_s: [f32; 4],
}

const MAX_DROPS: usize = 48;
const CAUSTIC_SIZE: u32 = 1024;
const CAUSTIC_GRID: u32 = 200;
const WATER: wgpu::TextureFormat = wgpu::TextureFormat::Rgba32Float;

struct Grid {
    size: u32,
    _water: [wgpu::Texture; 2],
    water_v: [wgpu::TextureView; 2],
    step_bg: [wgpu::BindGroup; 2],
    caustic_bg: [wgpu::BindGroup; 2],
    render_bg: [wgpu::BindGroup; 2],
}

pub struct Ripples {
    layout: wgpu::BindGroupLayout,
    uniform: wgpu::Buffer,
    drops: wgpu::Buffer,
    _caust: wgpu::Texture,
    caust_v: wgpu::TextureView,
    dummy: wgpu::TextureView,
    p_step: wgpu::RenderPipeline,
    p_caustics: wgpu::RenderPipeline,
    p_render: wgpu::RenderPipeline,
    grid: Option<Grid>,
    cur: usize,
    rng: u32,
    last_hits: [u32; 3],
    yaw: f32,
    clear_pending: bool,
    pending_drops: Vec<Drop>,
}

pub fn create(gpu: &Gpu, globals: &GlobalsGpu) -> Box<dyn Scene> {
    let module = gpu.shader("ripples", scene_source(include_str!("../shaders/scenes/ripples.wgsl")));
    let layout = gpu.layout(
        "ripples",
        &[Bind::Uniform, Bind::StorageRead, Bind::TextureUnfilterable, Bind::TextureUnfilterable],
    );
    let pl = gpu.pipeline_layout("ripples", &[&globals.layout, &layout]);
    let caust = gpu.render_texture("caustics", (CAUSTIC_SIZE, CAUSTIC_SIZE), wgpu::TextureFormat::R16Float);
    let caust_v = caust.create_view(&Default::default());
    let dummy = gpu.render_texture("rip-dummy", (1, 1), wgpu::TextureFormat::R16Float).create_view(&Default::default());
    Box::new(Ripples {
        p_step: gpu.fullscreen_pipeline("rip-step", &module, "fs_step", &pl, WATER, None),
        p_caustics: gpu.render_pipeline(
            "rip-caustics",
            &module,
            "vs_caustics",
            "fs_caustics",
            &pl,
            wgpu::TextureFormat::R16Float,
            Some(gpu::ADDITIVE),
            wgpu::PrimitiveTopology::TriangleList,
        ),
        p_render: gpu.fullscreen_pipeline("rip-render", &module, "fs_render", &pl, HDR_FORMAT, None),
        uniform: gpu.uniform_buffer("rip-u", std::mem::size_of::<RipUniform>() as u64),
        drops: gpu.storage_buffer("rip-drops", (MAX_DROPS * 16) as u64),
        layout,
        _caust: caust,
        caust_v,
        dummy,
        grid: None,
        cur: 0,
        rng: 0x2545F491,
        last_hits: [0; 3],
        yaw: 0.6,
        clear_pending: true,
        pending_drops: Vec::new(),
    })
}

impl Ripples {
    fn rand(&mut self) -> f32 {
        self.rng ^= self.rng << 13;
        self.rng ^= self.rng >> 17;
        self.rng ^= self.rng << 5;
        (self.rng >> 8) as f32 / (1u32 << 24) as f32
    }

    fn build(&self, gpu: &Gpu, size: u32) -> Grid {
        let w0 = gpu.render_texture("water0", (size, size), WATER);
        let w1 = gpu.render_texture("water1", (size, size), WATER);
        let v0 = w0.create_view(&Default::default());
        let v1 = w1.create_view(&Default::default());
        let bg = |w: &wgpu::TextureView, c: &wgpu::TextureView| {
            gpu.bind_group(
                "ripples",
                &self.layout,
                &[
                    self.uniform.as_entire_binding(),
                    self.drops.as_entire_binding(),
                    wgpu::BindingResource::TextureView(w),
                    wgpu::BindingResource::TextureView(c),
                ],
            )
        };
        Grid {
            size,
            step_bg: [bg(&v0, &self.dummy), bg(&v1, &self.dummy)],
            caustic_bg: [bg(&v0, &self.dummy), bg(&v1, &self.dummy)],
            render_bg: [bg(&v0, &self.caust_v), bg(&v1, &self.caust_v)],
            _water: [w0, w1],
            water_v: [v0, v1],
        }
    }

    fn add_drop(&mut self, x: f32, z: f32, radius: f32, strength: f32) {
        if self.pending_drops.len() < MAX_DROPS {
            self.pending_drops.push(Drop { pos_r_s: [x, z, radius.max(0.004), strength] });
        }
    }

    fn music_drops(&mut self, ctx: &SceneCtx) {
        let p = ctx.params;
        let a = &ctx.info.audio;
        let dt = ctx.info.dt.clamp(0.0, 0.1);
        let mode = p.choice_index("mode");
        let rate = p.f("drop_rate");
        let size = p.f("drop_size");
        let gain = 0.3 + p.f("waves");
        let n = a.bands.len();
        match mode {
            0 | 1 => {
                for (i, &e) in a.bands.iter().enumerate() {
                    let fi = i as f32 / (n - 1) as f32;
                    let chance = rate * e * e * 9.0 * dt;
                    if self.rand() < chance {
                        let (x, z) = if mode == 0 {
                            let r = 0.05 + 0.82 * fi;
                            let ang = self.rand() * std::f32::consts::TAU;
                            (ang.cos() * r, ang.sin() * r)
                        } else {
                            (self.rand() * 1.9 - 0.95, self.rand() * 1.9 - 0.95)
                        };
                        let radius = (0.02 + 0.07 * (1.0 - fi)) * (0.4 + size);
                        let strength = 0.045 * e * gain * if self.rand() < 0.5 { 1.0 } else { -0.8 };
                        self.add_drop(x, z, radius, strength);
                    }
                }
            }
            _ => {}
        }
        // drums (all modes): kick = big drop, snare = medium, hats = sprinkle
        if a.hits[0] != self.last_hits[0] {
            let (x, z) = if mode == 0 { (0.0, 0.0) } else { (self.rand() * 1.4 - 0.7, self.rand() * 1.4 - 0.7) };
            self.add_drop(x, z, 0.09 * (0.5 + size), 0.09 * a.onset[0] * gain);
        }
        if a.hits[1] != self.last_hits[1] {
            let ang = self.rand() * std::f32::consts::TAU;
            self.add_drop(ang.cos() * 0.55, ang.sin() * 0.55, 0.05 * (0.5 + size), -0.06 * a.onset[1] * gain);
        }
        if a.hits[2] != self.last_hits[2] && mode != 0 {
            for _ in 0..3 {
                let (x, z) = (self.rand() * 1.9 - 0.95, self.rand() * 1.9 - 0.95);
                self.add_drop(x, z, 0.015 * (0.5 + size), 0.02 * gain);
            }
        }
        self.last_hits = a.hits;
    }
}

impl Scene for Ripples {
    fn reset(&mut self, _gpu: &Gpu) {
        self.clear_pending = true;
    }

    fn render(&mut self, gpu: &Gpu, ctx: &mut SceneCtx) {
        let p = ctx.params;
        let size = match p.choice_index("quality") {
            0 => 256,
            1 => 384,
            _ => 512,
        };
        if self.grid.as_ref().map(|g| g.size) != Some(size) {
            self.grid = Some(self.build(gpu, size));
            self.clear_pending = true;
        }
        self.pending_drops.clear();
        self.music_drops(ctx);
        let drops = std::mem::take(&mut self.pending_drops);
        let mut data = drops.clone();
        data.resize(MAX_DROPS, Drop::default());
        gpu.queue.write_buffer(&self.drops, 0, bytemuck::cast_slice(&data));

        let dt = ctx.info.dt.clamp(0.0, 0.1);
        self.yaw += dt * p.f("spin") * 0.4;
        let a = &ctx.info.audio;
        let faraday = if p.choice_index("mode") == 3 { (0.3 + a.loudness) * (0.3 + p.f("waves")) } else { 0.0 };
        let mut u = RipUniform {
            sim_size: [size as f32, size as f32],
            yaw: self.yaw,
            tilt: p.f("tilt"),
            depth: 0.15 + p.f("depth") * 0.9,
            caustics: p.f("caustics"),
            reflections: p.f("reflections"),
            tint: p.f("tint"),
            damping: 0.999 - p.f("damping") * 0.02,
            drop_count: drops.len() as u32,
            floor_style: p.choice_index("floor") as u32,
            time: ctx.info.time,
            faraday,
            faraday_freq: 9.0 + a.note as f32 * 1.3,
            wave_gain: 1.0,
            _pad: 0.0,
        };
        gpu.queue.write_buffer(&self.uniform, 0, bytemuck::bytes_of(&u));

        let g = self.grid.as_ref().unwrap();
        let enc = &mut *ctx.encoder;
        if self.clear_pending {
            for v in &g.water_v {
                gpu::pass(enc, "rip-clear", v, Some(wgpu::Color::TRANSPARENT));
            }
            self.clear_pending = false;
        }
        let globals = &ctx.globals.bind_group;
        // two wave steps per 60 Hz frame (drops only on the first)
        let steps = ((dt * 120.0).round() as usize).clamp(1, 4);
        for s in 0..steps {
            if s == 1 {
                u.drop_count = 0;
                // (uniform writes land before the submit; drops apply on step 0 via a second buffer
                // would be overkill: re-applying them on later steps just makes slightly taller drops)
            }
            gpu::fullscreen(enc, "rip-step", &g.water_v[1 - self.cur], &self.p_step, &[globals, &g.step_bg[self.cur]], None);
            self.cur = 1 - self.cur;
        }
        {
            let mut rp = gpu::pass(enc, "rip-caustics", &self.caust_v, Some(wgpu::Color::BLACK));
            rp.set_pipeline(&self.p_caustics);
            rp.set_bind_group(0, globals, &[]);
            rp.set_bind_group(1, &g.caustic_bg[self.cur], &[]);
            rp.draw(0..CAUSTIC_GRID * CAUSTIC_GRID * 6, 0..1);
        }
        gpu::fullscreen(enc, "rip-render", ctx.target, &self.p_render, &[globals, &g.render_bg[self.cur]], None);
    }
}
