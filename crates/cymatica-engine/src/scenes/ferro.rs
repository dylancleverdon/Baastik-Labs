//! Ferrofluid: a black, glossy magnetic fluid. Each spike is a damped spring driven by its
//! frequency band (bass in the middle, treble at the rim); kicks make every spike jump.

use crate::globals::GlobalsGpu;
use crate::gpu::{self, Bind, Gpu, HDR_FORMAT};
use crate::params::{choice, float, ParamDef};
use crate::scene::{scene_source, Scene, SceneCtx};
use bytemuck::{Pod, Zeroable};
use glam::Vec3;

pub static PARAMS: &[ParamDef] = &[
    float("spike_height", "Spike height", "Fluid", 0.0, 1.0, 0.6, "How tall the spikes grow."),
    float("sharpness", "Sharpness", "Fluid", 0.0, 1.0, 0.55, "Needle-sharp or soft spikes."),
    float("density", "Spike density", "Fluid", 0.0, 1.0, 0.5, "How many spikes."),
    float("swell", "Bass swell", "Fluid", 0.0, 1.0, 0.5, "The bass makes the whole blob breathe."),
    float("bounce", "Bounce", "Physics", 0.0, 1.0, 0.5, "How springy the spikes are."),
    float("kick", "Kick jump", "Physics", 0.0, 1.0, 0.6, "All spikes jump on kicks."),
    float("gloss", "Gloss", "Look", 0.2, 1.5, 1.0, "Reflections on the fluid."),
    float("lights", "Coloured lights", "Look", 0.0, 2.0, 1.0, "Theme-coloured studio lights in the reflections."),
    float("rim", "Iridescence", "Look", 0.0, 1.0, 0.25, "Colourful rims on loud passages."),
    float("orbit", "Camera orbit", "Camera", -1.0, 1.0, 0.1, "Circles the dish."),
    float("height", "Camera height", "Camera", 0.0, 1.0, 0.4, "Low or high viewpoint."),
    float("zoom", "Zoom", "Camera", 0.0, 1.0, 0.5, "Closer or further."),
    choice("quality", "Mesh detail", "Quality", &[("low", "Low"), ("medium", "Medium"), ("high", "High"), ("insane", "Insane")], "high", "Surface mesh resolution.").fixed(),
];

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct FerroUniform {
    view_proj: [[f32; 4]; 4],
    eye: [f32; 4],
    lattice: [f32; 4],
    blob: [f32; 4],
    grid: [u32; 4],
    reflect_tint: [f32; 4],
    gloss: f32,
    time: f32,
    rim: f32,
    _p: f32,
}

const MAX_SPIKES: usize = 64 * 64;

pub struct Ferro {
    uniform: wgpu::Buffer,
    spikes_buf: wgpu::Buffer,
    bg: wgpu::BindGroup,
    p_room: wgpu::RenderPipeline,
    p_mesh: wgpu::RenderPipeline,
    depth: Option<((u32, u32), wgpu::Texture, wgpu::TextureView)>,
    heights: Vec<f32>,
    vels: Vec<f32>,
    yaw: f32,
    last_kick: u32,
    phase: Vec<f32>,
}

pub fn create(gpu: &Gpu, globals: &GlobalsGpu) -> Box<dyn Scene> {
    let module = gpu.shader("ferro", scene_source(include_str!("../shaders/scenes/ferro.wgsl")));
    let layout = gpu.layout("ferro", &[Bind::Uniform, Bind::StorageRead]);
    let pl = gpu.pipeline_layout("ferro", &[&globals.layout, &layout]);
    let uniform = gpu.uniform_buffer("ferro-u", std::mem::size_of::<FerroUniform>() as u64);
    let spikes_buf = gpu.storage_buffer("ferro-spikes", (MAX_SPIKES * 4) as u64);
    let bg = gpu.bind_group("ferro", &layout, &[uniform.as_entire_binding(), spikes_buf.as_entire_binding()]);
    let p_mesh = gpu.device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("ferro-mesh"),
        layout: Some(&pl),
        vertex: wgpu::VertexState { module: &module, entry_point: Some("vs_mesh"), compilation_options: Default::default(), buffers: &[] },
        primitive: wgpu::PrimitiveState::default(),
        depth_stencil: Some(wgpu::DepthStencilState {
            format: wgpu::TextureFormat::Depth32Float,
            depth_write_enabled: Some(true),
            depth_compare: Some(wgpu::CompareFunction::Less),
            stencil: Default::default(),
            bias: Default::default(),
        }),
        multisample: Default::default(),
        fragment: Some(wgpu::FragmentState {
            module: &module,
            entry_point: Some("fs_mesh"),
            compilation_options: Default::default(),
            targets: &[Some(wgpu::ColorTargetState { format: HDR_FORMAT, blend: None, write_mask: wgpu::ColorWrites::ALL })],
        }),
        multiview_mask: None,
        cache: None,
    });
    let mut rng = 0x51ED_270Bu32;
    let phase = (0..MAX_SPIKES)
        .map(|_| {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            (rng >> 8) as f32 / (1u32 << 24) as f32
        })
        .collect();
    Box::new(Ferro {
        p_room: gpu.fullscreen_pipeline("ferro-room", &module, "fs_room", &pl, HDR_FORMAT, None),
        p_mesh,
        uniform,
        spikes_buf,
        bg,
        depth: None,
        heights: vec![0.0; MAX_SPIKES],
        vels: vec![0.0; MAX_SPIKES],
        yaw: 0.6,
        last_kick: 0,
        phase,
    })
}

impl Scene for Ferro {
    fn render(&mut self, gpu: &Gpu, ctx: &mut SceneCtx) {
        let p = ctx.params;
        let a = &ctx.info.audio;
        let dt = ctx.info.dt.clamp(0.0, 0.05);
        if self.depth.as_ref().map(|d| d.0) != Some(ctx.size) {
            let t = gpu.texture("ferro-depth", ctx.size, wgpu::TextureFormat::Depth32Float, wgpu::TextureUsages::RENDER_ATTACHMENT);
            let v = t.create_view(&Default::default());
            self.depth = Some((ctx.size, t, v));
        }
        // lattice
        let radius = 1.0f32;
        let spacing = 0.22 - p.f("density") * 0.13;
        let cols = ((2.0 * radius / spacing).ceil() as usize + 1).min(63);
        let rows = ((2.0 * radius / (spacing * 0.866)).ceil() as usize + 1).min(63);
        let n = cols * rows;
        // spike dynamics: each spike follows the band for its distance from the centre
        let k = 60.0 + p.f("bounce") * 260.0;
        let c = 14.0 - p.f("bounce") * 10.0;
        let gain = p.f("spike_height") * 0.55;
        let kicked = a.hits[0] != self.last_kick;
        self.last_kick = a.hits[0];
        let t = ctx.info.time;
        for j in 0..rows {
            for i in 0..cols {
                let idx = j * cols + i;
                let x = (i as f32 - (cols - 1) as f32 * 0.5 + if j % 2 == 1 { 0.5 } else { 0.0 }) * spacing;
                let z = (j as f32 - (rows - 1) as f32 * 0.5) * spacing * 0.866;
                let r = (x * x + z * z).sqrt() / radius;
                let band = ((r * 6.0) as usize).min(6);
                let wobble = 0.75 + 0.25 * (t * 2.0 + self.phase[idx] * 6.283).sin();
                let target = gain * (0.08 + a.bands[band] * wobble) * (1.0 - r * 0.35).max(0.0);
                let acc = k * (target - self.heights[idx]) - c * self.vels[idx];
                self.vels[idx] += acc * dt;
                if kicked {
                    self.vels[idx] += p.f("kick") * a.onset[0] * 2.5 * (1.0 - r * 0.5) * (0.6 + 0.4 * self.phase[idx]);
                }
                self.heights[idx] = (self.heights[idx] + self.vels[idx] * dt).clamp(0.0, 0.9);
            }
        }
        gpu.queue.write_buffer(&self.spikes_buf, 0, bytemuck::cast_slice(&self.heights[..n]));

        self.yaw += dt * p.f("orbit") * 0.25;
        let pitch = 0.2 + p.f("height") * 0.9;
        let dist = 2.2 + (1.0 - p.f("zoom")) * 2.2;
        let target_pt = Vec3::new(0.0, 0.12, 0.0);
        let eye = target_pt + Vec3::new(self.yaw.sin() * pitch.cos(), pitch.sin(), self.yaw.cos() * pitch.cos()) * dist;
        let view = crate::camera::look_at_rh(eye, target_pt, Vec3::Y);
        let aspect = ctx.size.0 as f32 / ctx.size.1 as f32;
        let proj = crate::camera::perspective_rh(38f32.to_radians(), aspect, 0.05, 50.0);
        let mesh_n = [160u32, 256, 384, 560][p.choice_index("quality").min(3)];
        let u = FerroUniform {
            view_proj: (proj * view).to_cols_array_2d(),
            eye: [eye.x, eye.y, eye.z, 1.0],
            lattice: [spacing, cols as f32, rows as f32, spacing * 0.62],
            blob: [radius * 0.82, 0.12, p.f("swell") * a.bass * 0.8, 1.2 + p.f("sharpness") * 2.4],
            grid: [mesh_n, n as u32, 0, 0],
            reflect_tint: [p.f("lights"), 0.0, 0.0, 0.0],
            gloss: p.f("gloss"),
            time: t,
            rim: p.f("rim"),
            _p: 0.0,
        };
        gpu.queue.write_buffer(&self.uniform, 0, bytemuck::bytes_of(&u));
        let globals = &ctx.globals.bind_group;
        gpu::fullscreen(ctx.encoder, "ferro-room", ctx.target, &self.p_room, &[globals, &self.bg], Some(wgpu::Color::BLACK));
        let depth = &self.depth.as_ref().unwrap().2;
        let mut rp = ctx.encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("ferro-mesh"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: ctx.target,
                depth_slice: None,
                resolve_target: None,
                ops: wgpu::Operations { load: wgpu::LoadOp::Load, store: wgpu::StoreOp::Store },
            })],
            depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                view: depth,
                depth_ops: Some(wgpu::Operations { load: wgpu::LoadOp::Clear(1.0), store: wgpu::StoreOp::Discard }),
                stencil_ops: None,
            }),
            timestamp_writes: None,
            occlusion_query_set: None,
            multiview_mask: None,
        });
        rp.set_pipeline(&self.p_mesh);
        rp.set_bind_group(0, globals, &[]);
        rp.set_bind_group(1, &self.bg, &[]);
        rp.draw(0..mesh_n * mesh_n * 6, 0..1);
    }
}
