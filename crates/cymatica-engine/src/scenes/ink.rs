//! Ink: a real 2D fluid (incompressible Navier-Stokes with vorticity confinement). Each frequency
//! band is a jet of coloured ink; kicks splash, the bass swirls everything.

use crate::globals::GlobalsGpu;
use crate::gpu::{self, Bind, Gpu, HDR_FORMAT};
use crate::params::{choice, float, ParamDef};
use crate::scene::{scene_source, Scene, SceneCtx};
use bytemuck::{Pod, Zeroable};

pub static PARAMS: &[ParamDef] = &[
    choice("jets", "Jets", "Flow", &[("orbit", "Orbiting"), ("fountain", "Fountain"), ("rain", "Rain")], "orbit", "Where the ink comes from."),
    float("jet_force", "Jet force", "Flow", 0.0, 1.0, 0.7, "How hard each band pushes its ink."),
    float("splash", "Kick splash", "Flow", 0.0, 1.0, 0.7, "Big bursts on kicks."),
    float("swirl", "Bass swirl", "Flow", 0.0, 1.0, 0.35, "The bass stirs the whole fluid."),
    float("curl", "Curliness", "Fluid", 0.0, 1.0, 0.35, "Keeps small vortices alive (vorticity confinement)."),
    float("viscosity", "Thickness", "Fluid", 0.0, 1.0, 0.25, "How quickly motion dies down."),
    float("fade", "Ink fade", "Fluid", 0.0, 1.0, 0.55, "How quickly ink disappears."),
    float("ink", "Ink amount", "Look", 0.0, 1.0, 0.55, "How much colour each jet carries."),
    float("shading", "Gloss", "Look", 0.0, 1.0, 0.6, "Glossy 3D lighting on the ink."),
    float("brightness", "Brightness", "Look", 0.2, 3.0, 1.0, "Overall ink brightness."),
    choice("color_by", "Colour by", "Look", &[("band", "Frequency band"), ("flow", "Flowing")], "band", "How ink gets its colour."),
    choice("quality", "Simulation detail", "Quality", &[("low", "Low"), ("medium", "Medium"), ("high", "High"), ("insane", "Insane")], "high", "Fluid grid resolution.").fixed(),
];

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct InkUniform {
    sim_texel: [f32; 2],
    dye_texel: [f32; 2],
    dt: f32,
    vel_dissipation: f32,
    dye_dissipation: f32,
    curl_strength: f32,
    pressure_decay: f32,
    shading: f32,
    splat_count: u32,
    brightness: f32,
    swirl: f32,
    time: f32,
    aspect: f32,
    mode: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable, Default)]
struct Splat {
    pos_radius: [f32; 4],
    force: [f32; 4],
    color: [f32; 4],
}

const MAX_SPLATS: usize = 64;
const JACOBI: usize = 28;

type Tex = (wgpu::Texture, wgpu::TextureView);

struct Grids {
    sim: (u32, u32),
    dye: (u32, u32),
    vel: [Tex; 2],
    dye_tex: [Tex; 2],
    pressure: [Tex; 2],
    curl: Tex,
    div: Tex,
    // bind groups by parity
    splat_vel: [wgpu::BindGroup; 2],
    splat_dye: [wgpu::BindGroup; 2],
    curl_bg: [wgpu::BindGroup; 2],
    vort: [wgpu::BindGroup; 2],
    div_bg: [wgpu::BindGroup; 2],
    pdecay: [wgpu::BindGroup; 2],
    jacobi: [wgpu::BindGroup; 2],
    grad: [[wgpu::BindGroup; 2]; 2],
    adv_vel: [wgpu::BindGroup; 2],
    adv_dye: [[wgpu::BindGroup; 2]; 2],
    display: [[wgpu::BindGroup; 2]; 2],
}

pub struct Ink {
    layout: wgpu::BindGroupLayout,
    u_vel: wgpu::Buffer,
    u_dye: wgpu::Buffer,
    splats: wgpu::Buffer,
    sampler: wgpu::Sampler,
    p_splat_vec: wgpu::RenderPipeline,
    p_advect_vec: wgpu::RenderPipeline,
    p_curl: wgpu::RenderPipeline,
    p_vort: wgpu::RenderPipeline,
    p_div: wgpu::RenderPipeline,
    p_pdecay: wgpu::RenderPipeline,
    p_jacobi: wgpu::RenderPipeline,
    p_grad: wgpu::RenderPipeline,
    p_display: wgpu::RenderPipeline,
    grids: Option<Grids>,
    vi: usize,
    di: usize,
    pi: usize,
    rng: u32,
    last_hits: [u32; 3],
    clear_pending: bool,
}

const VEC: wgpu::TextureFormat = wgpu::TextureFormat::Rgba16Float;
const SCALAR: wgpu::TextureFormat = wgpu::TextureFormat::R16Float;

pub fn create(gpu: &Gpu, globals: &GlobalsGpu) -> Box<dyn Scene> {
    let module = gpu.shader("ink", scene_source(include_str!("../shaders/scenes/ink.wgsl")));
    let layout = gpu.layout("ink", &[Bind::Uniform, Bind::StorageRead, Bind::Texture, Bind::Texture, Bind::Sampler]);
    let pl = gpu.pipeline_layout("ink", &[&globals.layout, &layout]);
    let fsp = |label: &str, entry: &str, format| gpu.fullscreen_pipeline(label, &module, entry, &pl, format, None);
    Box::new(Ink {
        u_vel: gpu.uniform_buffer("ink-u-vel", std::mem::size_of::<InkUniform>() as u64),
        u_dye: gpu.uniform_buffer("ink-u-dye", std::mem::size_of::<InkUniform>() as u64),
        splats: gpu.storage_buffer("ink-splats", (MAX_SPLATS * std::mem::size_of::<Splat>()) as u64),
        sampler: gpu.sampler(wgpu::FilterMode::Linear, wgpu::AddressMode::ClampToEdge),
        p_splat_vec: fsp("ink-splat", "fs_splat", VEC),
        p_advect_vec: fsp("ink-advect", "fs_advect", VEC),
        p_curl: fsp("ink-curl", "fs_curl", SCALAR),
        p_vort: fsp("ink-vort", "fs_vorticity", VEC),
        p_div: fsp("ink-div", "fs_divergence", SCALAR),
        p_pdecay: fsp("ink-pdecay", "fs_pressure_decay", SCALAR),
        p_jacobi: fsp("ink-jacobi", "fs_jacobi", SCALAR),
        p_grad: fsp("ink-grad", "fs_gradient", VEC),
        p_display: fsp("ink-display", "fs_display", HDR_FORMAT),
        layout,
        grids: None,
        vi: 0,
        di: 0,
        pi: 0,
        rng: 0x9e3779b9,
        last_hits: [0; 3],
        clear_pending: true,
    })
}

impl Ink {
    fn rand(&mut self) -> f32 {
        self.rng ^= self.rng << 13;
        self.rng ^= self.rng >> 17;
        self.rng ^= self.rng << 5;
        (self.rng >> 8) as f32 / (1u32 << 24) as f32
    }

    fn build(&self, gpu: &Gpu, sim: (u32, u32), dye: (u32, u32)) -> Grids {
        let tex = |n: &str, s, f| {
            let t = gpu.render_texture(n, s, f);
            let v = t.create_view(&Default::default());
            (t, v)
        };
        let vel = [tex("ink-vel0", sim, VEC), tex("ink-vel1", sim, VEC)];
        let dye_tex = [tex("ink-dye0", dye, VEC), tex("ink-dye1", dye, VEC)];
        let pressure = [tex("ink-p0", sim, SCALAR), tex("ink-p1", sim, SCALAR)];
        let curl = tex("ink-curl", sim, SCALAR);
        let div = tex("ink-div", sim, SCALAR);
        let bg = |u: &wgpu::Buffer, a: &wgpu::TextureView, b: &wgpu::TextureView| {
            gpu.bind_group(
                "ink",
                &self.layout,
                &[
                    u.as_entire_binding(),
                    self.splats.as_entire_binding(),
                    wgpu::BindingResource::TextureView(a),
                    wgpu::BindingResource::TextureView(b),
                    wgpu::BindingResource::Sampler(&self.sampler),
                ],
            )
        };
        let (uv, ud) = (&self.u_vel, &self.u_dye);
        let two = |f: &dyn Fn(usize) -> wgpu::BindGroup| [f(0), f(1)];
        Grids {
            splat_vel: two(&|i| bg(uv, &vel[i].1, &vel[i].1)),
            splat_dye: two(&|i| bg(ud, &dye_tex[i].1, &dye_tex[i].1)),
            curl_bg: two(&|i| bg(uv, &vel[i].1, &vel[i].1)),
            vort: two(&|i| bg(uv, &vel[i].1, &curl.1)),
            div_bg: two(&|i| bg(uv, &vel[i].1, &vel[i].1)),
            pdecay: two(&|j| bg(uv, &pressure[j].1, &pressure[j].1)),
            jacobi: two(&|j| bg(uv, &pressure[j].1, &div.1)),
            grad: [two(&|i| bg(uv, &pressure[0].1, &vel[i].1)), two(&|i| bg(uv, &pressure[1].1, &vel[i].1))],
            adv_vel: two(&|i| bg(uv, &vel[i].1, &vel[i].1)),
            adv_dye: [two(&|k| bg(ud, &vel[0].1, &dye_tex[k].1)), two(&|k| bg(ud, &vel[1].1, &dye_tex[k].1))],
            display: [two(&|i| bg(ud, &dye_tex[0].1, &vel[i].1)), two(&|i| bg(ud, &dye_tex[1].1, &vel[i].1))],
            sim,
            dye,
            vel,
            dye_tex,
            pressure,
            curl,
            div,
        }
    }

    /// Splats driven by the music for this frame.
    fn music_splats(&mut self, ctx: &SceneCtx, aspect: f32) -> Vec<Splat> {
        let p = ctx.params;
        let a = &ctx.info.audio;
        let t = ctx.info.time;
        let mut out = Vec::new();
        let jets = p.choice_index("jets");
        let force = p.f("jet_force");
        let ink = p.f("ink");
        let by_band = p.choice_index("color_by") == 0;
        let n = a.bands.len();
        for (i, &e) in a.bands.iter().enumerate() {
            if e < 0.04 {
                continue;
            }
            let fi = i as f32 / (n - 1) as f32;
            let (pos, dir) = match jets {
                1 => {
                    // fountain along the bottom, pointing up and slightly inward
                    let x = 0.12 + 0.76 * fi;
                    let sway = (t * 0.7 + i as f32).sin() * 0.25;
                    ([x, 0.97], [((0.5 - x) * 0.6 + sway) * aspect.min(2.0), -1.0])
                }
                2 => {
                    let x = self.rand();
                    let y = self.rand() * 0.8 + 0.1;
                    let ang = self.rand() * std::f32::consts::TAU;
                    ([x, y], [ang.cos(), ang.sin()])
                }
                _ => {
                    let ang = fi * std::f32::consts::TAU + t * 0.15;
                    let r = 0.32;
                    let pos = [0.5 + ang.cos() * r / aspect, 0.5 + ang.sin() * r];
                    // tangential with a pull toward the middle
                    let tang = [-ang.sin(), ang.cos()];
                    let inward = [-ang.cos(), -ang.sin()];
                    (pos, [tang[0] * 0.8 + inward[0] * 0.6, tang[1] * 0.8 + inward[1] * 0.6])
                }
            };
            let len = (dir[0] * dir[0] + dir[1] * dir[1]).sqrt().max(1e-4);
            let mag = force * e * e * 9.0;
            let col_t = if by_band { fi } else { (fi * 0.5 + t * 0.03).fract() };
            out.push(Splat {
                pos_radius: [pos[0], pos[1], 0.00025 + 0.0012 * e, 0.0],
                force: [dir[0] / len * mag, dir[1] / len * mag, 0.0, 0.0],
                // colour.x = palette position, colour.y = amount (resolved against the palette below)
                color: [col_t, ink * e * 0.9, 0.0, 0.0],
            });
        }
        // kick: a ring of outward pushes
        let splash = p.f("splash");
        if a.hits[0] != self.last_hits[0] && splash > 0.0 {
            let cx = 0.25 + self.rand() * 0.5;
            let cy = 0.25 + self.rand() * 0.5;
            let base = self.rand() * 6.283;
            for k in 0..6 {
                let ang = base + k as f32 / 6.0 * std::f32::consts::TAU;
                let mag = splash * a.onset[0] * 14.0;
                out.push(Splat {
                    pos_radius: [cx + ang.cos() * 0.03 / aspect, cy + ang.sin() * 0.03, 0.0015, 0.0],
                    force: [ang.cos() * mag, ang.sin() * mag, 0.0, 0.0],
                    color: [(k as f32 / 6.0 + self.rand() * 0.2).fract(), ink * 1.2, 0.0, 0.0],
                });
            }
        }
        // snare: a sideways streak; hats: tiny sparkles
        if a.hits[1] != self.last_hits[1] {
            let y = 0.2 + self.rand() * 0.6;
            let from_left = self.rand() < 0.5;
            out.push(Splat {
                pos_radius: [if from_left { 0.05 } else { 0.95 }, y, 0.001, 0.0],
                force: [if from_left { 9.0 } else { -9.0 } * a.onset[1] * splash, 0.0, 0.0, 0.0],
                color: [0.5 + self.rand() * 0.3, ink, 0.0, 0.0],
            });
        }
        if a.hits[2] != self.last_hits[2] {
            for _ in 0..2 {
                let ang = self.rand() * 6.283;
                out.push(Splat {
                    pos_radius: [self.rand(), self.rand(), 0.0002, 0.0],
                    force: [ang.cos() * 2.0, ang.sin() * 2.0, 0.0, 0.0],
                    color: [0.85 + self.rand() * 0.15, ink * 0.6, 0.0, 0.0],
                });
            }
        }
        self.last_hits = a.hits;
        out.truncate(MAX_SPLATS);
        out
    }
}

impl Scene for Ink {
    fn reset(&mut self, _gpu: &Gpu) {
        self.clear_pending = true;
    }

    fn render(&mut self, gpu: &Gpu, ctx: &mut SceneCtx) {
        let p = ctx.params;
        let aspect = ctx.size.0 as f32 / ctx.size.1 as f32;
        let sim_h = match p.choice_index("quality") {
            0 => 128,
            1 => 192,
            3 => 384,
            _ => 256,
        };
        let sim = (((sim_h as f32) * aspect).round() as u32, sim_h);
        let dye_h = ctx.size.1.min(2160);
        let dye = (((dye_h as f32) * aspect).round() as u32, dye_h);
        if self.grids.as_ref().map(|g| (g.sim, g.dye)) != Some((sim, dye)) {
            self.grids = Some(self.build(gpu, sim, dye));
            self.clear_pending = true;
        }

        // palette lookups happen on the CPU for splat colours (reads the theme palette)
        let palette = ctx.globals.theme().map(|t| t.palette()).unwrap_or_default();
        let mut splats = self.music_splats(ctx, aspect);
        for s in &mut splats {
            let idx = ((s.color[0].clamp(0.0, 1.0)) * (palette.len().max(1) - 1) as f32) as usize;
            let c = palette.get(idx).copied().unwrap_or([1.0, 1.0, 1.0]);
            let amt = s.color[1];
            s.color = [c[0] * amt, c[1] * amt, c[2] * amt, 1.0];
        }
        let count = splats.len();
        let mut data = splats;
        data.resize(MAX_SPLATS.max(1), Splat::default());
        gpu.queue.write_buffer(&self.splats, 0, bytemuck::cast_slice(&data));

        let dt = ctx.info.dt.clamp(0.0, 1.0 / 30.0);
        let base = InkUniform {
            sim_texel: [1.0 / sim.0 as f32, 1.0 / sim.1 as f32],
            dye_texel: [1.0 / dye.0 as f32, 1.0 / dye.1 as f32],
            dt,
            vel_dissipation: 0.05 + p.f("viscosity") * 2.5,
            dye_dissipation: 0.05 + p.f("fade") * 1.6,
            curl_strength: p.f("curl") * 60.0,
            pressure_decay: 0.8,
            shading: p.f("shading"),
            splat_count: count as u32,
            brightness: p.f("brightness"),
            swirl: (0.4 * ctx.info.audio.bass + 0.1) * p.f("swirl") * 30.0,
            time: ctx.info.time,
            aspect,
            mode: 0,
        };
        gpu.queue.write_buffer(&self.u_vel, 0, bytemuck::bytes_of(&base));
        gpu.queue.write_buffer(&self.u_dye, 0, bytemuck::bytes_of(&InkUniform { mode: 1, ..base }));

        let g = self.grids.as_ref().unwrap();
        let enc = &mut *ctx.encoder;
        let globals = &ctx.globals.bind_group;
        let run = |enc: &mut wgpu::CommandEncoder, label: &str, view: &wgpu::TextureView, pipe: &wgpu::RenderPipeline, bg: &wgpu::BindGroup| {
            gpu::fullscreen(enc, label, view, pipe, &[globals, bg], None);
        };
        if self.clear_pending {
            for t in g.vel.iter().chain(g.dye_tex.iter()).chain(g.pressure.iter()) {
                gpu::pass(enc, "ink-clear", &t.1, Some(wgpu::Color::TRANSPARENT));
            }
            self.clear_pending = false;
        }
        let (mut vi, mut di, mut pi) = (self.vi, self.di, self.pi);
        // splats
        if count > 0 {
            run(enc, "ink-splat-v", &g.vel[1 - vi].1, &self.p_splat_vec, &g.splat_vel[vi]);
            vi = 1 - vi;
            run(enc, "ink-splat-d", &g.dye_tex[1 - di].1, &self.p_splat_vec, &g.splat_dye[di]);
            di = 1 - di;
        }
        // vorticity
        run(enc, "ink-curl", &g.curl.1, &self.p_curl, &g.curl_bg[vi]);
        run(enc, "ink-vort", &g.vel[1 - vi].1, &self.p_vort, &g.vort[vi]);
        vi = 1 - vi;
        // projection
        run(enc, "ink-div", &g.div.1, &self.p_div, &g.div_bg[vi]);
        run(enc, "ink-pdecay", &g.pressure[1 - pi].1, &self.p_pdecay, &g.pdecay[pi]);
        pi = 1 - pi;
        for _ in 0..JACOBI {
            run(enc, "ink-jacobi", &g.pressure[1 - pi].1, &self.p_jacobi, &g.jacobi[pi]);
            pi = 1 - pi;
        }
        run(enc, "ink-grad", &g.vel[1 - vi].1, &self.p_grad, &g.grad[pi][vi]);
        vi = 1 - vi;
        // advection
        run(enc, "ink-adv-v", &g.vel[1 - vi].1, &self.p_advect_vec, &g.adv_vel[vi]);
        vi = 1 - vi;
        run(enc, "ink-adv-d", &g.dye_tex[1 - di].1, &self.p_advect_vec, &g.adv_dye[vi][di]);
        di = 1 - di;
        // display
        run(enc, "ink-display", ctx.target, &self.p_display, &g.display[di][vi]);
        self.vi = vi;
        self.di = di;
        self.pi = pi;
    }
}
