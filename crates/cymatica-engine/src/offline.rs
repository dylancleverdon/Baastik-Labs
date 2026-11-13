//! Headless rendering to images: tests, previews, screenshots and demo videos.

use crate::globals::FrameInfo;
use crate::gpu::Gpu;
use crate::params::ParamSet;
use crate::post::OutputMode;
use crate::renderer::{RenderRequest, Renderer};
use crate::theme::Theme;
use anyhow::Result;
use cymatica_audio::analysis::{Analyzer, AudioFrame};
use cymatica_audio::cava::CavaSettings;
use cymatica_audio::synth::DemoSynth;

pub const OFFLINE_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Rgba8UnormSrgb;

/// Feeds audio to an analyzer in step with a video frame rate.
pub struct OfflineAudio {
    analyzer: Analyzer,
    source: AudioSource,
    rate: u32,
    consumed: usize,
    frame: AudioFrame,
}

#[allow(clippy::large_enum_variant)] // one per renderer
enum AudioSource {
    Demo(DemoSynth),
    Samples(Vec<f32>),
    Silence,
}

impl OfflineAudio {
    pub fn demo(rate: u32) -> Self {
        Self::with(rate, AudioSource::Demo(DemoSynth::new(rate)))
    }

    pub fn samples(rate: u32, interleaved: Vec<f32>) -> Self {
        Self::with(rate, AudioSource::Samples(interleaved))
    }

    pub fn silence(rate: u32) -> Self {
        Self::with(rate, AudioSource::Silence)
    }

    fn with(rate: u32, source: AudioSource) -> Self {
        Self {
            analyzer: Analyzer::new(rate, &CavaSettings::default()).expect("analyzer"),
            source,
            rate,
            consumed: 0,
            frame: AudioFrame::default(),
        }
    }

    pub fn set_cava(&mut self, settings: &CavaSettings) {
        let _ = self.analyzer.set_cava_settings(settings);
    }

    /// Analyses audio up to `seconds` and returns the latest frame.
    pub fn advance_to(&mut self, seconds: f64) -> &AudioFrame {
        let target = (seconds * self.rate as f64) as usize;
        while self.consumed < target {
            let n = (target - self.consumed).min(1024);
            let mut buf = vec![0.0f32; n * 2];
            match &mut self.source {
                AudioSource::Demo(s) => s.render(&mut buf),
                AudioSource::Samples(v) => {
                    let start = self.consumed * 2;
                    for (i, b) in buf.iter_mut().enumerate() {
                        *b = v.get(start + i).copied().unwrap_or(0.0);
                    }
                }
                AudioSource::Silence => {}
            }
            self.consumed += n;
            let frame = &mut self.frame;
            self.analyzer.push(&buf, |f| *frame = f.clone());
        }
        &self.frame
    }
}

pub struct OfflineRenderer {
    /// Crossfade seconds used when consecutive renders switch scenes.
    pub crossfade: f32,
    /// Render as for the see-through overlay window (no backdrops, alpha follows brightness).
    pub transparent: bool,
    pub gpu: Gpu,
    _instance: wgpu::Instance,
    pub renderer: Renderer,
    size: (u32, u32),
    target: wgpu::Texture,
    readback: wgpu::Buffer,
    padded_row: u32,
}

impl OfflineRenderer {
    pub fn new(size: (u32, u32)) -> Result<Self> {
        let (instance, gpu) = pollster::block_on(Gpu::headless())?;
        let renderer = Renderer::new(&gpu, OFFLINE_FORMAT);
        let target = gpu.texture(
            "offline",
            size,
            OFFLINE_FORMAT,
            wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
        );
        let padded_row = (size.0 * 4).div_ceil(256) * 256;
        let readback = gpu.buffer(
            "readback",
            (padded_row * size.1) as u64,
            wgpu::BufferUsages::MAP_READ | wgpu::BufferUsages::COPY_DST,
        );
        Ok(Self {
            crossfade: 0.0,
            transparent: false,
            gpu,
            _instance: instance,
            renderer,
            size,
            target,
            readback,
            padded_row,
        })
    }

    pub fn size(&self) -> (u32, u32) {
        self.size
    }

    /// Renders one frame and returns it as RGBA8 (sRGB).
    /// Records one frame into `enc` (shared by `render` and `step`).
    #[allow(clippy::too_many_arguments)]
    fn encode(
        &mut self,
        enc: &mut wgpu::CommandEncoder,
        scene: &str,
        scene_params: &ParamSet,
        look: &ParamSet,
        theme: &Theme,
        mut info: FrameInfo,
        render_scale: f32,
    ) {
        info.transparent |= self.transparent;
        let view = self.target.create_view(&Default::default());
        let rs = (
            ((self.size.0 as f32 * render_scale) as u32).max(16),
            ((self.size.1 as f32 * render_scale) as u32).max(16),
        );
        self.renderer.render(
            &self.gpu,
            enc,
            RenderRequest {
                scene,
                scene_params,
                look,
                theme,
                info,
                render_size: rs,
                output: &view,
                output_mode: OutputMode::SrgbTexture,
                crossfade: self.crossfade,
            },
        );
    }

    /// Like `render`, but without reading the picture back: for letting a simulation develop.
    pub fn step(
        &mut self,
        scene: &str,
        scene_params: &ParamSet,
        look: &ParamSet,
        theme: &Theme,
        info: FrameInfo,
        render_scale: f32,
    ) -> Result<()> {
        let mut enc = self.gpu.device.create_command_encoder(&Default::default());
        let frame = info.frame;
        self.encode(&mut enc, scene, scene_params, look, theme, info, render_scale);
        self.gpu.queue.submit([enc.finish()]);
        if frame % 16 == 15 {
            // don't let hundreds of frames queue up
            self.gpu.device.poll(wgpu::PollType::wait_indefinitely())?;
        }
        Ok(())
    }

    pub fn render(
        &mut self,
        scene: &str,
        scene_params: &ParamSet,
        look: &ParamSet,
        theme: &Theme,
        info: FrameInfo,
        render_scale: f32,
    ) -> Result<image::RgbaImage> {
        let mut enc = self.gpu.device.create_command_encoder(&Default::default());
        self.encode(&mut enc, scene, scene_params, look, theme, info, render_scale);
        enc.copy_texture_to_buffer(
            wgpu::TexelCopyTextureInfo {
                texture: &self.target,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            wgpu::TexelCopyBufferInfo {
                buffer: &self.readback,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(self.padded_row),
                    rows_per_image: Some(self.size.1),
                },
            },
            wgpu::Extent3d { width: self.size.0, height: self.size.1, depth_or_array_layers: 1 },
        );
        self.gpu.queue.submit([enc.finish()]);
        let slice = self.readback.slice(..);
        slice.map_async(wgpu::MapMode::Read, |r| r.expect("map readback"));
        self.gpu.device.poll(wgpu::PollType::wait_indefinitely())?;
        let mut img = image::RgbaImage::new(self.size.0, self.size.1);
        {
            let data = slice.get_mapped_range()?;
            for y in 0..self.size.1 {
                let row = &data[(y * self.padded_row) as usize..(y * self.padded_row + self.size.0 * 4) as usize];
                img.as_mut()[(y * self.size.0 * 4) as usize..((y + 1) * self.size.0 * 4) as usize].copy_from_slice(row);
            }
        }
        self.readback.unmap();
        Ok(img)
    }

    /// Renders `frames` frames at `fps` with the given audio, calling `on_frame` for each.
    #[allow(clippy::too_many_arguments)]
    pub fn render_sequence(
        &mut self,
        scene: &str,
        scene_params: &ParamSet,
        look: &ParamSet,
        theme: &Theme,
        audio: &mut OfflineAudio,
        start: f64,
        fps: f64,
        frames: u64,
        render_scale: f32,
        mut on_frame: impl FnMut(u64, &image::RgbaImage) -> Result<()>,
    ) -> Result<()> {
        // warm the simulation up from `start - 2 s` without saving frames
        let warm = (2.0 * fps) as u64;
        let t0 = (start - warm as f64 / fps).max(0.0);
        let mut n = 0u64;
        loop {
            let t = t0 + n as f64 / fps;
            if t > start + (frames as f64 - 0.5) / fps {
                break;
            }
            let a = audio.advance_to(t).clone();
            let info = FrameInfo::new(t as f32, (1.0 / fps) as f32, n, a);
            let img = self.render(scene, scene_params, look, theme, info, render_scale)?;
            if t >= start - 1e-9 {
                let idx = ((t - start) * fps).round() as u64;
                on_frame(idx, &img)?;
            }
            n += 1;
        }
        Ok(())
    }
}

/// Simple image statistics used by render tests.
pub struct ImageStats {
    pub mean_luma: f32,
    pub max_luma: f32,
    pub lit_fraction: f32,
}

pub fn image_stats(img: &image::RgbaImage) -> ImageStats {
    let mut sum = 0.0f64;
    let mut max = 0.0f32;
    let mut lit = 0usize;
    for p in img.pixels() {
        let l = (0.2126 * p[0] as f32 + 0.7152 * p[1] as f32 + 0.0722 * p[2] as f32) / 255.0;
        sum += l as f64;
        max = max.max(l);
        if l > 0.12 {
            lit += 1;
        }
    }
    let n = (img.width() * img.height()).max(1) as f64;
    ImageStats { mean_luma: (sum / n) as f32, max_luma: max, lit_fraction: (lit as f64 / n) as f32 }
}

/// Mean absolute per-pixel difference (0..1) between two images of the same size.
pub fn image_difference(a: &image::RgbaImage, b: &image::RgbaImage) -> f32 {
    let mut sum = 0u64;
    for (p, q) in a.pixels().zip(b.pixels()) {
        for c in 0..3 {
            sum += (p[c] as i32 - q[c] as i32).unsigned_abs() as u64;
        }
    }
    sum as f32 / (a.width() * a.height() * 3).max(1) as f32 / 255.0
}
