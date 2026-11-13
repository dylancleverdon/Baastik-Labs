//! The "look": feedback trails, bloom, grading, tone mapping and output encoding, applied to every
//! scene's HDR image.

use crate::gpu::{self, Bind, Gpu, HDR_FORMAT};
use crate::params::{choice, float, int, ParamDef, Params};
use bytemuck::{Pod, Zeroable};

pub static LOOK_PARAMS: &[ParamDef] = &[
    float("bloom", "Glow", "Glow", 0.0, 3.0, 0.9, "How much bright parts bleed light."),
    float("bloom_threshold", "Glow threshold", "Glow", 0.0, 3.0, 0.7, "Brightness where glow starts."),
    float("bloom_radius", "Glow size", "Glow", 0.0, 1.0, 0.65, "How far the glow spreads."),
    float("exposure", "Exposure", "Color", -3.0, 3.0, 0.0, "Overall brightness (stops)."),
    float("saturation", "Saturation", "Color", 0.0, 2.0, 1.1, "Colour intensity."),
    float("contrast", "Contrast", "Color", 0.5, 1.6, 1.0, "Tonal contrast."),
    float("hue_shift", "Hue shift", "Color", 0.0, 1.0, 0.0, "Rotates every colour around the wheel."),
    float("palette_speed", "Palette cycling", "Color", 0.0, 1.0, 0.0, "Slowly cycles the theme's colours."),
    float(
        "note_tint",
        "Synesthesia",
        "Color",
        0.0,
        1.0,
        0.0,
        "Tints the picture with the colour of the note being played.",
    ),
    choice(
        "note_colors",
        "Note colours",
        "Color",
        &[("scriabin", "Scriabin"), ("fifths", "Circle of fifths"), ("chromatic", "Chromatic")],
        "scriabin",
        "How notes map to colours for Synesthesia.",
    ),
    choice(
        "tonemap",
        "Tone mapping",
        "Color",
        &[("agx", "AgX (filmic)"), ("aces", "ACES"), ("reinhard", "Soft"), ("none", "None")],
        "agx",
        "How bright light rolls off.",
    )
    .fixed(),
    float("kick_zoom", "Kick punch", "Motion", 0.0, 1.0, 0.25, "Zooms in a little on every kick."),
    float("chromatic", "Chromatic aberration", "Lens", 0.0, 1.0, 0.15, "Colour fringing, stronger on hits."),
    float("vignette", "Vignette", "Lens", 0.0, 1.0, 0.35, "Darkens the corners."),
    float("grain", "Film grain", "Lens", 0.0, 1.0, 0.08, "Subtle noise texture."),
    float("scanlines", "Scanlines", "Lens", 0.0, 1.0, 0.0, "CRT-style lines."),
    float("trails", "Trails", "Feedback", 0.0, 0.98, 0.0, "Leaves fading trails (MilkDrop-style feedback)."),
    float("trail_zoom", "Trail zoom", "Feedback", -0.05, 0.05, 0.008, "Trails drift inward (+) or outward (-)."),
    float("trail_rotate", "Trail twist", "Feedback", -0.05, 0.05, 0.0, "Trails spin."),
    int("kaleido", "Kaleidoscope", "Feedback", 0, 16, 0, "Mirrors the picture into N segments (0 = off)."),
];

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct PostUniform {
    resolution: [f32; 2],
    texel: [f32; 2],
    bloom: f32,
    threshold: f32,
    knee: f32,
    radius: f32,
    exposure: f32,
    saturation: f32,
    contrast: f32,
    hue_shift: f32,
    tonemap: u32,
    output_mode: u32,
    headroom: f32,
    chromatic: f32,
    zoom: f32,
    kaleido: f32,
    kaleido_spin: f32,
    vignette: f32,
    grain: f32,
    scanlines: f32,
    time: f32,
    trails: f32,
    trail_zoom: f32,
    trail_rotate: f32,
    note_tint: f32,
    frame: u32,
    note_color: [f32; 4],
}

/// How the composite writes its output.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum OutputMode {
    /// Linear values into an sRGB texture (the hardware encodes).
    SrgbTexture,
    /// Shader encodes sRGB (plain unorm target).
    EncodeSrgb,
    /// EDR: extended linear sRGB (scRGB) with `headroom` (macOS HDR).
    Edr { headroom: f32 },
}

impl OutputMode {
    pub fn for_format(format: wgpu::TextureFormat) -> Self {
        if format.is_srgb() {
            OutputMode::SrgbTexture
        } else if matches!(format, wgpu::TextureFormat::Rgba16Float) {
            OutputMode::Edr { headroom: 1.0 }
        } else {
            OutputMode::EncodeSrgb
        }
    }
}

const BLOOM_MIPS: usize = 6;

/// Per-frame inputs to the look that don't come from parameters.
pub struct LookFrame {
    pub time: f32,
    pub frame: u64,
    pub kick: f32,
    pub note_color: [f32; 3],
}

struct Sized {
    size: (u32, u32),
    mips: Vec<(wgpu::Texture, wgpu::TextureView, (u32, u32))>,
    trail: [(wgpu::Texture, wgpu::TextureView); 2],
}

pub struct Post {
    layout: wgpu::BindGroupLayout,
    uniform_bufs: Vec<wgpu::Buffer>,
    sampler: wgpu::Sampler,
    p_trails: wgpu::RenderPipeline,
    p_down_first: wgpu::RenderPipeline,
    p_down: wgpu::RenderPipeline,
    p_up: wgpu::RenderPipeline,
    p_composite: wgpu::RenderPipeline,
    p_crossfade: wgpu::RenderPipeline,
    output_format: wgpu::TextureFormat,
    sized: Option<Sized>,
    trail_flip: usize,
    dummy: wgpu::TextureView,
}

impl Post {
    pub fn new(gpu: &Gpu, output_format: wgpu::TextureFormat) -> Self {
        let layout = gpu.layout("post", &[Bind::Uniform, Bind::Texture, Bind::Sampler, Bind::Texture]);
        let pl = gpu.pipeline_layout("post", &[&layout]);
        let src = format!("{}\n{}", include_str!("shaders/common.wgsl"), include_str!("shaders/post.wgsl"));
        let module = gpu.shader("post", src);
        let p_trails = gpu.fullscreen_pipeline("trails", &module, "fs_trails", &pl, HDR_FORMAT, None);
        let p_down_first = gpu.fullscreen_pipeline("bloom-down0", &module, "fs_down_first", &pl, HDR_FORMAT, None);
        let p_down = gpu.fullscreen_pipeline("bloom-down", &module, "fs_down", &pl, HDR_FORMAT, None);
        let p_up = gpu.fullscreen_pipeline("bloom-up", &module, "fs_up", &pl, HDR_FORMAT, Some(gpu::ADDITIVE));
        let p_composite = gpu.fullscreen_pipeline("composite", &module, "fs_composite", &pl, output_format, None);
        let p_crossfade = gpu.fullscreen_pipeline("crossfade", &module, "fs_crossfade", &pl, HDR_FORMAT, None);
        // one small uniform buffer per pass (each pass needs its own texel size)
        let uniform_bufs = (0..(2 * BLOOM_MIPS + 4))
            .map(|_| gpu.uniform_buffer("post-u", std::mem::size_of::<PostUniform>() as u64))
            .collect();
        let dummy = gpu.render_texture("dummy", (1, 1), HDR_FORMAT).create_view(&Default::default());
        Self {
            layout,
            uniform_bufs,
            sampler: gpu.sampler(wgpu::FilterMode::Linear, wgpu::AddressMode::ClampToEdge),
            p_trails,
            p_down_first,
            p_down,
            p_up,
            p_composite,
            p_crossfade,
            output_format,
            sized: None,
            trail_flip: 0,
            dummy,
        }
    }

    pub fn output_format(&self) -> wgpu::TextureFormat {
        self.output_format
    }

    fn ensure_size(&mut self, gpu: &Gpu, size: (u32, u32)) {
        if self.sized.as_ref().map(|s| s.size) == Some(size) {
            return;
        }
        let mut mips = Vec::new();
        let mut s = ((size.0 / 2).max(1), (size.1 / 2).max(1));
        for i in 0..BLOOM_MIPS {
            let t = gpu.render_texture(&format!("bloom{i}"), s, HDR_FORMAT);
            let v = t.create_view(&Default::default());
            mips.push((t, v, s));
            s = ((s.0 / 2).max(1), (s.1 / 2).max(1));
        }
        let mk = |n: &str| {
            let t = gpu.render_texture(n, size, HDR_FORMAT);
            let v = t.create_view(&Default::default());
            (t, v)
        };
        self.sized = Some(Sized { size, mips, trail: [mk("trail-a"), mk("trail-b")] });
    }

    fn bind(&self, gpu: &Gpu, buf: &wgpu::Buffer, src: &wgpu::TextureView, aux: &wgpu::TextureView) -> wgpu::BindGroup {
        gpu.bind_group(
            "post",
            &self.layout,
            &[
                buf.as_entire_binding(),
                wgpu::BindingResource::TextureView(src),
                wgpu::BindingResource::Sampler(&self.sampler),
                wgpu::BindingResource::TextureView(aux),
            ],
        )
    }

    /// Blends two HDR scene images (`t` = 0 old, 1 new) into `out`.
    pub fn crossfade(
        &mut self,
        gpu: &Gpu,
        encoder: &mut wgpu::CommandEncoder,
        new: &wgpu::TextureView,
        old: &wgpu::TextureView,
        out: &wgpu::TextureView,
        t: f32,
    ) {
        let u = PostUniform { trails: t.clamp(0.0, 1.0), ..bytemuck::Zeroable::zeroed() };
        let b = &self.uniform_bufs[self.uniform_bufs.len() - 1];
        gpu.queue.write_buffer(b, 0, bytemuck::bytes_of(&u));
        let bg = self.bind(gpu, b, new, old);
        gpu::fullscreen(encoder, "crossfade", out, &self.p_crossfade, &[&bg], None);
    }

    /// Applies the look to `scene` (HDR) and writes the result to `output`.
    #[allow(clippy::too_many_arguments)]
    pub fn run(
        &mut self,
        gpu: &Gpu,
        encoder: &mut wgpu::CommandEncoder,
        scene: &wgpu::TextureView,
        size: (u32, u32),
        output: &wgpu::TextureView,
        mode: OutputMode,
        look: &Params,
        frame: &LookFrame,
    ) {
        self.ensure_size(gpu, size);
        let trails = look.f("trails");
        let kaleido = look.i("kaleido");
        let zoom = 1.0 + look.f("kick_zoom") * 0.06 * frame.kick * frame.kick;
        let base = PostUniform {
            resolution: [size.0 as f32, size.1 as f32],
            texel: [1.0 / size.0 as f32, 1.0 / size.1 as f32],
            bloom: look.f("bloom") * 0.35,
            threshold: look.f("bloom_threshold"),
            knee: 0.5,
            radius: look.f("bloom_radius"),
            exposure: 2f32.powf(look.f("exposure")),
            saturation: look.f("saturation"),
            contrast: look.f("contrast"),
            hue_shift: look.f("hue_shift"),
            tonemap: look.choice_index("tonemap") as u32,
            output_mode: match mode {
                OutputMode::SrgbTexture => 0,
                OutputMode::EncodeSrgb => 1,
                OutputMode::Edr { .. } => 2,
            },
            headroom: match mode {
                OutputMode::Edr { headroom } => headroom,
                _ => 1.0,
            },
            chromatic: look.f("chromatic") * (0.35 + 1.4 * frame.kick),
            zoom,
            kaleido: if kaleido >= 2 { kaleido as f32 } else { 0.0 },
            kaleido_spin: frame.time * 0.05,
            vignette: look.f("vignette"),
            grain: look.f("grain"),
            scanlines: look.f("scanlines"),
            time: frame.time,
            trails,
            trail_zoom: look.f("trail_zoom"),
            trail_rotate: look.f("trail_rotate"),
            note_tint: look.f("note_tint"),
            frame: frame.frame as u32,
            note_color: [frame.note_color[0], frame.note_color[1], frame.note_color[2], 1.0],
        };
        let sized = self.sized.as_ref().unwrap();
        let mut ubuf = 0usize;
        let mut write = |u: PostUniform| {
            let b = &self.uniform_bufs[ubuf];
            gpu.queue.write_buffer(b, 0, bytemuck::bytes_of(&u));
            ubuf += 1;
            b
        };

        // 1. feedback trails
        let mut source: &wgpu::TextureView = scene;
        if trails > 0.001 {
            let prev = &sized.trail[self.trail_flip].1;
            let cur = &sized.trail[1 - self.trail_flip].1;
            let b = write(base);
            let bg = self.bind(gpu, b, scene, prev);
            gpu::fullscreen(encoder, "trails", cur, &self.p_trails, &[&bg], None);
            source = cur;
            self.trail_flip = 1 - self.trail_flip;
        }

        // 2. bloom down chain
        let mips = &sized.mips;
        for i in 0..mips.len() {
            let (input, in_size) = if i == 0 { (source, size) } else { (&mips[i - 1].1, mips[i - 1].2) };
            let mut u = base;
            u.texel = [1.0 / in_size.0 as f32, 1.0 / in_size.1 as f32];
            let b = write(u);
            let bg = self.bind(gpu, b, input, &self.dummy);
            let p = if i == 0 { &self.p_down_first } else { &self.p_down };
            gpu::fullscreen(encoder, "bloom-down", &mips[i].1, p, &[&bg], Some(wgpu::Color::BLACK));
        }
        // 3. bloom up chain (additive)
        for i in (1..mips.len()).rev() {
            let mut u = base;
            u.texel = [1.0 / mips[i].2 .0 as f32, 1.0 / mips[i].2 .1 as f32];
            let b = write(u);
            let bg = self.bind(gpu, b, &mips[i].1, &self.dummy);
            gpu::fullscreen(encoder, "bloom-up", &mips[i - 1].1, &self.p_up, &[&bg], None);
        }
        // 4. composite
        let b = write(base);
        let bg = self.bind(gpu, b, source, &mips[0].1);
        gpu::fullscreen(encoder, "composite", output, &self.p_composite, &[&bg], Some(wgpu::Color::BLACK));
    }
}
