//! Liquid: a real 3D fluid (MLS-MPM) in a glass tank, rendered as a smooth refracting surface.
//! Bass sloshes the tank, kicks fire geysers from the floor "subwoofer", mids stir a whirlpool,
//! the beat drives a wave machine.

use crate::globals::GlobalsGpu;
use crate::gpu::{self, Bind, Gpu, HDR_FORMAT};
use crate::params::{choice, float, ParamDef};
use crate::scene::{scene_source, Scene, SceneCtx};
use bytemuck::{Pod, Zeroable};
use glam::Vec3;

pub static PARAMS: &[ParamDef] = &[
    float("viscosity", "Thickness", "Liquid", 0.0, 1.0, 0.08, "Water (0) to honey (1)."),
    float("stiffness", "Bounce", "Liquid", 0.0, 1.0, 0.5, "How strongly the liquid resists squeezing."),
    float("gravity", "Gravity", "Liquid", 0.2, 2.0, 1.0, "How heavy the liquid is."),
    float("slosh", "Bass slosh", "Music", 0.0, 1.0, 0.6, "Bass tilts the tank to the beat."),
    float("geyser", "Kick geysers", "Music", 0.0, 1.0, 0.7, "Kicks fire jets from the subwoofer in the floor."),
    float("whirlpool", "Mid whirlpool", "Music", 0.0, 1.0, 0.35, "Mids stir the liquid around."),
    float("waves", "Wave machine", "Music", 0.0, 1.0, 0.35, "A paddle pushes waves on the beat."),
    choice(
        "color_mode",
        "Colour",
        "Look",
        &[("theme", "Theme colour"), ("rainbow", "Rainbow dye"), ("depth", "By depth")],
        "rainbow",
        "How the liquid is coloured.",
    ),
    float("tint", "Tint", "Look", 0.0, 1.0, 0.25, "Which theme colour (for Theme colour)."),
    float("clarity", "Clarity", "Look", 0.0, 1.0, 0.55, "Clear (1) or deeply coloured (0)."),
    float("refraction", "Refraction", "Look", 0.0, 1.0, 0.6, "How much the liquid bends the light."),
    float("gloss", "Gloss", "Look", 0.0, 1.0, 0.85, "Reflections on the surface."),
    float("foam", "Foam", "Look", 0.0, 1.0, 0.5, "White spray from fast-moving liquid."),
    float("smoothness", "Smoothness", "Look", 0.0, 1.0, 0.6, "Surface smoothing."),
    choice("tank", "Tank", "Look", &[("tiles", "Tiled"), ("dark", "Dark")], "tiles", "Tank floor."),
    float("orbit", "Camera orbit", "Camera", -1.0, 1.0, 0.12, "Circles the tank."),
    float("height", "Camera height", "Camera", 0.0, 1.0, 0.45, "Low or high viewpoint."),
    float("zoom", "Zoom", "Camera", 0.0, 1.0, 0.5, "Closer or further."),
    choice(
        "quality",
        "Particles",
        "Quality",
        &[("low", "30k"), ("medium", "60k"), ("high", "120k"), ("insane", "250k")],
        "high",
        "Number of fluid particles.",
    )
    .fixed(),
];

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct SimUniform {
    grid: [u32; 4],
    dt: f32,
    stiffness: f32,
    rest_density: f32,
    viscosity: f32,
    gravity: [f32; 4],
    geyser: [f32; 4],
    vortex: [f32; 4],
    paddle: [f32; 4],
    frame: u32,
    _p: [u32; 3],
}

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct DrawUniform {
    view: [[f32; 4]; 4],
    proj: [[f32; 4]; 4],
    inv_view: [[f32; 4]; 4],
    eye: [f32; 4],
    grid: [f32; 4],
    resolution: [f32; 2],
    blur_dir: [f32; 2],
    radius: f32,
    tan_half_fov: f32,
    aspect: f32,
    thickness_scale: f32,
    absorb: f32,
    refraction: f32,
    foam: f32,
    gloss: f32,
    color_mode: u32,
    blur_radius: f32,
    kick: f32,
    tank_style: u32,
    liquid_color: [f32; 4],
    out_resolution: [f32; 2],
    time: f32,
    _p: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct Particle {
    pos: [f32; 3],
    dye: f32,
    vel: [f32; 3],
    speed: f32,
    c: [[f32; 4]; 3],
}

struct Quality {
    grid: [u32; 3],
    particles: u32,
}

fn quality(i: usize) -> Quality {
    match i {
        0 => Quality { grid: [32, 24, 24], particles: 30_000 },
        1 => Quality { grid: [40, 30, 30], particles: 60_000 },
        3 => Quality { grid: [64, 44, 44], particles: 250_000 },
        _ => Quality { grid: [48, 36, 36], particles: 120_000 },
    }
}

struct Sim {
    q: usize,
    grid: [u32; 3],
    n: u32,
    spacing: f32,
    rest_density: f32,
    particles: wgpu::Buffer,
    sim_bg: wgpu::BindGroup,
    _cells: wgpu::Buffer,
    _grid_v: wgpu::Buffer,
}

type Tex = (wgpu::Texture, wgpu::TextureView);

struct Targets {
    size: (u32, u32),
    fluid: (u32, u32),
    bg: Tex,
    depth_a: Tex,
    depth_b: Tex,
    thick: Tex,
    zbuf: Tex,
    g_bg: wgpu::BindGroup,
    g_blur_h: wgpu::BindGroup,
    g_blur_v: wgpu::BindGroup,
    g_comp: wgpu::BindGroup,
}

pub struct Liquid {
    sim_layout: wgpu::BindGroupLayout,
    draw_layout: wgpu::BindGroupLayout,
    sim_u: wgpu::Buffer,
    draw_u: wgpu::Buffer,
    draw_u_h: wgpu::Buffer,
    draw_u_v: wgpu::Buffer,
    cs_clear: wgpu::ComputePipeline,
    cs_p2g_mass: wgpu::ComputePipeline,
    cs_p2g_stress: wgpu::ComputePipeline,
    cs_grid: wgpu::ComputePipeline,
    cs_g2p: wgpu::ComputePipeline,
    p_bg: wgpu::RenderPipeline,
    p_depth: wgpu::RenderPipeline,
    p_thick: wgpu::RenderPipeline,
    p_blur: wgpu::RenderPipeline,
    p_comp: wgpu::RenderPipeline,
    p_foam: wgpu::RenderPipeline,
    dummy: wgpu::TextureView,
    sim: Option<Sim>,
    targets: Option<Targets>,
    yaw: f32,
    frame: u32,
    needs_reset: bool,
}

const R32: wgpu::TextureFormat = wgpu::TextureFormat::R32Float;
const RGBA16: wgpu::TextureFormat = wgpu::TextureFormat::Rgba16Float;

#[allow(clippy::too_many_arguments)]
fn sphere_pipeline(
    gpu: &Gpu,
    label: &str,
    module: &wgpu::ShaderModule,
    vs: &str,
    fs: &str,
    layout: &wgpu::PipelineLayout,
    format: wgpu::TextureFormat,
    blend: Option<wgpu::BlendState>,
    depth: bool,
) -> wgpu::RenderPipeline {
    gpu.device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some(label),
        layout: Some(layout),
        vertex: wgpu::VertexState {
            module,
            entry_point: Some(vs),
            compilation_options: Default::default(),
            buffers: &[],
        },
        primitive: wgpu::PrimitiveState::default(),
        depth_stencil: depth.then(|| wgpu::DepthStencilState {
            format: wgpu::TextureFormat::Depth32Float,
            depth_write_enabled: Some(true),
            depth_compare: Some(wgpu::CompareFunction::Less),
            stencil: Default::default(),
            bias: Default::default(),
        }),
        multisample: Default::default(),
        fragment: Some(wgpu::FragmentState {
            module,
            entry_point: Some(fs),
            compilation_options: Default::default(),
            targets: &[Some(wgpu::ColorTargetState { format, blend, write_mask: wgpu::ColorWrites::ALL })],
        }),
        multiview_mask: None,
        cache: None,
    })
}

pub fn create(gpu: &Gpu, globals: &GlobalsGpu) -> Box<dyn Scene> {
    let sim_mod = gpu.shader("liquid-sim", scene_source(include_str!("../shaders/scenes/liquid_sim.wgsl")));
    let draw_mod = gpu.shader("liquid-draw", scene_source(include_str!("../shaders/scenes/liquid_draw.wgsl")));
    let sim_layout = gpu.layout("liquid-sim", &[Bind::Uniform, Bind::StorageRw, Bind::StorageRw, Bind::StorageRw]);
    let draw_layout = gpu.layout(
        "liquid-draw",
        &[
            Bind::Uniform,
            Bind::StorageRead,
            Bind::TextureUnfilterable,
            Bind::TextureUnfilterable,
            Bind::TextureUnfilterable,
        ],
    );
    let sim_pl = gpu.pipeline_layout("liquid-sim", &[&globals.layout, &sim_layout]);
    let draw_pl = gpu.pipeline_layout("liquid-draw", &[&globals.layout, &draw_layout]);
    let cp = |entry: &str| gpu.compute_pipeline(entry, &sim_mod, entry, &sim_pl);
    let dummy = gpu.render_texture("liquid-dummy", (1, 1), R32).create_view(&Default::default());
    let du = std::mem::size_of::<DrawUniform>() as u64;
    Box::new(Liquid {
        cs_clear: cp("cs_clear"),
        cs_p2g_mass: cp("cs_p2g_mass"),
        cs_p2g_stress: cp("cs_p2g_stress"),
        cs_grid: cp("cs_grid"),
        cs_g2p: cp("cs_g2p"),
        p_bg: gpu.fullscreen_pipeline("liquid-bg", &draw_mod, "fs_background", &draw_pl, HDR_FORMAT, None),
        p_depth: sphere_pipeline(gpu, "liquid-depth", &draw_mod, "vs_sphere", "fs_depth", &draw_pl, R32, None, true),
        p_thick: sphere_pipeline(
            gpu,
            "liquid-thick",
            &draw_mod,
            "vs_sphere",
            "fs_thickness",
            &draw_pl,
            RGBA16,
            Some(gpu::ADDITIVE),
            false,
        ),
        p_blur: gpu.fullscreen_pipeline("liquid-blur", &draw_mod, "fs_blur", &draw_pl, R32, None),
        p_comp: gpu.fullscreen_pipeline("liquid-comp", &draw_mod, "fs_composite", &draw_pl, HDR_FORMAT, None),
        p_foam: sphere_pipeline(
            gpu,
            "liquid-foam",
            &draw_mod,
            "vs_foam",
            "fs_foam",
            &draw_pl,
            HDR_FORMAT,
            Some(gpu::ADDITIVE),
            false,
        ),
        sim_u: gpu.uniform_buffer("liquid-sim-u", std::mem::size_of::<SimUniform>() as u64),
        draw_u: gpu.uniform_buffer("liquid-draw-u", du),
        draw_u_h: gpu.uniform_buffer("liquid-draw-u-h", du),
        draw_u_v: gpu.uniform_buffer("liquid-draw-u-v", du),
        sim_layout,
        draw_layout,
        dummy,
        sim: None,
        targets: None,
        yaw: 0.7,
        frame: 0,
        needs_reset: true,
    })
}

impl Liquid {
    fn build_sim(&self, gpu: &Gpu, q: usize) -> Sim {
        let qual = quality(q);
        let [gx, gy, gz] = qual.grid;
        let n = qual.particles;
        // fill ~40% of the tank's height with ~5-6 particles per cell
        let inner = ((gx - 4) * (gz - 4)) as f32;
        let fill_h = gy as f32 * 0.42;
        let spacing = (fill_h * inner / n as f32).cbrt();
        let nx = ((gx as f32 - 4.0) / spacing).floor() as u32;
        let nz = ((gz as f32 - 4.0) / spacing).floor() as u32;
        let mut rng = 0x1234_5678u32;
        let mut rand = || {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            (rng >> 8) as f32 / (1u32 << 24) as f32
        };
        let mut parts = Vec::with_capacity(n as usize);
        for i in 0..n {
            let ix = i % nx;
            let iz = (i / nx) % nz;
            let iy = i / (nx * nz);
            let pos = [
                2.0 + (ix as f32 + 0.5 + (rand() - 0.5) * 0.3) * spacing,
                2.0 + (iy as f32 + 0.5 + (rand() - 0.5) * 0.3) * spacing,
                2.0 + (iz as f32 + 0.5 + (rand() - 0.5) * 0.3) * spacing,
            ];
            parts.push(Particle { pos, dye: ix as f32 / nx as f32, vel: [0.0; 3], speed: 0.0, c: [[0.0; 4]; 3] });
        }
        let particles = gpu.storage_buffer("liquid-particles", (n as u64) * 80);
        gpu.queue.write_buffer(&particles, 0, bytemuck::cast_slice(&parts));
        let ncells = (gx * gy * gz) as u64;
        let cells = gpu.storage_buffer("liquid-cells", ncells * 16);
        let grid_v = gpu.storage_buffer("liquid-grid-v", ncells * 16);
        let sim_bg = gpu.bind_group(
            "liquid-sim",
            &self.sim_layout,
            &[
                self.sim_u.as_entire_binding(),
                particles.as_entire_binding(),
                cells.as_entire_binding(),
                grid_v.as_entire_binding(),
            ],
        );
        let ppc = 1.0 / (spacing * spacing * spacing);
        Sim {
            q,
            grid: qual.grid,
            n,
            spacing,
            rest_density: ppc * 0.92,
            particles,
            sim_bg,
            _cells: cells,
            _grid_v: grid_v,
        }
    }

    fn build_targets(&self, gpu: &Gpu, size: (u32, u32), sim: &Sim) -> Targets {
        let fluid = ((size.0 / 2).max(8), (size.1 / 2).max(8));
        let tex = |label: &str, s: (u32, u32), f| {
            let t = gpu.render_texture(label, s, f);
            let v = t.create_view(&Default::default());
            (t, v)
        };
        let bg = tex("liquid-bgtex", size, HDR_FORMAT);
        let depth_a = tex("liquid-depth-a", fluid, R32);
        let depth_b = tex("liquid-depth-b", fluid, R32);
        let thick = tex("liquid-thick", fluid, RGBA16);
        let zt =
            gpu.texture("liquid-z", fluid, wgpu::TextureFormat::Depth32Float, wgpu::TextureUsages::RENDER_ATTACHMENT);
        let zv = zt.create_view(&Default::default());
        let group = |u: &wgpu::Buffer, a: &wgpu::TextureView, b: &wgpu::TextureView, c: &wgpu::TextureView| {
            gpu.bind_group(
                "liquid-draw",
                &self.draw_layout,
                &[
                    u.as_entire_binding(),
                    sim.particles.as_entire_binding(),
                    wgpu::BindingResource::TextureView(a),
                    wgpu::BindingResource::TextureView(b),
                    wgpu::BindingResource::TextureView(c),
                ],
            )
        };
        let d = &self.dummy;
        Targets {
            g_bg: group(&self.draw_u, d, d, d),
            g_blur_h: group(&self.draw_u_h, &depth_a.1, d, d),
            g_blur_v: group(&self.draw_u_v, &depth_b.1, d, d),
            g_comp: group(&self.draw_u, &depth_a.1, &thick.1, &bg.1),
            size,
            fluid,
            bg,
            depth_a,
            depth_b,
            thick,
            zbuf: (zt, zv),
        }
    }
}

impl Scene for Liquid {
    fn reset(&mut self, _gpu: &Gpu) {
        self.needs_reset = true;
    }

    fn render(&mut self, gpu: &Gpu, ctx: &mut SceneCtx) {
        let p = ctx.params;
        let a = &ctx.info.audio;
        let q = p.choice_index("quality");
        if self.needs_reset || self.sim.as_ref().map(|s| s.q) != Some(q) {
            self.sim = Some(self.build_sim(gpu, q));
            self.targets = None;
            self.needs_reset = false;
        }
        if self.targets.as_ref().map(|t| t.size) != Some(ctx.size) {
            let t = self.build_targets(gpu, ctx.size, self.sim.as_ref().unwrap());
            self.targets = Some(t);
        }
        let sim = self.sim.as_ref().unwrap();
        let t = self.targets.as_ref().unwrap();
        let [gx, gy, gz] = sim.grid;
        let time = ctx.info.time;
        let dt = ctx.info.dt.clamp(0.0, 0.1);
        self.frame = self.frame.wrapping_add(1);

        // ---- music → forces
        let g = 0.3 * p.f("gravity");
        let slosh = p.f("slosh") * a.bass * 0.32;
        let phase = (a.beat_count % 2) as f32 + a.beat_phase;
        let tilt_x = slosh * (phase * std::f32::consts::PI).sin();
        let tilt_z = slosh * 0.4 * (time * 0.7).sin();
        let gravity = [tilt_x.sin() * g, -tilt_x.cos() * tilt_z.cos() * g, tilt_z.sin() * g];
        let geyser = p.f("geyser") * a.onset[0] * a.onset[0] * 4.5;
        let vortex = p.f("whirlpool") * (0.15 + a.mid) * 0.35;
        let paddle_push = if a.beat_phase < 0.18 && !a.silent { p.f("waves") * (0.4 + a.bass) * 1.6 } else { 0.0 };
        let su = SimUniform {
            grid: [gx, gy, gz, sim.n],
            dt: 0.2,
            stiffness: 1.5 + p.f("stiffness") * 4.0,
            rest_density: sim.rest_density,
            viscosity: p.f("viscosity") * p.f("viscosity") * 2.5,
            gravity: [gravity[0], gravity[1], gravity[2], 0.0],
            geyser: [gx as f32 * 0.5, gz as f32 * 0.5, gx as f32 * 0.12, geyser],
            vortex: [vortex, gx as f32 * 0.45, 0.0, 0.0],
            paddle: [3.0, paddle_push, 0.0, 0.0],
            frame: self.frame,
            _p: [0; 3],
        };
        gpu.queue.write_buffer(&self.sim_u, 0, bytemuck::bytes_of(&su));

        let steps = ((dt * 120.0).round() as u32).clamp(1, 4);
        let ncells = gx * gy * gz;
        {
            let mut cp = ctx
                .encoder
                .begin_compute_pass(&wgpu::ComputePassDescriptor { label: Some("liquid-sim"), timestamp_writes: None });
            cp.set_bind_group(0, &ctx.globals.bind_group, &[]);
            cp.set_bind_group(1, &sim.sim_bg, &[]);
            for _ in 0..steps {
                cp.set_pipeline(&self.cs_clear);
                cp.dispatch_workgroups(ncells.div_ceil(256), 1, 1);
                cp.set_pipeline(&self.cs_p2g_mass);
                cp.dispatch_workgroups(sim.n.div_ceil(256), 1, 1);
                cp.set_pipeline(&self.cs_p2g_stress);
                cp.dispatch_workgroups(sim.n.div_ceil(256), 1, 1);
                cp.set_pipeline(&self.cs_grid);
                cp.dispatch_workgroups(ncells.div_ceil(256), 1, 1);
                cp.set_pipeline(&self.cs_g2p);
                cp.dispatch_workgroups(sim.n.div_ceil(256), 1, 1);
            }
        }

        // ---- camera
        self.yaw += dt * p.f("orbit") * 0.25;
        let target = Vec3::new(gx as f32 * 0.5, gy as f32 * 0.36, gz as f32 * 0.5);
        let pitch = 0.18 + p.f("height") * 0.75;
        let dist = gx as f32 * (1.3 + (1.0 - p.f("zoom")) * 1.0);
        let eye = target + Vec3::new(self.yaw.sin() * pitch.cos(), pitch.sin(), self.yaw.cos() * pitch.cos()) * dist;
        let view = crate::camera::look_at_rh(eye, target, Vec3::Y);
        let aspect = ctx.size.0 as f32 / ctx.size.1 as f32;
        let fov = 40f32.to_radians();
        let proj = crate::camera::perspective_rh(fov, aspect, 0.1, 2000.0);
        let palette = ctx.globals.theme().map(|t| t.palette()).unwrap_or_default();
        let tint = palette.get((p.f("tint") * 255.0) as usize).copied().unwrap_or([0.3, 0.6, 1.0]);
        let du = DrawUniform {
            view: view.to_cols_array_2d(),
            proj: proj.to_cols_array_2d(),
            inv_view: view.inverse().to_cols_array_2d(),
            eye: [eye.x, eye.y, eye.z, 1.0],
            grid: [gx as f32, gy as f32, gz as f32, sim.n as f32],
            resolution: [t.fluid.0 as f32, t.fluid.1 as f32],
            blur_dir: [1.0, 0.0],
            radius: sim.spacing * 1.35,
            tan_half_fov: (fov * 0.5).tan(),
            aspect,
            thickness_scale: 0.35 * sim.spacing,
            absorb: 0.15 + (1.0 - p.f("clarity")) * 1.6,
            refraction: p.f("refraction"),
            foam: p.f("foam"),
            gloss: p.f("gloss"),
            color_mode: p.choice_index("color_mode") as u32,
            blur_radius: 0.8 + p.f("smoothness") * 2.6,
            kick: a.onset[0],
            tank_style: p.choice_index("tank") as u32,
            liquid_color: [tint[0], tint[1], tint[2], 1.0],
            out_resolution: [ctx.size.0 as f32, ctx.size.1 as f32],
            time,
            _p: 0.0,
        };
        gpu.queue.write_buffer(&self.draw_u, 0, bytemuck::bytes_of(&du));
        gpu.queue.write_buffer(&self.draw_u_h, 0, bytemuck::bytes_of(&DrawUniform { blur_dir: [1.0, 0.0], ..du }));
        gpu.queue.write_buffer(&self.draw_u_v, 0, bytemuck::bytes_of(&DrawUniform { blur_dir: [0.0, 1.0], ..du }));

        let globals = &ctx.globals.bind_group;
        let enc = &mut *ctx.encoder;
        // the room behind and under the liquid
        gpu::fullscreen(enc, "liquid-bg", &t.bg.1, &self.p_bg, &[globals, &t.g_bg], None);
        // sphere depths
        {
            let mut rp = enc.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("liquid-depth"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &t.depth_a.1,
                    depth_slice: None,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color { r: 10000.0, g: 0.0, b: 0.0, a: 1.0 }),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: Some(wgpu::RenderPassDepthStencilAttachment {
                    view: &t.zbuf.1,
                    depth_ops: Some(wgpu::Operations { load: wgpu::LoadOp::Clear(1.0), store: wgpu::StoreOp::Store }),
                    stencil_ops: None,
                }),
                timestamp_writes: None,
                occlusion_query_set: None,
                multiview_mask: None,
            });
            rp.set_pipeline(&self.p_depth);
            rp.set_bind_group(0, globals, &[]);
            rp.set_bind_group(1, &t.g_bg, &[]);
            rp.draw(0..6, 0..sim.n);
        }
        // thickness + dye
        {
            let mut rp = gpu::pass(enc, "liquid-thick", &t.thick.1, Some(wgpu::Color::TRANSPARENT));
            rp.set_pipeline(&self.p_thick);
            rp.set_bind_group(0, globals, &[]);
            rp.set_bind_group(1, &t.g_bg, &[]);
            rp.draw(0..6, 0..sim.n);
        }
        // smooth the surface
        for _ in 0..2 {
            gpu::fullscreen(enc, "liquid-blur-h", &t.depth_b.1, &self.p_blur, &[globals, &t.g_blur_h], None);
            gpu::fullscreen(enc, "liquid-blur-v", &t.depth_a.1, &self.p_blur, &[globals, &t.g_blur_v], None);
        }
        // shade
        gpu::fullscreen(enc, "liquid-comp", ctx.target, &self.p_comp, &[globals, &t.g_comp], Some(wgpu::Color::BLACK));
        // foam on top
        {
            let mut rp = gpu::pass(enc, "liquid-foam", ctx.target, None);
            rp.set_pipeline(&self.p_foam);
            rp.set_bind_group(0, globals, &[]);
            rp.set_bind_group(1, &t.g_bg, &[]);
            rp.draw(0..6, 0..sim.n);
        }
    }
}
