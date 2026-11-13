//! A line-for-line port of cava's `cavacore.c` (MIT, Copyright (c) 2015 Karl Stavestrand) plus the
//! per-frame post-processing cava.c applies (sensitivity, clamping, monstercat/waves, stereo layout).
//!
//! The arithmetic deliberately mirrors C's mixed float/double/int semantics so the bars match cava's
//! (see `tests/cava_golden.rs`, which compares against the real C code).

// Kept index-for-index with cavacore.c so it can be compared line by line.
#![allow(clippy::needless_range_loop)]

use realfft::num_complex::Complex;
use realfft::{RealFftPlanner, RealToComplex};
use std::f64::consts::PI;
use std::sync::Arc;

/// How cava turns FFT magnitudes into bar heights (`[general] scaling`).
#[derive(Clone, Copy, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum Scaling {
    Linear,
    Decibel,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CavaInitError(pub String);

impl std::fmt::Display for CavaInitError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for CavaInitError {}

/// Port of `struct cava_plan` + `cava_init`/`cava_execute`.
pub struct CavaPlan {
    pub number_of_bars: usize,
    pub audio_channels: usize,
    pub rate: u32,
    pub autosens: i32,
    sens_init: bool,
    pub sens: f64,
    pub framerate: f64,
    frame_skip: u32,
    pub noise_reduction: f64,
    pub scaling: Scaling,

    pub fft_bass_size: usize,
    pub fft_size: usize,
    pub input_buffer_size: usize,
    input_buffer: Vec<f64>,

    pub lower_cut_off: Vec<i32>,
    pub upper_cut_off: Vec<i32>,
    pub eq: Vec<f64>,
    pub cut_off_frequency: Vec<f32>,
    pub bass_cut_off_bar: usize,

    fall: Vec<f64>,
    mem: Vec<f64>,
    peak: Vec<f64>,
    prev_out: Vec<f64>,

    bass_multiplier: Vec<f64>,
    multiplier: Vec<f64>,

    fft_bass: Arc<dyn RealToComplex<f64>>,
    fft: Arc<dyn RealToComplex<f64>>,
    in_bass: [Vec<f64>; 2],
    in_mid: [Vec<f64>; 2],
    out_bass: [Vec<Complex<f64>>; 2],
    out_mid: [Vec<Complex<f64>>; 2],
    scratch: Vec<Complex<f64>>,
}

impl CavaPlan {
    /// `cava_init`. `noise_reduction` is cava's config value divided by 100 (0..1).
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        number_of_bars: usize,
        rate: u32,
        channels: usize,
        autosens: i32,
        noise_reduction: f64,
        low_cut_off: i32,
        high_cut_off: i32,
        scaling: Scaling,
    ) -> Result<Self, CavaInitError> {
        if !(1..=2).contains(&channels) {
            return Err(CavaInitError(format!("illegal number of channels: {channels}, supported are 1 and 2")));
        }
        if rate < 1 {
            return Err(CavaInitError(format!("illegal sample rate: {rate}")));
        }
        let mut fft_buffer_size: usize = 512;
        if rate > 8125 && rate <= 16250 {
            fft_buffer_size *= 2;
        } else if rate > 16250 && rate <= 32500 {
            fft_buffer_size *= 4;
        } else if rate > 32500 && rate <= 75000 {
            fft_buffer_size *= 8;
        } else if rate > 75000 && rate <= 150000 {
            fft_buffer_size *= 16;
        } else if rate > 150000 && rate <= 300000 {
            fft_buffer_size *= 32;
        } else if rate > 300000 && rate <= 705600 {
            fft_buffer_size *= 64;
        } else if rate > 705600 {
            fft_buffer_size *= 128;
        }
        if number_of_bars < 1 {
            return Err(CavaInitError(format!("illegal number of bars: {number_of_bars}")));
        }
        if number_of_bars > fft_buffer_size / 2 + 1 {
            return Err(CavaInitError(format!(
                "for {rate} sample rate the number of bars can't be more than {}",
                fft_buffer_size / 2 + 1
            )));
        }
        if low_cut_off < 1 || high_cut_off < 1 {
            return Err(CavaInitError("low_cut_off must be a positive value".into()));
        }
        if low_cut_off >= high_cut_off {
            return Err(CavaInitError("high_cut_off must be a higher than low_cut_off".into()));
        }
        if high_cut_off as u32 > rate / 2 {
            return Err(CavaInitError("high_cut_off can't be higher than sample rate / 2 (Nyquist)".into()));
        }

        let fft_bass_size = fft_buffer_size * 2;
        let fft_size = fft_buffer_size;
        let input_buffer_size = fft_bass_size * channels;
        let bars = number_of_bars;

        let bass_multiplier = (0..fft_bass_size)
            .map(|i| 0.5 * (1.0 - (2.0 * PI * i as f64 / (fft_bass_size - 1) as f64).cos()))
            .collect();
        let multiplier =
            (0..fft_size).map(|i| 0.5 * (1.0 - (2.0 * PI * i as f64 / (fft_size - 1) as f64).cos())).collect();

        let mut planner = RealFftPlanner::<f64>::new();
        let fft_bass = planner.plan_fft_forward(fft_bass_size);
        let fft = planner.plan_fft_forward(fft_size);
        let scratch_len = fft_bass.get_scratch_len().max(fft.get_scratch_len());

        let mut lower = vec![0i32; bars + 1];
        let mut upper = vec![0i32; bars + 1];
        let mut eq = vec![0f64; bars + 1];
        let mut cof = vec![0f32; bars + 1];
        let mut rel = vec![0f32; bars + 1];

        // process: calculate cutoff frequencies and eq
        let lower_cut_off = low_cut_off;
        let upper_cut_off = high_cut_off;
        let bass_cut_off: i32 = 100;

        // calculate frequency constant (used to distribute bars across the frequency band)
        let frequency_constant: f64 = ((lower_cut_off as f32 / upper_cut_off as f32) as f64).log10()
            / ((1.0f32 / (bars as f32 + 1.0) - 1.0) as f64);

        let mut bass_cut_off_bar: usize = 0;
        let mut first_bar = true;
        // C: `float min_bandwidth = p->rate / p->FFTbassbufferSize;` (integer division)
        let min_bandwidth: f32 = (rate / fft_bass_size as u32) as f32;
        let half_rate = (rate / 2) as f32;
        let bass_half = (fft_bass_size / 2) as i32;
        let half = (fft_size / 2) as i32;

        for n in 0..bars + 1 {
            let mut bar_distribution_coefficient = -frequency_constant;
            bar_distribution_coefficient += ((n as f32 + 1.0) / (bars as f32 + 1.0)) as f64 * frequency_constant;
            cof[n] = (upper_cut_off as f64 * 10f64.powf(bar_distribution_coefficient)) as f32;

            if n > 0 && cof[n - 1] >= cof[n] {
                cof[n] = cof[n - 1] + min_bandwidth;
            }

            // remember nyquist!
            rel[n] = cof[n] / half_rate;

            if cof[n] < bass_cut_off as f32 {
                // BASS
                lower[n] = (rel[n] * bass_half as f32) as i32;
                bass_cut_off_bar += 1;
                if bass_cut_off_bar > 1 {
                    first_bar = false;
                }
                if lower[n] > bass_half {
                    lower[n] = bass_half;
                }
            } else {
                // MID + TREBLE
                lower[n] = ((rel[n] * half as f32) as f64).ceil() as i32;
                if n == bass_cut_off_bar {
                    first_bar = true;
                    if n > 0 {
                        upper[n - 1] = (rel[n] * bass_half as f32 - 1.0) as i32;
                    }
                } else {
                    first_bar = false;
                }
                if lower[n] > half {
                    lower[n] = half;
                }
            }

            if n > 0 {
                if !first_bar {
                    upper[n - 1] = lower[n] - 1;

                    // pushing the spectrum up if the exponential function gets "clumped" in the
                    // bass and calculating new cut off frequencies
                    if lower[n] <= lower[n - 1] {
                        let room_for_more = if n < bass_cut_off_bar {
                            lower[n - 1] + 1 < bass_half + 1
                        } else {
                            lower[n - 1] + 1 < half + 1
                        };
                        if room_for_more {
                            lower[n] = lower[n - 1] + 1;
                            upper[n - 1] = lower[n] - 1;
                        }
                    }
                } else if upper[n - 1] < lower[n - 1] {
                    upper[n - 1] = lower[n - 1] + 1;
                }
            }

            // calculate actual cut off frequency
            if n < bass_cut_off_bar {
                rel[n] = lower[n] as f32 / (fft_bass_size as f32 / 2.0);
            } else {
                rel[n] = lower[n] as f32 / (fft_size as f32 / 2.0);
            }
            cof[n] = rel[n] * (rate as f32 / 2.0);
        }

        // hard coded eq
        for n in 0..bars {
            eq[n] = 1.0 / 2f64.powi(28);
            eq[n] *= (cof[n + 1] as f64).powf(0.85);
            if n < bass_cut_off_bar {
                eq[n] /= (fft_bass_size as f64).log2();
            } else {
                eq[n] /= (fft_size as f64).log2();
            }
            eq[n] /= (upper[n] - lower[n] + 1) as f64;
        }

        let channel_bars = bars * channels;
        Ok(Self {
            number_of_bars: bars,
            audio_channels: channels,
            rate,
            autosens,
            sens_init: true,
            sens: 1.0,
            framerate: 75.0,
            frame_skip: 1,
            noise_reduction,
            scaling,
            fft_bass_size,
            fft_size,
            input_buffer_size,
            input_buffer: vec![0.0; input_buffer_size],
            lower_cut_off: lower,
            upper_cut_off: upper,
            eq,
            cut_off_frequency: cof,
            bass_cut_off_bar,
            fall: vec![0.0; channel_bars],
            mem: vec![0.0; channel_bars],
            peak: vec![0.0; channel_bars],
            prev_out: vec![0.0; channel_bars],
            bass_multiplier,
            multiplier,
            in_bass: [vec![0.0; fft_bass_size], vec![0.0; fft_bass_size]],
            in_mid: [vec![0.0; fft_size], vec![0.0; fft_size]],
            out_bass: [
                vec![Complex::new(0.0, 0.0); fft_bass_size / 2 + 1],
                vec![Complex::new(0.0, 0.0); fft_bass_size / 2 + 1],
            ],
            out_mid: [vec![Complex::new(0.0, 0.0); fft_size / 2 + 1], vec![Complex::new(0.0, 0.0); fft_size / 2 + 1]],
            fft_bass,
            fft,
            scratch: vec![Complex::new(0.0, 0.0); scratch_len],
        })
    }

    /// `cava_execute`: `input` holds `new_samples` interleaved samples (int16 range in linear
    /// mode, like cava's input backends deliver). `out` receives `bars * channels` values: all left
    /// bars, then all right bars.
    pub fn execute(&mut self, input: &[f64], out: &mut [f64]) {
        let mut new_samples = input.len();
        if new_samples > self.input_buffer_size {
            new_samples = self.input_buffer_size;
        }
        let mut silence = true;
        if new_samples > 0 {
            self.framerate -= self.framerate / 64.0;
            let frames = (new_samples / self.audio_channels) as f64;
            self.framerate += (self.rate as f64 * self.frame_skip as f64) / frames / 64.0;
            self.frame_skip = 1;

            // shifting input buffer
            let size = self.input_buffer_size;
            self.input_buffer.copy_within(0..size - new_samples, new_samples);

            // fill the input buffer (newest sample first)
            for (n, &sample) in input.iter().take(new_samples).enumerate() {
                self.input_buffer[new_samples - n - 1] = match self.scaling {
                    Scaling::Decibel => sample / 32768.0,
                    Scaling::Linear => sample,
                };
                if sample != 0.0 {
                    silence = false;
                }
            }
        } else {
            self.frame_skip += 1;
        }

        let stereo = self.audio_channels == 2;
        // fill the bass, mid and treble buffers, applying the Hann window
        for n in 0..self.fft_bass_size {
            if stereo {
                self.in_bass[1][n] = self.bass_multiplier[n] * self.input_buffer[n * 2];
                self.in_bass[0][n] = self.bass_multiplier[n] * self.input_buffer[n * 2 + 1];
            } else {
                self.in_bass[0][n] = self.bass_multiplier[n] * self.input_buffer[n];
            }
        }
        for n in 0..self.fft_size {
            if stereo {
                self.in_mid[1][n] = self.multiplier[n] * self.input_buffer[n * 2];
                self.in_mid[0][n] = self.multiplier[n] * self.input_buffer[n * 2 + 1];
            } else {
                self.in_mid[0][n] = self.multiplier[n] * self.input_buffer[n];
            }
        }

        // process: execute FFT and sort frequency bands
        for ch in 0..self.audio_channels {
            // realfft consumes its input buffer, which is refilled every call above.
            let _ =
                self.fft_bass.process_with_scratch(&mut self.in_bass[ch], &mut self.out_bass[ch], &mut self.scratch);
            let _ = self.fft.process_with_scratch(&mut self.in_mid[ch], &mut self.out_mid[ch], &mut self.scratch);
        }

        let bars = self.number_of_bars;
        for n in 0..bars {
            for ch in 0..self.audio_channels {
                let mut temp = 0.0f64;
                let lo = self.lower_cut_off[n];
                let hi = self.upper_cut_off[n];
                if lo <= hi {
                    for i in lo..=hi {
                        let c = if n < self.bass_cut_off_bar {
                            self.out_bass[ch][i as usize]
                        } else {
                            self.out_mid[ch][i as usize]
                        };
                        temp += c.re.hypot(c.im);
                    }
                }
                match self.scaling {
                    Scaling::Decibel => {
                        const MAX_DB: f64 = 70.0;
                        temp = 20.0 * temp.log10() / MAX_DB;
                        if !temp.is_finite() {
                            temp = 0.0;
                        }
                    }
                    Scaling::Linear => temp *= self.eq[n],
                }
                out[n + ch * bars] = if temp.is_finite() { temp } else { 0.0 };
            }
        }

        let total = bars * self.audio_channels;
        // applying sens or getting max value
        if self.autosens != 0 {
            for v in out.iter_mut().take(total) {
                *v *= self.sens;
            }
        }

        // process [smoothing]
        let mut overshoot = false;
        let framerate_mod = 66.0 / self.framerate;
        let gravity_mod = framerate_mod.powf(2.5) * 2.0 / self.noise_reduction;
        let integral_mod = framerate_mod.powf(0.1);

        for n in 0..total {
            // falloff
            if out[n] < self.prev_out[n] && self.noise_reduction > 0.1 {
                out[n] = self.peak[n] * (1.0 - (self.fall[n] * self.fall[n] * gravity_mod));
                if out[n] < 0.0 {
                    out[n] = 0.0;
                }
                self.fall[n] += 0.028;
            } else {
                self.peak[n] = out[n];
                self.fall[n] = 0.0;
            }
            self.prev_out[n] = out[n];

            // integral
            out[n] += self.mem[n] * self.noise_reduction / integral_mod;
            self.mem[n] = out[n];
            if self.autosens != 0 && out[n] > 1.0 {
                overshoot = true;
                out[n] = 1.0;
            }
        }

        // calculating automatic sense adjustment
        if self.autosens != 0 {
            if overshoot {
                self.sens *= 1.0 - 0.02 * framerate_mod;
                self.sens_init = false;
            } else if !silence {
                self.sens *= 1.0 + 0.001 * framerate_mod * self.autosens as f64;
                if self.sens_init {
                    self.sens *= 1.0 + 0.1 * framerate_mod;
                }
            }
        }
    }
}

/// cava.c's `monstercat_filter` (also handles `waves`), operating on bar heights in display units.
pub fn monstercat_filter(bars: &mut [f32], waves: bool, monstercat: f64, height: i32) {
    let number_of_bars = bars.len();
    if number_of_bars == 0 {
        return;
    }
    let height_normalizer: f32 = if height > 1000 { height as f32 / 912.76 } else { 1.0 };
    if waves {
        for z in 0..number_of_bars {
            bars[z] /= 1.25;
            let bz = bars[z] as f64;
            for m_y in (0..z).rev() {
                let de = (z - m_y) as f64;
                let v = bz - height_normalizer as f64 * (de * de);
                bars[m_y] = (v as f32).max(bars[m_y]);
            }
            for m_y in z + 1..number_of_bars {
                let de = (m_y - z) as f64;
                let v = bz - height_normalizer as f64 * (de * de);
                bars[m_y] = (v as f32).max(bars[m_y]);
            }
        }
    } else if monstercat > 0.0 {
        let base = monstercat * 1.5;
        let mut powers = vec![1.0f64; number_of_bars];
        for (z, p) in powers.iter_mut().enumerate().skip(1) {
            *p = base.powi(z as i32);
        }
        for z in 0..number_of_bars {
            for m_y in (0..z).rev() {
                let de = z - m_y;
                bars[m_y] = ((bars[z] as f64 / powers[de]) as f32).max(bars[m_y]);
            }
            for m_y in z + 1..number_of_bars {
                let de = m_y - z;
                bars[m_y] = ((bars[z] as f64 / powers[de]) as f32).max(bars[m_y]);
            }
        }
    }
}

/// cava's `[output] channels` + `mono_option`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum ChannelLayout {
    /// Left bars mirrored on the left half, right bars on the right half (cava's stereo default).
    Stereo,
    MonoAverage,
    MonoLeft,
    MonoRight,
}

/// User-facing cava settings, in cava's own units (as in `~/.config/cava/config`).
#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
#[serde(default)]
pub struct CavaSettings {
    /// Total number of bars across the display (cava's `bars`; 0 = automatic).
    pub bars: usize,
    pub autosens: bool,
    /// Percent; only used when `autosens` is off (cava's `sensitivity`).
    pub sensitivity: f64,
    /// 0..100 (cava's `noise_reduction`).
    pub noise_reduction: f64,
    pub lower_cutoff_freq: i32,
    pub higher_cutoff_freq: i32,
    pub monstercat: f64,
    pub waves: bool,
    pub layout: ChannelLayout,
    pub reverse: bool,
    pub scaling: Scaling,
}

impl Default for CavaSettings {
    fn default() -> Self {
        Self {
            bars: 64,
            autosens: true,
            sensitivity: 100.0,
            noise_reduction: 77.0,
            lower_cutoff_freq: 50,
            higher_cutoff_freq: 10000,
            monstercat: 0.0,
            waves: false,
            layout: ChannelLayout::Stereo,
            reverse: false,
            scaling: Scaling::Linear,
        }
    }
}

/// Virtual display height used for cava's monstercat/waves maths (pixel units in cava's GUI outputs).
pub const VIRTUAL_HEIGHT: i32 = 1000;

/// cava's whole bar pipeline: plan + sensitivity + clamping + filter + channel layout.
pub struct CavaBars {
    plan: CavaPlan,
    settings: CavaSettings,
    raw: Vec<f64>,
    left: Vec<f32>,
    right: Vec<f32>,
    /// Final bars, 0..1, in display order (len = total bars).
    pub bars: Vec<f32>,
    /// Per-channel bars, 0..1, low to high frequency.
    pub bars_left: Vec<f32>,
    pub bars_right: Vec<f32>,
    pub cut_off_frequency: Vec<f32>,
}

impl CavaBars {
    pub fn new(settings: &CavaSettings, rate: u32) -> Result<Self, CavaInitError> {
        let mut settings = settings.clone();
        if settings.bars == 0 {
            settings.bars = 64;
        }
        let output_channels = if settings.layout == ChannelLayout::Stereo { 2 } else { 1 };
        // cava requires an even number of bars in stereo
        if output_channels == 2 && settings.bars % 2 == 1 {
            settings.bars += 1;
        }
        let per_channel = (settings.bars / output_channels).max(1);
        let nyquist = (rate / 2) as i32;
        let high = settings.higher_cutoff_freq.min(nyquist);
        let low = settings.lower_cutoff_freq.max(1).min(high - 1);
        let plan = CavaPlan::new(
            per_channel,
            rate,
            2,
            settings.autosens as i32,
            (settings.noise_reduction / 100.0).clamp(0.0, 1.0),
            low,
            high,
            settings.scaling,
        )?;
        Ok(Self {
            raw: vec![0.0; per_channel * 2],
            left: vec![0.0; per_channel],
            right: vec![0.0; per_channel],
            bars: vec![0.0; per_channel * output_channels],
            bars_left: vec![0.0; per_channel],
            bars_right: vec![0.0; per_channel],
            cut_off_frequency: plan.cut_off_frequency.clone(),
            plan,
            settings,
        })
    }

    pub fn settings(&self) -> &CavaSettings {
        &self.settings
    }

    pub fn plan(&self) -> &CavaPlan {
        &self.plan
    }

    /// Feeds interleaved stereo samples in -1..1 (converted to cava's int16 range).
    pub fn process(&mut self, stereo: &[f32]) {
        let scaled: Vec<f64> = stereo.iter().map(|&s| s as f64 * 32768.0).collect();
        self.process_raw(&scaled);
    }

    /// Feeds interleaved stereo samples already in cava's int16 range.
    pub fn process_raw(&mut self, stereo: &[f64]) {
        self.plan.execute(stereo, &mut self.raw);
        let n = self.plan.number_of_bars;
        let sens = if self.settings.autosens { 1.0 } else { self.settings.sensitivity / 100.0 };
        let h = VIRTUAL_HEIGHT as f64;
        for i in 0..n {
            let l = (self.raw[i] * sens).clamp(0.0, 1.0);
            let r = (self.raw[i + n] * sens).clamp(0.0, 1.0);
            self.left[i] = (l * h) as f32;
            self.right[i] = (r * h) as f32;
        }
        // Like cava, the filter only runs when monstercat is set; `waves` then takes precedence.
        if self.settings.monstercat > 0.0 {
            let mc = self.settings.monstercat;
            monstercat_filter(&mut self.left, self.settings.waves, mc, VIRTUAL_HEIGHT);
            monstercat_filter(&mut self.right, self.settings.waves, mc, VIRTUAL_HEIGHT);
        }
        let inv = 1.0 / VIRTUAL_HEIGHT as f32;
        for i in 0..n {
            self.bars_left[i] = (self.left[i] * inv).clamp(0.0, 1.0);
            self.bars_right[i] = (self.right[i] * inv).clamp(0.0, 1.0);
        }
        let rev = self.settings.reverse;
        match self.settings.layout {
            ChannelLayout::Stereo => {
                let total = n * 2;
                for k in 0..total {
                    self.bars[k] = if k < n {
                        if rev {
                            self.bars_left[k]
                        } else {
                            self.bars_left[n - k - 1]
                        }
                    } else if rev {
                        self.bars_right[total - k - 1]
                    } else {
                        self.bars_right[k - n]
                    };
                }
            }
            layout => {
                for k in 0..n {
                    let v = match layout {
                        ChannelLayout::MonoLeft => self.bars_left[k],
                        ChannelLayout::MonoRight => self.bars_right[k],
                        _ => (self.bars_left[k] + self.bars_right[k]) / 2.0,
                    };
                    let idx = if rev { n - k - 1 } else { k };
                    self.bars[idx] = v;
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_bad_configs() {
        assert!(CavaPlan::new(0, 48000, 2, 1, 0.77, 50, 10000, Scaling::Linear).is_err());
        assert!(CavaPlan::new(10, 48000, 3, 1, 0.77, 50, 10000, Scaling::Linear).is_err());
        assert!(CavaPlan::new(10, 48000, 2, 1, 0.77, 500, 400, Scaling::Linear).is_err());
        assert!(CavaPlan::new(10, 16000, 2, 1, 0.77, 50, 10000, Scaling::Linear).is_err());
    }

    #[test]
    fn cutoffs_are_monotonic_and_span_the_range() {
        let p = CavaPlan::new(32, 48000, 2, 1, 0.77, 50, 10000, Scaling::Linear).unwrap();
        assert_eq!(p.fft_size, 4096);
        assert_eq!(p.fft_bass_size, 8192);
        for n in 1..=32 {
            assert!(p.cut_off_frequency[n] >= p.cut_off_frequency[n - 1]);
        }
        assert!(p.cut_off_frequency[0] < 60.0);
        assert!(p.cut_off_frequency[32] > 9000.0);
    }

    #[test]
    fn a_tone_lights_up_the_right_bar() {
        let settings = CavaSettings { bars: 32, layout: ChannelLayout::MonoAverage, ..Default::default() };
        let mut cava = CavaBars::new(&settings, 48000).unwrap();
        let freq = 1000.0f64;
        let mut t = 0usize;
        for _ in 0..120 {
            let chunk: Vec<f32> = (0..800)
                .map(|i| {
                    let s = ((t + i / 2) as f64 * 2.0 * PI * freq / 48000.0).sin() * 0.5;
                    s as f32
                })
                .collect();
            t += 400;
            cava.process(&chunk);
        }
        let (argmax, _) = cava.bars.iter().enumerate().max_by(|a, b| a.1.partial_cmp(b.1).unwrap()).unwrap();
        let cof = &cava.cut_off_frequency;
        assert!(
            cof[argmax] <= 1000.0 * 1.15 && cof[argmax + 1] >= 1000.0 * 0.85,
            "peak bar {argmax} spans {}..{} Hz",
            cof[argmax],
            cof[argmax + 1]
        );
        assert!(cava.bars[argmax] > 0.5);
    }

    #[test]
    fn silence_stays_flat() {
        let mut cava = CavaBars::new(&CavaSettings::default(), 44100).unwrap();
        for _ in 0..60 {
            cava.process(&[0.0; 735 * 2]);
        }
        assert!(cava.bars.iter().all(|&b| b == 0.0));
    }

    #[test]
    fn monstercat_spreads_peaks() {
        let mut bars = vec![0.0, 0.0, 800.0, 0.0, 0.0];
        monstercat_filter(&mut bars, false, 1.0, VIRTUAL_HEIGHT);
        assert!(bars[1] > 0.0 && bars[3] > 0.0);
        assert!(bars[1] < 800.0);
        let mut w = vec![0.0, 0.0, 800.0, 0.0, 0.0];
        monstercat_filter(&mut w, true, 1.0, VIRTUAL_HEIGHT);
        assert!((w[2] - 640.0).abs() < 1e-3);
        assert!((w[1] - 639.0).abs() < 1e-3);
    }
}
