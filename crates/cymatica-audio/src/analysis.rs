//! Turns raw stereo samples into `AudioFrame`s: cava's bars plus the musical features scenes use
//! (spectrum, bands, loudness, onsets, tempo/beat, chroma, stereo image, waveform).

use crate::cava::{CavaBars, CavaInitError, CavaSettings};
use realfft::num_complex::Complex;
use realfft::{RealFftPlanner, RealToComplex};
use std::sync::Arc;

/// Analysis frames per second.
pub const ANALYSIS_RATE: f64 = 120.0;
/// Log-spaced spectrum bins, 30 Hz to 16 kHz.
pub const SPECTRUM_BINS: usize = 256;
pub const SPECTRUM_LOW_HZ: f32 = 30.0;
pub const SPECTRUM_HIGH_HZ: f32 = 16000.0;
/// Stereo samples of waveform history in each frame.
pub const WAVE_LEN: usize = 2048;
pub const NUM_BANDS: usize = 7;
pub const BAND_EDGES: [f32; NUM_BANDS + 1] = [20.0, 60.0, 250.0, 500.0, 2000.0, 4000.0, 6000.0, 20000.0];
pub const BAND_NAMES: [&str; NUM_BANDS] = ["Sub", "Bass", "Low mid", "Mid", "High mid", "Presence", "Air"];
/// Onset detectors: kick, snare, hat.
pub const ONSET_NAMES: [&str; 3] = ["Kick", "Snare", "Hat"];
pub const NOTE_NAMES: [&str; 12] = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

/// Everything the visuals know about the music at one moment. Values are normalized to 0..1
/// unless noted.
#[derive(Clone, Debug)]
pub struct AudioFrame {
    /// Seconds of audio analysed so far (audio clock).
    pub time: f64,
    /// cava's bars in display order (stereo: left mirrored | right).
    pub bars: Vec<f32>,
    pub bars_left: Vec<f32>,
    pub bars_right: Vec<f32>,
    /// Log-frequency spectrum, `SPECTRUM_BINS` values from 30 Hz to 16 kHz.
    pub spectrum: Vec<f32>,
    pub bands: [f32; NUM_BANDS],
    pub bass: f32,
    pub mid: f32,
    pub treble: f32,
    pub loudness: f32,
    /// Raw RMS level in dBFS (not normalized).
    pub rms_db: f32,
    pub peak: f32,
    /// Hit envelopes (jump on a hit, then decay): kick, snare, hat.
    pub onset: [f32; 3],
    /// Hit counters; a change means a new hit.
    pub hits: [u32; 3],
    /// Beat pulse: 1 on the beat, decaying until the next.
    pub beat: f32,
    pub beat_phase: f32,
    pub beat_count: u32,
    pub bpm: f32,
    pub tempo_confidence: f32,
    /// Spectral centroid on a log scale (0 = dark, 1 = bright).
    pub centroid: f32,
    /// Spectral flatness (0 = tonal, 1 = noisy).
    pub flatness: f32,
    pub chroma: [f32; 12],
    /// Dominant pitch class (0 = C).
    pub note: u8,
    pub note_strength: f32,
    pub level_l: f32,
    pub level_r: f32,
    /// -1..1 stereo correlation.
    pub correlation: f32,
    /// Most recent stereo samples, oldest first.
    pub waveform: Vec<[f32; 2]>,
    pub silent: bool,
}

impl Default for AudioFrame {
    fn default() -> Self {
        Self {
            time: 0.0,
            bars: Vec::new(),
            bars_left: Vec::new(),
            bars_right: Vec::new(),
            spectrum: vec![0.0; SPECTRUM_BINS],
            bands: [0.0; NUM_BANDS],
            bass: 0.0,
            mid: 0.0,
            treble: 0.0,
            loudness: 0.0,
            rms_db: -120.0,
            peak: 0.0,
            onset: [0.0; 3],
            hits: [0; 3],
            beat: 0.0,
            beat_phase: 0.0,
            beat_count: 0,
            bpm: 120.0,
            tempo_confidence: 0.0,
            centroid: 0.5,
            flatness: 0.0,
            chroma: [0.0; 12],
            note: 0,
            note_strength: 0.0,
            level_l: 0.0,
            level_r: 0.0,
            correlation: 1.0,
            waveform: vec![[0.0, 0.0]; WAVE_LEN],
            silent: true,
        }
    }
}

fn lerp(a: f32, b: f32, t: f32) -> f32 {
    a + (b - a) * t
}

impl AudioFrame {
    /// Blends two neighbouring frames (continuous values interpolate; events come from `b`).
    pub fn lerp(a: &AudioFrame, b: &AudioFrame, t: f32) -> AudioFrame {
        let t = t.clamp(0.0, 1.0);
        let mut out = b.clone();
        out.time = a.time + (b.time - a.time) * t as f64;
        let mix = |x: &[f32], y: &[f32], dst: &mut Vec<f32>| {
            if x.len() == y.len() {
                for (i, d) in dst.iter_mut().enumerate() {
                    *d = lerp(x[i], y[i], t);
                }
            }
        };
        mix(&a.bars, &b.bars, &mut out.bars);
        mix(&a.bars_left, &b.bars_left, &mut out.bars_left);
        mix(&a.bars_right, &b.bars_right, &mut out.bars_right);
        mix(&a.spectrum, &b.spectrum, &mut out.spectrum);
        for i in 0..NUM_BANDS {
            out.bands[i] = lerp(a.bands[i], b.bands[i], t);
        }
        for i in 0..12 {
            out.chroma[i] = lerp(a.chroma[i], b.chroma[i], t);
        }
        out.bass = lerp(a.bass, b.bass, t);
        out.mid = lerp(a.mid, b.mid, t);
        out.treble = lerp(a.treble, b.treble, t);
        out.loudness = lerp(a.loudness, b.loudness, t);
        out.centroid = lerp(a.centroid, b.centroid, t);
        out.flatness = lerp(a.flatness, b.flatness, t);
        out.level_l = lerp(a.level_l, b.level_l, t);
        out.level_r = lerp(a.level_r, b.level_r, t);
        out
    }

    pub fn note_name(&self) -> &'static str {
        NOTE_NAMES[self.note as usize % 12]
    }
}

/// Exponential smoothing with separate attack and release times (seconds).
#[derive(Clone, Copy, Debug)]
struct Smoother {
    attack: f32,
    release: f32,
}

impl Smoother {
    const fn new(attack: f32, release: f32) -> Self {
        Self { attack, release }
    }
    fn step(&self, current: f32, target: f32, dt: f32) -> f32 {
        let tau = if target > current { self.attack } else { self.release };
        if tau <= 0.0 {
            return target;
        }
        current + (target - current) * (1.0 - (-dt / tau).exp())
    }
}

/// Peak-following reference level for automatic gain (in dB).
#[derive(Clone, Copy, Debug)]
struct Agc {
    ref_db: f32,
    floor_db: f32,
    decay_db_per_s: f32,
}

impl Agc {
    fn new(floor_db: f32, decay_db_per_s: f32) -> Self {
        Self { ref_db: floor_db, floor_db, decay_db_per_s }
    }
    fn update(&mut self, db: f32, dt: f32) -> f32 {
        if db > self.ref_db {
            self.ref_db = db;
        } else {
            self.ref_db = (self.ref_db - self.decay_db_per_s * dt).max(self.floor_db);
        }
        self.ref_db
    }
}

/// Biquad filter (RBJ cookbook), transposed direct form II.
#[derive(Clone, Copy, Debug)]
struct Biquad {
    b0: f32,
    b1: f32,
    b2: f32,
    a1: f32,
    a2: f32,
    z1: f32,
    z2: f32,
}

impl Biquad {
    fn new(b: [f64; 3], a: [f64; 3]) -> Self {
        Self {
            b0: (b[0] / a[0]) as f32,
            b1: (b[1] / a[0]) as f32,
            b2: (b[2] / a[0]) as f32,
            a1: (a[1] / a[0]) as f32,
            a2: (a[2] / a[0]) as f32,
            z1: 0.0,
            z2: 0.0,
        }
    }

    fn coefficients(fs: f32, f0: f32, q: f32) -> (f64, f64) {
        let w0 = 2.0 * std::f64::consts::PI * f0.min(fs * 0.45) as f64 / fs as f64;
        let (s, c) = w0.sin_cos();
        (s / (2.0 * q as f64), c)
    }

    fn lowpass(fs: f32, f0: f32) -> Self {
        let (alpha, c) = Self::coefficients(fs, f0, std::f32::consts::FRAC_1_SQRT_2);
        Self::new([(1.0 - c) / 2.0, 1.0 - c, (1.0 - c) / 2.0], [1.0 + alpha, -2.0 * c, 1.0 - alpha])
    }

    fn highpass(fs: f32, f0: f32) -> Self {
        let (alpha, c) = Self::coefficients(fs, f0, std::f32::consts::FRAC_1_SQRT_2);
        Self::new([(1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0], [1.0 + alpha, -2.0 * c, 1.0 - alpha])
    }

    #[inline]
    fn process(&mut self, x: f32) -> f32 {
        let y = self.b0 * x + self.z1;
        self.z1 = self.b1 * x - self.a1 * y + self.z2;
        self.z2 = self.b2 * x - self.a2 * y;
        y
    }
}

/// Detects hits in one frequency region: the band's energy jumping above its recent level,
/// measured relative to the overall music level so quiet cross-talk from other drums can't
/// trigger it.
struct OnsetDetector {
    filters: Vec<Biquad>,
    power: f64,
    count: u32,
    slow_db: f32,
    slow_tau: f32,
    min_rise_db: f32,
    min_level_db: f32,
    history: Vec<f32>,
    pos: usize,
    filled: usize,
    since_hit: f32,
    refractory: f32,
    decay_half_life: f32,
    envelope: f32,
    hits: u32,
    last_rise: f32,
    last_level: f32,
}

impl OnsetDetector {
    fn new(filters: Vec<Biquad>, refractory: f32, half_life: f32, min_rise_db: f32, min_level_db: f32) -> Self {
        Self {
            filters,
            power: 0.0,
            count: 0,
            slow_db: -60.0,
            slow_tau: 0.08,
            min_rise_db,
            min_level_db,
            history: vec![0.0; 90],
            pos: 0,
            filled: 0,
            since_hit: 10.0,
            refractory,
            decay_half_life: half_life,
            envelope: 0.0,
            hits: 0,
            last_rise: 0.0,
            last_level: -60.0,
        }
    }

    #[inline]
    fn feed(&mut self, x: f32) {
        let mut y = x;
        for f in &mut self.filters {
            y = f.process(y);
        }
        self.power += (y * y) as f64;
        self.count += 1;
    }

    /// Closes the hop: measures this band's level and rise (dB) relative to `ref_db`.
    fn measure(&mut self, ref_db: f32, dt: f32) -> (f32, f32) {
        let p = if self.count > 0 { self.power / self.count as f64 } else { 0.0 };
        self.power = 0.0;
        self.count = 0;
        let level = (10.0 * (p as f32 + 1e-12).log10() - ref_db).max(-60.0);
        let rise = level - self.slow_db;
        self.slow_db += (level - self.slow_db) * (1.0 - (-dt / self.slow_tau).exp());
        self.last_rise = rise;
        self.last_level = level;
        (level, rise)
    }

    /// Decides whether this hop is a hit (`allowed` = not explained by another band).
    fn decide(&mut self, allowed: bool, dt: f32) -> bool {
        let (level, rise) = (self.last_level, self.last_rise);
        let n = self.filled.max(1) as f32;
        let mean = self.history.iter().take(self.filled).sum::<f32>() / n;
        let var = self.history.iter().take(self.filled).map(|v| (v - mean) * (v - mean)).sum::<f32>() / n;
        let threshold = (mean + 2.0 * var.sqrt()).max(self.min_rise_db);

        self.since_hit += dt;
        self.envelope *= 0.5f32.powf(dt / self.decay_half_life);
        let mut hit = false;
        if allowed
            && self.filled >= 12
            && rise > threshold
            && level > self.min_level_db
            && self.since_hit >= self.refractory
        {
            hit = true;
            self.hits = self.hits.wrapping_add(1);
            self.since_hit = 0.0;
            let strength = ((rise - threshold) / 15.0).clamp(0.0, 1.0);
            self.envelope = self.envelope.max(0.55 + 0.45 * strength.sqrt());
        }
        self.history[self.pos] = rise.max(0.0);
        self.pos = (self.pos + 1) % self.history.len();
        self.filled = (self.filled + 1).min(self.history.len());
        hit
    }
}

/// Autocorrelation tempo estimate plus a soft phase-locked beat clock.
struct TempoTracker {
    onset_history: Vec<f32>,
    pos: usize,
    filled: usize,
    since_estimate: f32,
    bpm: f32,
    confidence: f32,
    phase: f64,
    beat_count: u32,
    candidate: f32,
    candidate_votes: u32,
}

impl TempoTracker {
    const HISTORY: usize = (ANALYSIS_RATE as usize) * 6;

    fn new() -> Self {
        Self {
            onset_history: vec![0.0; Self::HISTORY],
            pos: 0,
            filled: 0,
            since_estimate: 0.0,
            bpm: 120.0,
            confidence: 0.0,
            phase: 0.0,
            beat_count: 0,
            candidate: 0.0,
            candidate_votes: 0,
        }
    }

    fn estimate(&mut self) {
        let n = self.filled;
        if n < Self::HISTORY / 2 {
            return;
        }
        // Oldest first.
        let start = if n < Self::HISTORY { 0 } else { self.pos };
        let series: Vec<f32> = (0..n).map(|i| self.onset_history[(start + i) % Self::HISTORY]).collect();
        let mean = series.iter().sum::<f32>() / n as f32;
        let centered: Vec<f32> = series.iter().map(|v| v - mean).collect();
        let energy: f32 = centered.iter().map(|v| v * v).sum::<f32>().max(1e-9);
        let rate = ANALYSIS_RATE as f32;
        let min_lag = (60.0 / 190.0 * rate) as usize;
        let max_lag = (60.0 / 65.0 * rate) as usize;
        let mut acf = vec![0f32; max_lag + 2];
        for (lag, a) in acf.iter_mut().enumerate().take(max_lag + 2).skip(min_lag.saturating_sub(1)) {
            let mut s = 0.0;
            for i in lag..n {
                s += centered[i] * centered[i - lag];
            }
            *a = s / energy;
        }
        let mut best = (0usize, f32::MIN);
        for lag in min_lag..=max_lag {
            let bpm = 60.0 * rate / lag as f32;
            // log-normal prior around 120 BPM
            let octaves = (bpm / 120.0).log2();
            let prior = (-0.5 * (octaves / 0.7) * (octaves / 0.7)).exp();
            // reward lags whose double also correlates (metrical structure)
            let double = if lag * 2 < acf.len() { acf[lag * 2].max(0.0) * 0.5 } else { 0.0 };
            let score = (acf[lag] + double) * (0.6 + 0.4 * prior);
            if score > best.1 {
                best = (lag, score);
            }
        }
        let lag = best.0;
        if lag == 0 {
            return;
        }
        // parabolic refinement
        let (y0, y1, y2) = (acf[lag - 1], acf[lag], acf[lag + 1]);
        let denom = y0 - 2.0 * y1 + y2;
        let offset = if denom.abs() > 1e-9 { 0.5 * (y0 - y2) / denom } else { 0.0 };
        let refined = lag as f32 + offset.clamp(-0.5, 0.5);
        let bpm = 60.0 * rate / refined;
        let confidence = acf[lag].clamp(0.0, 1.0);

        if (bpm - self.candidate).abs() / self.candidate.max(1.0) < 0.04 {
            self.candidate_votes += 1;
        } else {
            self.candidate = bpm;
            self.candidate_votes = 1;
        }
        if self.candidate_votes >= 2 || confidence > self.confidence + 0.2 || self.confidence < 0.05 {
            self.bpm = bpm;
        }
        self.confidence = self.confidence * 0.5 + confidence * 0.5;
    }

    /// Advances the beat clock; returns whether a beat boundary was crossed.
    fn step(&mut self, onset_strength: f32, kick_hit: bool, dt: f32) -> bool {
        self.onset_history[self.pos] = onset_strength;
        self.pos = (self.pos + 1) % Self::HISTORY;
        self.filled = (self.filled + 1).min(Self::HISTORY);
        self.since_estimate += dt;
        if self.since_estimate > 0.5 {
            self.since_estimate = 0.0;
            self.estimate();
        }
        let before = self.phase.floor();
        self.phase += dt as f64 * self.bpm as f64 / 60.0;
        if kick_hit {
            // nudge the phase toward the kick (soft PLL)
            let frac = self.phase - self.phase.floor();
            let err = if frac > 0.5 { frac - 1.0 } else { frac };
            self.phase -= err * 0.25;
        }
        let crossed = self.phase.floor() > before;
        if crossed {
            self.beat_count = self.beat_count.wrapping_add(1);
        }
        crossed
    }
}

/// The analysis pipeline. Feed it interleaved stereo samples; it emits a frame every hop.
pub struct Analyzer {
    rate: u32,
    hop: usize,
    dt: f32,
    cava: CavaBars,
    cava_settings: CavaSettings,
    pending: Vec<f32>,
    pending_start: usize,
    ring: Vec<[f32; 2]>,
    ring_pos: usize,
    samples_done: u64,

    // spectrum FFT (4096 @ 48 kHz)
    fft: Arc<dyn RealToComplex<f32>>,
    fft_n: usize,
    window: Vec<f32>,
    fft_in: Vec<f32>,
    fft_out: Vec<Complex<f32>>,
    scratch: Vec<Complex<f32>>,
    mags: Vec<f32>,
    mag_norm: f32,
    spectrum_map: Vec<(f32, f32)>,
    spectrum_tilt: Vec<f32>,
    chroma_map: Vec<(u8, f32)>,

    // onset FFT (1024 @ 48 kHz)
    offt: Arc<dyn RealToComplex<f32>>,
    offt_n: usize,
    owindow: Vec<f32>,
    offt_in: Vec<f32>,
    offt_out: Vec<Complex<f32>>,
    oscratch: Vec<Complex<f32>>,
    olog: Vec<f32>,
    olog_prev: Vec<f32>,
    onsets: [OnsetDetector; 3],
    /// Slow peak follower of the onset spectrum, so tempo flux is relative to the music's level.
    onset_ref: f32,
    tempo: TempoTracker,

    spectrum_agc: Agc,
    band_agc: [Agc; NUM_BANDS],
    loud_agc: Agc,
    silent_for: f32,
    frame: AudioFrame,
    /// Per-detector energy rise and level (dB) of the last frame (diagnostics).
    pub debug_flux: [f32; 3],
    pub debug_level: [f32; 3],
}

impl Analyzer {
    pub fn new(rate: u32, cava_settings: &CavaSettings) -> Result<Self, CavaInitError> {
        let cava = CavaBars::new(cava_settings, rate)?;
        let hop = ((rate as f64 / ANALYSIS_RATE).round() as usize).max(16);
        let fft_n = (rate as usize / 12).next_power_of_two().clamp(1024, 16384);
        let offt_n = (fft_n / 4).max(256);
        let mut planner = RealFftPlanner::<f32>::new();
        let fft = planner.plan_fft_forward(fft_n);
        let offt = planner.plan_fft_forward(offt_n);
        let hann = |n: usize| -> Vec<f32> {
            (0..n).map(|i| 0.5 - 0.5 * (2.0 * std::f32::consts::PI * i as f32 / n as f32).cos()).collect()
        };
        let window = hann(fft_n);
        let owindow = hann(offt_n);
        let mag_norm = 2.0 / window.iter().sum::<f32>();
        let bin_hz = rate as f32 / fft_n as f32;

        let nyq = rate as f32 / 2.0;
        let high = SPECTRUM_HIGH_HZ.min(nyq * 0.95);
        let spectrum_map: Vec<(f32, f32)> = (0..SPECTRUM_BINS)
            .map(|i| {
                let f0 = SPECTRUM_LOW_HZ * (high / SPECTRUM_LOW_HZ).powf(i as f32 / SPECTRUM_BINS as f32);
                let f1 = SPECTRUM_LOW_HZ * (high / SPECTRUM_LOW_HZ).powf((i + 1) as f32 / SPECTRUM_BINS as f32);
                (f0 / bin_hz, f1 / bin_hz)
            })
            .collect();
        // +3 dB/octave around 1 kHz so treble isn't buried (music falls off ~4.5 dB/oct).
        let spectrum_tilt: Vec<f32> = spectrum_map
            .iter()
            .map(|(a, b)| {
                let f = ((a + b) * 0.5 * bin_hz).max(1.0);
                3.0 * (f / 1000.0).log2()
            })
            .collect();
        let chroma_map: Vec<(u8, f32)> = (0..fft_n / 2 + 1)
            .map(|k| {
                let f = k as f32 * bin_hz;
                if !(55.0..=5000.0).contains(&f) {
                    return (255, 0.0);
                }
                let midi = 69.0 + 12.0 * (f / 440.0).log2();
                let nearest = midi.round();
                let dist = (midi - nearest).abs();
                let pc = (nearest as i32).rem_euclid(12) as u8;
                (pc, (1.0 - 2.0 * dist).max(0.0))
            })
            .collect();
        let obins = offt_n / 2 + 1;
        let fs = rate as f32;
        let onsets = [
            // kick: 30-120 Hz, 24 dB/oct
            OnsetDetector::new(
                vec![Biquad::highpass(fs, 30.0), Biquad::lowpass(fs, 120.0), Biquad::lowpass(fs, 120.0)],
                0.12,
                0.13,
                6.0,
                -12.0,
            ),
            // snare: 180 Hz - 5 kHz
            OnsetDetector::new(
                vec![Biquad::highpass(fs, 180.0), Biquad::highpass(fs, 180.0), Biquad::lowpass(fs, 5000.0)],
                0.11,
                0.10,
                6.0,
                -16.0,
            ),
            // hats: above 7 kHz
            OnsetDetector::new(
                vec![Biquad::highpass(fs, 7000.0), Biquad::highpass(fs, 7000.0)],
                0.06,
                0.06,
                5.0,
                -36.0,
            ),
        ];
        let frame = AudioFrame {
            bars: cava.bars.clone(),
            bars_left: cava.bars_left.clone(),
            bars_right: cava.bars_right.clone(),
            ..Default::default()
        };

        Ok(Self {
            rate,
            hop,
            dt: hop as f32 / rate as f32,
            cava_settings: cava.settings().clone(),
            cava,
            pending: Vec::with_capacity(hop * 8),
            pending_start: 0,
            ring: vec![[0.0, 0.0]; fft_n.max(WAVE_LEN)],
            ring_pos: 0,
            samples_done: 0,
            scratch: fft.make_scratch_vec(),
            fft_in: fft.make_input_vec(),
            fft_out: fft.make_output_vec(),
            fft,
            fft_n,
            window,
            mags: vec![0.0; fft_n / 2 + 1],
            mag_norm,
            spectrum_map,
            spectrum_tilt,
            chroma_map,
            oscratch: offt.make_scratch_vec(),
            offt_in: offt.make_input_vec(),
            offt_out: offt.make_output_vec(),
            offt,
            offt_n,
            owindow,
            olog: vec![0.0; obins],
            olog_prev: vec![0.0; obins],
            onsets,
            onset_ref: 1e-3,
            tempo: TempoTracker::new(),
            spectrum_agc: Agc::new(-60.0, 1.5),
            band_agc: [Agc::new(-66.0, 1.0); NUM_BANDS],
            loud_agc: Agc::new(-50.0, 1.0),
            silent_for: 10.0,
            frame,
            debug_flux: [0.0; 3],
            debug_level: [-60.0; 3],
        })
    }

    pub fn rate(&self) -> u32 {
        self.rate
    }

    pub fn hop(&self) -> usize {
        self.hop
    }

    pub fn frame(&self) -> &AudioFrame {
        &self.frame
    }

    pub fn cava_settings(&self) -> &CavaSettings {
        &self.cava_settings
    }

    /// Rebuilds cava with new settings (keeps every other feature's state).
    pub fn set_cava_settings(&mut self, settings: &CavaSettings) -> Result<(), CavaInitError> {
        self.cava = CavaBars::new(settings, self.rate)?;
        self.cava_settings = self.cava.settings().clone();
        Ok(())
    }

    /// Feeds interleaved stereo samples; calls `on_frame` for every completed analysis frame.
    pub fn push(&mut self, interleaved: &[f32], mut on_frame: impl FnMut(&AudioFrame)) {
        self.pending.extend_from_slice(interleaved);
        let need = self.hop * 2;
        while self.pending.len() - self.pending_start >= need {
            let start = self.pending_start;
            self.pending_start += need;
            let chunk: Vec<f32> = self.pending[start..start + need].to_vec();
            self.process_hop(&chunk);
            on_frame(&self.frame);
        }
        if self.pending_start > 0 && self.pending_start * 2 > self.pending.len() {
            self.pending.drain(..self.pending_start);
            self.pending_start = 0;
        }
    }

    fn process_hop(&mut self, chunk: &[f32]) {
        let dt = self.dt;
        let ring_len = self.ring.len();
        let mut sum_l = 0.0f64;
        let mut sum_r = 0.0f64;
        let mut sum_lr = 0.0f64;
        let mut peak = 0.0f32;
        for &[l, r] in chunk.as_chunks::<2>().0 {
            self.ring[self.ring_pos] = [l, r];
            self.ring_pos = (self.ring_pos + 1) % ring_len;
            sum_l += (l * l) as f64;
            sum_r += (r * r) as f64;
            sum_lr += (l * r) as f64;
            peak = peak.max(l.abs()).max(r.abs());
            let mono = (l + r) * 0.5;
            for det in &mut self.onsets {
                det.feed(mono);
            }
        }
        let frames = (chunk.len() / 2) as f64;
        self.samples_done += frames as u64;
        self.cava.process(chunk);

        let f = &mut self.frame;
        f.time = self.samples_done as f64 / self.rate as f64;
        f.bars.clone_from(&self.cava.bars);
        f.bars_left.clone_from(&self.cava.bars_left);
        f.bars_right.clone_from(&self.cava.bars_right);

        // ---- levels
        let ms = ((sum_l + sum_r) / (2.0 * frames)) as f32;
        let rms_db = 10.0 * ms.max(1e-12).log10();
        f.rms_db = rms_db;
        f.peak = peak.min(1.0);
        let loud_ref = self.loud_agc.update(rms_db, dt);
        let to_level = |db: f32| ((db - (loud_ref - 36.0)) / 36.0).clamp(0.0, 1.0);
        let level_s = Smoother::new(0.01, 0.15);
        f.loudness = level_s.step(f.loudness, to_level(rms_db), dt);
        let l_db = 10.0 * ((sum_l / frames) as f32).max(1e-12).log10();
        let r_db = 10.0 * ((sum_r / frames) as f32).max(1e-12).log10();
        f.level_l = level_s.step(f.level_l, to_level(l_db), dt);
        f.level_r = level_s.step(f.level_r, to_level(r_db), dt);
        let corr = if sum_l > 1e-12 && sum_r > 1e-12 { (sum_lr / (sum_l * sum_r).sqrt()) as f32 } else { 1.0 };
        f.correlation = Smoother::new(0.1, 0.1).step(f.correlation, corr.clamp(-1.0, 1.0), dt);
        if rms_db < -85.0 {
            self.silent_for += dt;
        } else {
            self.silent_for = 0.0;
        }
        f.silent = self.silent_for > 0.3;

        // ---- waveform (oldest first)
        if f.waveform.len() != WAVE_LEN {
            f.waveform = vec![[0.0, 0.0]; WAVE_LEN];
        }
        for i in 0..WAVE_LEN {
            let idx = (self.ring_pos + ring_len - WAVE_LEN + i) % ring_len;
            f.waveform[i] = self.ring[idx];
        }

        // ---- spectrum FFT (mono mix, newest fft_n samples)
        let n = self.fft_n;
        for i in 0..n {
            let s = self.ring[(self.ring_pos + ring_len - n + i) % ring_len];
            self.fft_in[i] = (s[0] + s[1]) * 0.5 * self.window[i];
        }
        let _ = self.fft.process_with_scratch(&mut self.fft_in, &mut self.fft_out, &mut self.scratch);
        for (m, c) in self.mags.iter_mut().zip(self.fft_out.iter()) {
            *m = c.norm() * self.mag_norm;
        }
        let bins = self.mags.len();

        // spectrum bins (dB with tilt, AGC)
        let mut spec_db = [0f32; SPECTRUM_BINS];
        let mut frame_max_db = -200.0f32;
        for (i, &(a, b)) in self.spectrum_map.iter().enumerate() {
            let mag = if b - a < 1.0 {
                // narrower than an FFT bin: interpolate
                let x = ((a + b) * 0.5).clamp(0.0, (bins - 1) as f32);
                let k = x.floor() as usize;
                let t = x - k as f32;
                let k2 = (k + 1).min(bins - 1);
                self.mags[k] * (1.0 - t) + self.mags[k2] * t
            } else {
                let lo = (a.floor() as usize).min(bins - 1);
                let hi = (b.ceil() as usize).min(bins);
                self.mags[lo..hi.max(lo + 1)].iter().cloned().fold(0.0, f32::max)
            };
            let db = 20.0 * mag.max(1e-9).log10() + self.spectrum_tilt[i];
            spec_db[i] = db;
            frame_max_db = frame_max_db.max(db);
        }
        let spec_ref = self.spectrum_agc.update(frame_max_db, dt);
        let spec_s = Smoother::new(0.0, 0.09);
        for (i, v) in f.spectrum.iter_mut().enumerate() {
            let target = ((spec_db[i] - (spec_ref - 54.0)) / 54.0).clamp(0.0, 1.0);
            *v = spec_s.step(*v, target, dt);
        }

        // bands
        let bin_hz = self.rate as f32 / n as f32;
        let global_ref = spec_ref;
        let band_s = Smoother::new(0.012, 0.16);
        for b in 0..NUM_BANDS {
            let lo = ((BAND_EDGES[b] / bin_hz) as usize).clamp(1, bins - 1);
            let hi = ((BAND_EDGES[b + 1] / bin_hz) as usize).clamp(lo + 1, bins);
            let power: f32 = self.mags[lo..hi].iter().map(|m| m * m).sum();
            let db = 10.0 * power.max(1e-14).log10();
            let r = self.band_agc[b].update(db, dt).max(global_ref - 18.0);
            let target = ((db - (r - 30.0)) / 30.0).clamp(0.0, 1.0);
            f.bands[b] = band_s.step(f.bands[b], target * target, dt);
        }
        f.bass = (0.45 * f.bands[0] + 0.55 * f.bands[1]).min(1.0);
        f.mid = (f.bands[2] + f.bands[3] + f.bands[4]) / 3.0;
        f.treble = (f.bands[5] + f.bands[6]) * 0.5;

        // centroid & flatness
        let mut wsum = 0.0f32;
        let mut msum = 0.0f32;
        let mut log_sum = 0.0f32;
        let mut pow_sum = 0.0f32;
        let mut count = 0usize;
        let c_lo = (60.0 / bin_hz) as usize;
        let c_hi = ((12000.0 / bin_hz) as usize).min(bins);
        for k in c_lo.max(1)..c_hi {
            let m = self.mags[k];
            wsum += m * k as f32 * bin_hz;
            msum += m;
            let fk = k as f32 * bin_hz;
            if (200.0..8000.0).contains(&fk) {
                let p = m * m + 1e-14;
                log_sum += p.ln();
                pow_sum += p;
                count += 1;
            }
        }
        let feat_s = Smoother::new(0.05, 0.25);
        if msum > 1e-7 {
            let centroid = wsum / msum;
            let c = ((centroid.max(1.0) / 200.0).log2() / (6000.0f32 / 200.0).log2()).clamp(0.0, 1.0);
            f.centroid = feat_s.step(f.centroid, c, dt);
        }
        if count > 0 && pow_sum > 1e-10 {
            let geo = (log_sum / count as f32).exp();
            let arith = pow_sum / count as f32;
            f.flatness = feat_s.step(f.flatness, (geo / arith).clamp(0.0, 1.0), dt);
        }

        // chroma
        let mut chroma = [0f32; 12];
        for (k, &(pc, w)) in self.chroma_map.iter().enumerate() {
            if pc < 12 {
                chroma[pc as usize] += self.mags[k] * self.mags[k] * w;
            }
        }
        let cmax = chroma.iter().cloned().fold(0.0, f32::max);
        let chroma_s = Smoother::new(0.05, 0.35);
        for (out, &c) in f.chroma.iter_mut().zip(chroma.iter()) {
            let target = if cmax > 1e-12 { (c / cmax).sqrt() } else { 0.0 };
            *out = chroma_s.step(*out, target, dt);
        }
        let (note, &best) = f
            .chroma
            .iter()
            .enumerate()
            .max_by(|a, b| a.1.partial_cmp(b.1).unwrap_or(std::cmp::Ordering::Equal))
            .unwrap();
        let mean = f.chroma.iter().sum::<f32>() / 12.0;
        f.note = note as u8;
        f.note_strength = if best > 1e-6 { ((best - mean) / best).clamp(0.0, 1.0) } else { 0.0 };

        // ---- onsets (short FFT for timing)
        let on = self.offt_n;
        for i in 0..on {
            let s = self.ring[(self.ring_pos + ring_len - on + i) % ring_len];
            self.offt_in[i] = (s[0] + s[1]) * 0.5 * self.owindow[i];
        }
        let _ = self.offt.process_with_scratch(&mut self.offt_in, &mut self.offt_out, &mut self.oscratch);
        std::mem::swap(&mut self.olog, &mut self.olog_prev);
        let onorm = 2.0 / self.owindow.iter().sum::<f32>();
        let frame_peak = self.offt_out.iter().map(|c| c.norm() * onorm).fold(0.0, f32::max);
        // follows peaks instantly, halves over ~2 s, never below a noise floor
        self.onset_ref = (self.onset_ref * 0.5f32.powf(dt / 2.0)).max(frame_peak).max(2e-4);
        let gain = 20.0 / self.onset_ref;
        for (l, c) in self.olog.iter_mut().zip(self.offt_out.iter()) {
            *l = (1.0 + gain * c.norm() * onorm).ln();
        }
        // tempo flux: mean positive log-magnitude change across the spectrum
        let mut total_flux = 0.0;
        for k in 1..self.olog.len() {
            let d = self.olog[k] - self.olog_prev[k];
            if d > 0.0 {
                total_flux += d;
            }
        }
        total_flux /= self.olog.len() as f32;

        // hits: band energy jumps, relative to the overall level
        let ref_db = self.loud_agc.ref_db;
        let m: [(f32, f32); 3] = std::array::from_fn(|i| self.onsets[i].measure(ref_db, dt));
        // A band only counts as hit when the band below doesn't explain it: a kick's knock and
        // click spill into the snare band, a snare's wires into the hat band.
        let allowed =
            [true, m[1].0 > m[0].0 + 1.0 && m[1].0 > m[2].0 - 6.0, m[2].0 > m[1].0 + 2.0 || m[2].1 > m[1].1 + 8.0];
        let mut kick_hit = false;
        for (i, det) in self.onsets.iter_mut().enumerate() {
            let hit = det.decide(allowed[i], dt);
            if hit && i == 0 {
                kick_hit = true;
            }
            f.onset[i] = det.envelope;
            f.hits[i] = det.hits;
            self.debug_flux[i] = det.last_rise;
            self.debug_level[i] = det.last_level;
        }

        // ---- tempo & beat
        let crossed = self.tempo.step(total_flux, kick_hit, dt);
        f.bpm = self.tempo.bpm;
        f.tempo_confidence = self.tempo.confidence;
        f.beat_phase = (self.tempo.phase - self.tempo.phase.floor()) as f32;
        f.beat_count = self.tempo.beat_count;
        let pulse = (-f.beat_phase * 7.0).exp();
        let c = (self.tempo.confidence * 2.5).clamp(0.0, 1.0);
        f.beat = if f.silent { 0.0 } else { c * pulse + (1.0 - c) * f.onset[0] };
        let _ = crossed;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f32::consts::PI;

    fn run(an: &mut Analyzer, seconds: f32, mut gen: impl FnMut(usize) -> [f32; 2]) -> Vec<AudioFrame> {
        let rate = an.rate() as usize;
        let total = (seconds * rate as f32) as usize;
        let mut frames = Vec::new();
        let mut n = 0;
        while n < total {
            let block = 512.min(total - n);
            let mut buf = Vec::with_capacity(block * 2);
            for i in 0..block {
                let s = gen(n + i);
                buf.push(s[0]);
                buf.push(s[1]);
            }
            n += block;
            an.push(&buf, |f| frames.push(f.clone()));
        }
        frames
    }

    #[test]
    fn frames_arrive_at_the_analysis_rate() {
        let mut an = Analyzer::new(48000, &CavaSettings::default()).unwrap();
        let frames = run(&mut an, 2.0, |_| [0.0, 0.0]);
        assert!((frames.len() as f64 - 2.0 * ANALYSIS_RATE).abs() <= 2.0, "{}", frames.len());
        assert!(frames.last().unwrap().silent);
        assert!(frames.last().unwrap().bands.iter().all(|&b| b < 1e-3));
    }

    #[test]
    fn a_440_tone_reads_as_a_and_lights_the_mid_spectrum() {
        let mut an = Analyzer::new(48000, &CavaSettings::default()).unwrap();
        let frames = run(&mut an, 2.0, |n| {
            let s = (2.0 * PI * 440.0 * n as f32 / 48000.0).sin() * 0.4;
            [s, s]
        });
        let f = frames.last().unwrap();
        assert_eq!(f.note_name(), "A");
        assert!(f.note_strength > 0.5);
        // spectrum peak near 440 Hz
        let (peak, _) = f.spectrum.iter().enumerate().max_by(|a, b| a.1.partial_cmp(b.1).unwrap()).unwrap();
        let hz =
            SPECTRUM_LOW_HZ * (SPECTRUM_HIGH_HZ / SPECTRUM_LOW_HZ).powf((peak as f32 + 0.5) / SPECTRUM_BINS as f32);
        assert!((hz / 440.0).log2().abs() < 0.1, "peak at {hz} Hz");
        assert!(f.bands[3] > f.bands[0], "mid band above sub band: {:?}", f.bands);
        assert!(!f.silent);
        assert!((f.correlation - 1.0).abs() < 0.01);
    }

    #[test]
    fn kicks_at_120_bpm_are_detected_and_tempo_locks() {
        let rate = 48000usize;
        let mut an = Analyzer::new(rate as u32, &CavaSettings::default()).unwrap();
        let beat = rate / 2; // 120 BPM
        let frames = run(&mut an, 12.0, |n| {
            let k = n % beat;
            let t = k as f32 / rate as f32;
            let env = (-t * 18.0).exp();
            let freq = 50.0 + 120.0 * (-t * 40.0).exp();
            let s = (2.0 * PI * freq * t).sin() * env * 0.8;
            [s, s]
        });
        let f = frames.last().unwrap();
        // ~24 kicks in 12 s; allow a couple missed at the start
        assert!((20..=25).contains(&f.hits[0]), "kick hits: {}", f.hits[0]);
        assert!(
            (f.bpm - 120.0).abs() < 3.0 || (f.bpm - 240.0).abs() < 6.0 || (f.bpm - 60.0).abs() < 2.0,
            "bpm {}",
            f.bpm
        );
        assert!(f.tempo_confidence > 0.2, "confidence {}", f.tempo_confidence);
        // the hat detector should stay mostly quiet on pure kicks
        assert!(f.hits[2] < 6, "hat hits on kick-only signal: {}", f.hits[2]);
    }

    fn noise(state: &mut u32) -> f32 {
        *state = state.wrapping_mul(1664525).wrapping_add(1013904223);
        (*state >> 8) as f32 / (1u32 << 24) as f32 * 2.0 - 1.0
    }

    /// Simple drum voices with realistic spectra, shared by the onset tests.
    struct Drums {
        st: u32,
        hp1: (f32, f32),
        hp2: (f32, f32),
        hp3: (f32, f32),
    }

    impl Drums {
        fn new() -> Self {
            Self { st: 12345, hp1: (0.0, 0.0), hp2: (0.0, 0.0), hp3: (0.0, 0.0) }
        }
        fn hp(state: &mut (f32, f32), x: f32, cutoff: f32) -> f32 {
            let rc = 1.0 / (2.0 * PI * cutoff);
            let a = rc / (rc + 1.0 / 48000.0);
            let y = a * (state.1 + x - state.0);
            *state = (x, y);
            y
        }
        fn kick(&mut self, t: f32) -> f32 {
            (2.0 * PI * (50.0 + 120.0 * (-t * 40.0).exp()) * t).sin() * (-t * 18.0).exp() * 0.8
        }
        fn snare(&mut self, t: f32) -> f32 {
            let w = noise(&mut self.st);
            let n = Self::hp(&mut self.hp1, w, 1200.0);
            let body = (2.0 * PI * 190.0 * t).sin() * (-t * 30.0).exp();
            (n * 0.7 + body * 0.5) * (-t * 16.0).exp() * 0.6
        }
        fn hat(&mut self, t: f32) -> f32 {
            let w = noise(&mut self.st);
            let a = Self::hp(&mut self.hp2, w, 7000.0);
            let b = Self::hp(&mut self.hp3, a, 7000.0);
            b * (-t * 55.0).exp() * 0.5
        }
    }

    #[test]
    #[ignore]
    fn print_flux_profiles() {
        let rate = 48000usize;
        for which in 0..3 {
            let mut an = Analyzer::new(rate as u32, &CavaSettings::default()).unwrap();
            let mut d = Drums::new();
            let total = rate * 3;
            let mut n = 0;
            let mut peaks = [0f32; 3];
            let mut levels = [0f32; 3];
            while n < total {
                let mut buf = Vec::new();
                for i in 0..512 {
                    let t = ((n + i) % (rate / 2)) as f32 / rate as f32;
                    let s = match which {
                        0 => d.kick(t),
                        1 => d.hat(t),
                        _ => d.snare(t),
                    };
                    buf.push(s);
                    buf.push(s);
                }
                n += 512;
                an.push(&buf, |_| {});
                for j in 0..3 {
                    if an.debug_flux[j] > peaks[j] {
                        peaks[j] = an.debug_flux[j];
                        levels[j] = an.debug_level[j];
                    }
                }
            }
            println!(
                "{:>5}: max rise kick/snare/hat = {:?} dB at levels {:?} dB",
                ["kick", "hat", "snare"][which],
                peaks,
                levels
            );
        }
    }

    #[test]
    fn hats_are_detected_as_hats() {
        let rate = 48000usize;
        let mut an = Analyzer::new(rate as u32, &CavaSettings::default()).unwrap();
        let step = rate / 4;
        let mut d = Drums::new();
        let frames = run(&mut an, 6.0, |n| {
            let s = d.hat((n % step) as f32 / rate as f32);
            [s, s]
        });
        let f = frames.last().unwrap();
        assert!((18..=25).contains(&f.hits[2]), "hat hits {}", f.hits[2]);
        assert!(f.hits[0] < 3, "kick hits on hats: {}", f.hits[0]);
        assert!(f.hits[1] < 3, "snare hits on hats: {}", f.hits[1]);
    }

    #[test]
    fn snares_are_detected_as_snares() {
        let rate = 48000usize;
        let mut an = Analyzer::new(rate as u32, &CavaSettings::default()).unwrap();
        let step = rate;
        let mut d = Drums::new();
        let frames = run(&mut an, 8.0, |n| {
            let s = d.snare((n % step) as f32 / rate as f32);
            [s, s]
        });
        let f = frames.last().unwrap();
        assert!((6..=8).contains(&f.hits[1]), "snare hits {}", f.hits[1]);
        assert!(f.hits[0] < 3, "kick hits on snares: {}", f.hits[0]);
    }

    #[test]
    fn a_drum_groove_separates_kick_snare_and_hats() {
        // 120 BPM: kick on 1 and 3, snare on 2 and 4, hats on every 8th.
        let rate = 48000usize;
        let mut an = Analyzer::new(rate as u32, &CavaSettings::default()).unwrap();
        let mut d = Drums::new();
        let eighth = rate / 4;
        let frames = run(&mut an, 8.0, |n| {
            let step = n / eighth;
            let t = (n % eighth) as f32 / rate as f32;
            let mut s = d.hat(t) * 0.6;
            match step % 8 {
                0 | 4 => s += d.kick(t),
                2 | 6 => s += d.snare(t),
                _ => {}
            }
            [s, s]
        });
        let f = frames.last().unwrap();
        // 8 s at 120 BPM = 16 beats: 8 kicks, 8 snares, 32 hats
        assert!((6..=9).contains(&f.hits[0]), "kicks {}", f.hits[0]);
        assert!((6..=9).contains(&f.hits[1]), "snares {}", f.hits[1]);
        assert!(f.hits[2] >= 14, "hats {}", f.hits[2]);
    }

    #[test]
    fn hard_left_audio_shows_in_levels_and_correlation() {
        let mut an = Analyzer::new(44100, &CavaSettings::default()).unwrap();
        let frames = run(&mut an, 1.5, |n| {
            let s = (2.0 * PI * 300.0 * n as f32 / 44100.0).sin() * 0.5;
            [s, 0.0]
        });
        let f = frames.last().unwrap();
        assert!(f.level_l > 0.5 && f.level_r < 0.05, "{} {}", f.level_l, f.level_r);
        assert!(f.bars_left.iter().cloned().fold(0.0, f32::max) > 0.3);
        assert!(f.bars_right.iter().cloned().fold(0.0, f32::max) < 0.01);
    }

    #[test]
    fn noise_is_flat_and_bright() {
        let mut an = Analyzer::new(48000, &CavaSettings::default()).unwrap();
        let mut state = 1u32;
        let frames = run(&mut an, 1.5, |_| {
            state = state.wrapping_mul(1664525).wrapping_add(1013904223);
            let s = ((state >> 8) as f32 / (1u32 << 24) as f32 - 0.5) * 0.6;
            [s, s]
        });
        let f = frames.last().unwrap();
        assert!(f.flatness > 0.4, "flatness {}", f.flatness);
        assert!(f.centroid > 0.6, "centroid {}", f.centroid);
    }

    #[test]
    fn lerp_blends_continuous_values() {
        let a = AudioFrame { loudness: 0.0, ..Default::default() };
        let b = AudioFrame { loudness: 1.0, hits: [3, 0, 0], ..Default::default() };
        let m = AudioFrame::lerp(&a, &b, 0.25);
        assert!((m.loudness - 0.25).abs() < 1e-6);
        assert_eq!(m.hits[0], 3);
    }
}
