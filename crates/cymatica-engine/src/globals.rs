//! Group 0, shared by every scene: time, the music features, cava's bars, the spectrum, the
//! waveform and the theme palette.

use crate::gpu::{Bind, Gpu};
use crate::theme::{Theme, PALETTE_SIZE};
use bytemuck::{Pod, Zeroable};
use cymatica_audio::analysis::{AudioFrame, SPECTRUM_BINS, WAVE_LEN};

pub const MAX_BARS: usize = 1024;

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
pub struct GlobalsUniform {
    pub resolution: [f32; 2],
    pub inv_resolution: [f32; 2],
    pub time: f32,
    pub dt: f32,
    pub frame: u32,
    pub aspect: f32,
    pub bass: f32,
    pub mid: f32,
    pub treble: f32,
    pub loudness: f32,
    pub kick: f32,
    pub snare: f32,
    pub hat: f32,
    pub beat: f32,
    pub beat_phase: f32,
    pub bpm: f32,
    pub centroid: f32,
    pub flatness: f32,
    pub level_l: f32,
    pub level_r: f32,
    pub correlation: f32,
    pub note_strength: f32,
    pub note: u32,
    pub silent: u32,
    pub bar_count: u32,
    pub wave_len: u32,
    pub hits: [u32; 4],
    pub bands: [[f32; 4]; 2],
    pub chroma: [[f32; 4]; 3],
    pub bg_top: [f32; 4],
    pub bg_bottom: [f32; 4],
    pub accent: [f32; 4],
    pub note_color: [f32; 4],
    pub palette_shift: f32,
    pub energy: f32,
    pub seed: f32,
    pub spectrum_len: u32,
}

/// Everything a frame needs from the outside world.
#[derive(Clone, Debug)]
pub struct FrameInfo {
    pub time: f32,
    pub dt: f32,
    pub frame: u64,
    pub audio: AudioFrame,
    pub palette_shift: f32,
    pub note_color: [f32; 3],
    pub seed: f32,
}

impl FrameInfo {
    pub fn new(time: f32, dt: f32, frame: u64, audio: AudioFrame) -> Self {
        Self { time, dt, frame, audio, palette_shift: 0.0, note_color: [1.0, 1.0, 1.0], seed: 0.0 }
    }
}

pub struct GlobalsGpu {
    pub layout: wgpu::BindGroupLayout,
    pub bind_group: wgpu::BindGroup,
    uniform: wgpu::Buffer,
    spectrum: wgpu::Buffer,
    bars: wgpu::Buffer,
    wave: wgpu::Buffer,
    palette: wgpu::Texture,
    theme: Option<Theme>,
    bg: [[f32; 3]; 2],
    accent: [f32; 3],
}

impl GlobalsGpu {
    pub fn new(gpu: &Gpu) -> Self {
        let layout = gpu.layout(
            "globals",
            &[
                Bind::Uniform,
                Bind::StorageRead,
                Bind::StorageRead,
                Bind::StorageRead,
                Bind::Texture,
                Bind::Sampler,
                Bind::Sampler,
            ],
        );
        let uniform = gpu.uniform_buffer("globals", std::mem::size_of::<GlobalsUniform>() as u64);
        let spectrum = gpu.storage_buffer("spectrum", (SPECTRUM_BINS * 4) as u64);
        let bars = gpu.storage_buffer("bars", (MAX_BARS * 4) as u64);
        let wave = gpu.storage_buffer("wave", (WAVE_LEN * 8) as u64);
        let palette = gpu.texture(
            "palette",
            (PALETTE_SIZE as u32, 1),
            wgpu::TextureFormat::Rgba16Float,
            wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
        );
        let pv = palette.create_view(&Default::default());
        let clamp = gpu.sampler(wgpu::FilterMode::Linear, wgpu::AddressMode::ClampToEdge);
        let repeat = gpu.sampler(wgpu::FilterMode::Linear, wgpu::AddressMode::Repeat);
        let bind_group = gpu.bind_group(
            "globals",
            &layout,
            &[
                uniform.as_entire_binding(),
                spectrum.as_entire_binding(),
                bars.as_entire_binding(),
                wave.as_entire_binding(),
                wgpu::BindingResource::TextureView(&pv),
                wgpu::BindingResource::Sampler(&clamp),
                wgpu::BindingResource::Sampler(&repeat),
            ],
        );
        let mut g = Self {
            layout,
            bind_group,
            uniform,
            spectrum,
            bars,
            wave,
            palette,
            theme: None,
            bg: [[0.0; 3]; 2],
            accent: [1.0; 3],
        };
        g.set_theme(gpu, &crate::theme::builtin_themes()[0]);
        g
    }

    pub fn theme(&self) -> Option<&Theme> {
        self.theme.as_ref()
    }

    pub fn set_theme(&mut self, gpu: &Gpu, theme: &Theme) {
        if self.theme.as_ref() == Some(theme) {
            return;
        }
        let pal = theme.palette();
        let mut data: Vec<half::f16> = Vec::with_capacity(PALETTE_SIZE * 4);
        for c in pal {
            data.extend_from_slice(&[
                half::f16::from_f32(c[0]),
                half::f16::from_f32(c[1]),
                half::f16::from_f32(c[2]),
                half::f16::from_f32(1.0),
            ]);
        }
        gpu.queue.write_texture(
            wgpu::TexelCopyTextureInfo {
                texture: &self.palette,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            bytemuck::cast_slice(&data),
            wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(PALETTE_SIZE as u32 * 8),
                rows_per_image: None,
            },
            wgpu::Extent3d { width: PALETTE_SIZE as u32, height: 1, depth_or_array_layers: 1 },
        );
        self.bg = theme.background_linear();
        self.accent = theme.accent_linear();
        self.theme = Some(theme.clone());
    }

    pub fn update(&self, gpu: &Gpu, info: &FrameInfo, size: (u32, u32)) {
        let a = &info.audio;
        let bar_count = a.bars.len().min(MAX_BARS);
        let mut bands = [[0f32; 4]; 2];
        for (i, b) in a.bands.iter().enumerate() {
            bands[i / 4][i % 4] = *b;
        }
        let mut chroma = [[0f32; 4]; 3];
        for (i, c) in a.chroma.iter().enumerate() {
            chroma[i / 4][i % 4] = *c;
        }
        let u = GlobalsUniform {
            resolution: [size.0 as f32, size.1 as f32],
            inv_resolution: [1.0 / size.0.max(1) as f32, 1.0 / size.1.max(1) as f32],
            time: info.time,
            dt: info.dt,
            frame: info.frame as u32,
            aspect: size.0 as f32 / size.1.max(1) as f32,
            bass: a.bass,
            mid: a.mid,
            treble: a.treble,
            loudness: a.loudness,
            kick: a.onset[0],
            snare: a.onset[1],
            hat: a.onset[2],
            beat: a.beat,
            beat_phase: a.beat_phase,
            bpm: a.bpm,
            centroid: a.centroid,
            flatness: a.flatness,
            level_l: a.level_l,
            level_r: a.level_r,
            correlation: a.correlation,
            note_strength: a.note_strength,
            note: a.note as u32,
            silent: a.silent as u32,
            bar_count: bar_count as u32,
            wave_len: a.waveform.len().min(WAVE_LEN) as u32,
            hits: [a.hits[0], a.hits[1], a.hits[2], a.beat_count],
            bands,
            chroma,
            bg_top: [self.bg[0][0], self.bg[0][1], self.bg[0][2], 1.0],
            bg_bottom: [self.bg[1][0], self.bg[1][1], self.bg[1][2], 1.0],
            accent: [self.accent[0], self.accent[1], self.accent[2], 1.0],
            note_color: [info.note_color[0], info.note_color[1], info.note_color[2], 1.0],
            palette_shift: info.palette_shift,
            energy: a.loudness.max(a.bass * 0.85),
            seed: info.seed,
            spectrum_len: a.spectrum.len().clamp(1, SPECTRUM_BINS) as u32,
        };
        gpu.queue.write_buffer(&self.uniform, 0, bytemuck::bytes_of(&u));
        if !a.spectrum.is_empty() {
            let n = a.spectrum.len().min(SPECTRUM_BINS);
            gpu.queue.write_buffer(&self.spectrum, 0, bytemuck::cast_slice(&a.spectrum[..n]));
        }
        if bar_count > 0 {
            gpu.queue.write_buffer(&self.bars, 0, bytemuck::cast_slice(&a.bars[..bar_count]));
        }
        if !a.waveform.is_empty() {
            let n = a.waveform.len().min(WAVE_LEN);
            let start = a.waveform.len() - n;
            gpu.queue.write_buffer(&self.wave, 0, bytemuck::cast_slice(&a.waveform[start..]));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn uniform_layout_matches_wgsl() {
        // WGSL struct Globals is 288 bytes (see shaders/globals.wgsl).
        assert_eq!(std::mem::size_of::<GlobalsUniform>(), 288);
        assert_eq!(std::mem::offset_of!(GlobalsUniform, hits), 112);
        assert_eq!(std::mem::offset_of!(GlobalsUniform, bands), 128);
        assert_eq!(std::mem::offset_of!(GlobalsUniform, chroma), 160);
        assert_eq!(std::mem::offset_of!(GlobalsUniform, bg_top), 208);
        assert_eq!(std::mem::offset_of!(GlobalsUniform, palette_shift), 272);
    }
}
