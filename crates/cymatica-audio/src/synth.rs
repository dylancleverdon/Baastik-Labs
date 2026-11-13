//! A small procedural "demo track" (124 BPM, A minor) so every scene can be previewed without
//! music playing, and so tests and demo renders have rich, deterministic input. It is analysed,
//! never played out loud.

use std::f32::consts::{PI, TAU};

const BPM: f32 = 124.0;

/// Song sections in bars: intro, groove, breakdown, drop.
const SECTIONS: [(Section, u32); 4] =
    [(Section::Intro, 8), (Section::Groove, 8), (Section::Breakdown, 4), (Section::Drop, 8)];

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Section {
    Intro,
    Groove,
    Breakdown,
    Drop,
}

/// A minor: Am - F - C - G, as (root midi, chord tones as semitone offsets from root).
const PROGRESSION: [(i32, [i32; 3]); 4] = [(57, [0, 3, 7]), (53, [0, 4, 7]), (48, [0, 4, 7]), (55, [0, 4, 7])];
/// Lead melody per 16th step over two bars (-1 = rest), as scale-relative midi notes.
const MELODY: [i32; 32] = [
    76, -1, -1, 74, 72, -1, 69, -1, 72, -1, 74, -1, 76, -1, -1, -1, //
    79, -1, 77, -1, 76, -1, 74, -1, 72, -1, 74, 72, 69, -1, -1, -1,
];

fn midi_hz(n: f32) -> f32 {
    440.0 * 2f32.powf((n - 69.0) / 12.0)
}

/// Topology-preserving state variable filter (Zavalishin), lowpass + bandpass outputs.
#[derive(Clone, Copy, Default)]
struct Svf {
    ic1: f32,
    ic2: f32,
}

impl Svf {
    fn process(&mut self, x: f32, cutoff: f32, q: f32, fs: f32) -> (f32, f32) {
        let g = (PI * cutoff.clamp(20.0, fs * 0.45) / fs).tan();
        let k = 1.0 / q;
        let a1 = 1.0 / (1.0 + g * (g + k));
        let a2 = g * a1;
        let a3 = g * a2;
        let v3 = x - self.ic2;
        let v1 = a1 * self.ic1 + a2 * v3;
        let v2 = self.ic2 + a2 * self.ic1 + a3 * v3;
        self.ic1 = 2.0 * v1 - self.ic1;
        self.ic2 = 2.0 * v2 - self.ic2;
        (v2, v1)
    }
}

#[derive(Clone, Copy, Default)]
struct OnePoleHp {
    x1: f32,
    y1: f32,
}

impl OnePoleHp {
    fn process(&mut self, x: f32, cutoff: f32, fs: f32) -> f32 {
        let rc = 1.0 / (TAU * cutoff);
        let a = rc / (rc + 1.0 / fs);
        let y = a * (self.y1 + x - self.x1);
        self.x1 = x;
        self.y1 = y;
        y
    }
}

/// Band-limited-ish saw (PolyBLEP).
fn saw(phase: f32, dphase: f32) -> f32 {
    let mut v = 2.0 * phase - 1.0;
    if phase < dphase {
        let t = phase / dphase;
        v -= t + t - t * t - 1.0;
    } else if phase > 1.0 - dphase {
        let t = (phase - 1.0) / dphase;
        v -= t * t + t + t + 1.0;
    }
    v
}

#[derive(Clone, Copy, Default)]
struct Voice {
    age: f32,
    active: bool,
    note: f32,
    phase: [f32; 3],
    vel: f32,
}

/// The demo track generator. Produces interleaved stereo at `rate`.
pub struct DemoSynth {
    fs: f32,
    sample: u64,
    rng: u32,
    step: i64,
    kick: Voice,
    snare: Voice,
    hat: Voice,
    hat_open: bool,
    bass: Voice,
    bass_filter: Svf,
    pad: [Voice; 3],
    pad_filter: [Svf; 2],
    arp: Voice,
    lead: Voice,
    lead_filter: Svf,
    riser_filter: Svf,
    hp: [OnePoleHp; 4],
    kick_phase: f32,
}

impl DemoSynth {
    pub fn new(rate: u32) -> Self {
        Self {
            fs: rate as f32,
            sample: 0,
            rng: 0x1234_5678,
            step: -1,
            kick: Voice::default(),
            snare: Voice::default(),
            hat: Voice::default(),
            hat_open: false,
            bass: Voice::default(),
            bass_filter: Svf::default(),
            pad: [Voice::default(); 3],
            pad_filter: [Svf::default(); 2],
            arp: Voice::default(),
            lead: Voice::default(),
            lead_filter: Svf::default(),
            riser_filter: Svf::default(),
            hp: [OnePoleHp::default(); 4],
            kick_phase: 0.0,
        }
    }

    /// Length of one loop of the song in seconds.
    pub fn loop_seconds() -> f32 {
        let bars: u32 = SECTIONS.iter().map(|s| s.1).sum();
        bars as f32 * 4.0 * 60.0 / BPM
    }

    pub fn bpm() -> f32 {
        BPM
    }

    fn noise(&mut self) -> f32 {
        self.rng = self.rng.wrapping_mul(1664525).wrapping_add(1013904223);
        (self.rng >> 8) as f32 / (1u32 << 23) as f32 - 1.0
    }

    /// Section and bar within the loop for a 16th-note step.
    fn section_at(step: i64) -> (Section, u32, u32) {
        let total_bars: u32 = SECTIONS.iter().map(|s| s.1).sum();
        let bar = ((step / 16) as u32) % total_bars;
        let mut start = 0;
        for &(section, len) in &SECTIONS {
            if bar < start + len {
                return (section, bar - start, len);
            }
            start += len;
        }
        (Section::Intro, 0, 8)
    }

    /// The section playing at `seconds` into the song.
    pub fn section_at_time(seconds: f32) -> Section {
        let step = (seconds * BPM / 60.0 * 4.0) as i64;
        Self::section_at(step).0
    }

    fn trigger(&mut self, step: i64) {
        let (section, bar_in_section, section_len) = Self::section_at(step);
        let s16 = (step % 16) as usize;
        let bar = (step / 16) as usize;
        let (root, chord) = PROGRESSION[bar % 4];
        let drums = section != Section::Breakdown;
        let full = matches!(section, Section::Groove | Section::Drop);

        if drums && s16.is_multiple_of(4) {
            self.kick = Voice { active: true, vel: 1.0, ..Default::default() };
            self.kick_phase = 0.0;
        }
        if full && (s16 == 4 || s16 == 12) {
            self.snare = Voice { active: true, vel: 0.9, ..Default::default() };
        }
        if drums && s16.is_multiple_of(2) {
            self.hat = Voice { active: true, vel: if s16 % 4 == 2 { 0.9 } else { 0.45 }, ..Default::default() };
            self.hat_open = s16 % 4 == 2 && section == Section::Drop;
        }
        // bass: 8ths with octave jumps
        if section != Section::Breakdown && s16.is_multiple_of(2) {
            let oct = if s16 % 4 == 2 { 12 } else { 0 };
            self.bass = Voice {
                active: true,
                note: (root - 24 + oct) as f32,
                vel: 1.0,
                phase: self.bass.phase,
                ..Default::default()
            };
        }
        // pad on each bar
        if s16 == 0 {
            for (i, v) in self.pad.iter_mut().enumerate() {
                *v = Voice {
                    active: true,
                    note: (root + chord[i]) as f32,
                    vel: 1.0,
                    phase: v.phase,
                    ..Default::default()
                };
            }
        }
        // arp 16ths in groove/drop
        if full {
            let idx = [0, 1, 2, 1][s16 % 4];
            let up = if s16 >= 8 { 12 } else { 0 };
            self.arp = Voice {
                active: true,
                note: (root + 12 + chord[idx] + up) as f32,
                vel: 0.7,
                phase: self.arp.phase,
                ..Default::default()
            };
        }
        // lead in the drop
        if section == Section::Drop {
            let m = MELODY[(step % 32) as usize];
            if m >= 0 {
                self.lead =
                    Voice { active: true, note: m as f32, vel: 0.8, phase: self.lead.phase, ..Default::default() };
            }
        }
        let _ = (bar_in_section, section_len);
    }

    /// Fills `out` with interleaved stereo samples.
    pub fn render(&mut self, out: &mut [f32]) {
        let fs = self.fs;
        let dt = 1.0 / fs;
        let step_len = 60.0 / BPM / 4.0;
        for frame in out.as_chunks_mut::<2>().0 {
            let t = self.sample as f32 / fs;
            let step = (t / step_len) as i64;
            if step != self.step {
                self.step = step;
                self.trigger(step);
            }
            let (section, bar_in_section, section_len) = Self::section_at(step);
            let section_pos = (bar_in_section as f32 * 16.0 + (step % 16) as f32 + (t / step_len).fract())
                / (section_len as f32 * 16.0);

            let mut l = 0.0f32;
            let mut r = 0.0f32;

            // kick
            if self.kick.active {
                let a = self.kick.age;
                let f = 48.0 + 115.0 * (-a * 32.0).exp();
                self.kick_phase += TAU * f * dt;
                let click = if a < 0.002 { self.noise() * (1.0 - a / 0.002) * 0.3 } else { 0.0 };
                let v = (self.kick_phase.sin() * (-a * 7.5).exp() + click) * 0.9 * self.kick.vel;
                l += v;
                r += v;
                self.kick.age += dt;
                if a > 0.6 {
                    self.kick.active = false;
                }
            }
            // snare / clap
            if self.snare.active {
                let a = self.snare.age;
                let n = self.noise();
                let wires = self.hp[0].process(n, 1500.0, fs);
                let body = (TAU * 185.0 * a).sin() * (-a * 28.0).exp();
                let v = (wires * 0.75 + body * 0.5) * (-a * 13.0).exp() * 0.55 * self.snare.vel;
                l += v * 0.95;
                r += v;
                self.snare.age += dt;
                if a > 0.5 {
                    self.snare.active = false;
                }
            }
            // hats
            if self.hat.active {
                let a = self.hat.age;
                let n = self.noise();
                let h1 = self.hp[1].process(n, 7000.0, fs);
                let h2 = self.hp[2].process(h1, 7000.0, fs);
                let decay = if self.hat_open { 9.0 } else { 45.0 };
                let v = h2 * (-a * decay).exp() * 0.32 * self.hat.vel;
                l += v * 0.8;
                r += v * 1.1;
                self.hat.age += dt;
                if a > 0.4 {
                    self.hat.active = false;
                }
            }
            // bass
            if self.bass.active {
                let a = self.bass.age;
                let f = midi_hz(self.bass.note);
                let dp = f * dt;
                self.bass.phase[0] = (self.bass.phase[0] + dp).fract();
                let osc = saw(self.bass.phase[0], dp) * 0.6 + (self.bass.phase[0] * TAU).sin() * 0.5;
                let env = (-a * 6.0).exp();
                let cutoff = 180.0 + 1400.0 * (-a * 14.0).exp() + if section == Section::Drop { 500.0 } else { 0.0 };
                let (lp, _) = self.bass_filter.process(osc, cutoff, 1.4, fs);
                let v = lp * env * 0.55;
                l += v;
                r += v;
                self.bass.age += dt;
            }
            // pad: detuned saws, wide
            let pad_level = match section {
                Section::Intro => 0.12 + 0.1 * section_pos,
                Section::Breakdown => 0.3,
                _ => 0.16,
            };
            let mut pl = 0.0;
            let mut pr = 0.0;
            for v in self.pad.iter_mut() {
                if !v.active {
                    continue;
                }
                let f = midi_hz(v.note);
                let det = [0.997f32, 1.0, 1.004];
                for (k, ph) in v.phase.iter_mut().enumerate() {
                    let dp = f * det[k] * dt;
                    *ph = (*ph + dp).fract();
                    let s = saw(*ph, dp);
                    if k == 0 {
                        pl += s;
                    } else if k == 2 {
                        pr += s;
                    } else {
                        pl += s * 0.5;
                        pr += s * 0.5;
                    }
                }
                v.age += dt;
            }
            let attack = (self.pad[0].age / 0.25).min(1.0);
            let pad_cut = match section {
                Section::Breakdown => 600.0 + 3500.0 * section_pos,
                _ => 1400.0,
            };
            let (pll, _) = self.pad_filter[0].process(pl, pad_cut, 0.8, fs);
            let (prl, _) = self.pad_filter[1].process(pr, pad_cut, 0.8, fs);
            l += pll * pad_level * attack;
            r += prl * pad_level * attack;
            // arp
            if self.arp.active {
                let a = self.arp.age;
                let f = midi_hz(self.arp.note);
                self.arp.phase[0] = (self.arp.phase[0] + f * dt).fract();
                let sq = if self.arp.phase[0] < 0.5 { 1.0 } else { -1.0 };
                let v = sq * (-a * 18.0).exp() * 0.09 * self.arp.vel;
                l += v * 1.2;
                r += v * 0.7;
                self.arp.age += dt;
            }
            // lead
            if self.lead.active {
                let a = self.lead.age;
                let f = midi_hz(self.lead.note) * (1.0 + 0.004 * (TAU * 5.5 * a).sin());
                let dp = f * dt;
                self.lead.phase[0] = (self.lead.phase[0] + dp).fract();
                let (lp, _) = self.lead_filter.process(saw(self.lead.phase[0], dp), 2500.0, 1.0, fs);
                let v = lp * (-a * 2.5).exp() * 0.2 * self.lead.vel;
                l += v;
                r += v;
                self.lead.age += dt;
            }
            // riser in the breakdown
            if section == Section::Breakdown {
                let n = self.noise();
                let cut = 400.0 + 9000.0 * section_pos * section_pos;
                let (_, bp) = self.riser_filter.process(n, cut, 3.0, fs);
                let v = bp * 0.25 * section_pos;
                l += v;
                r += v;
            }

            frame[0] = (l * 0.8).tanh();
            frame[1] = (r * 0.8).tanh();
            self.sample += 1;
        }
    }
}

/// Simple test signals.
pub mod signals {
    use std::f32::consts::TAU;

    pub fn sine(rate: u32, freq: f32, seconds: f32, amp: f32) -> Vec<f32> {
        let n = (rate as f32 * seconds) as usize;
        let mut out = Vec::with_capacity(n * 2);
        for i in 0..n {
            let s = (TAU * freq * i as f32 / rate as f32).sin() * amp;
            out.push(s);
            out.push(s);
        }
        out
    }

    /// Exponential sweep from `f0` to `f1`.
    pub fn sweep(rate: u32, f0: f32, f1: f32, seconds: f32, amp: f32) -> Vec<f32> {
        let n = (rate as f32 * seconds) as usize;
        let k = (f1 / f0).ln() / seconds;
        let mut out = Vec::with_capacity(n * 2);
        for i in 0..n {
            let t = i as f32 / rate as f32;
            let phase = TAU * f0 * ((k * t).exp() - 1.0) / k;
            let s = phase.sin() * amp;
            out.push(s);
            out.push(s);
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::analysis::Analyzer;
    use crate::cava::CavaSettings;

    #[test]
    fn demo_track_is_deterministic_and_bounded() {
        let mut a = DemoSynth::new(48000);
        let mut b = DemoSynth::new(48000);
        let mut x = vec![0.0; 9600];
        let mut y = vec![0.0; 9600];
        for _ in 0..50 {
            a.render(&mut x);
            b.render(&mut y);
            assert_eq!(x, y);
            assert!(x.iter().all(|s| s.is_finite() && s.abs() <= 1.0));
        }
    }

    #[test]
    fn analysis_of_the_demo_finds_its_tempo_and_drums() {
        let rate = 48000;
        let mut synth = DemoSynth::new(rate);
        let mut an = Analyzer::new(rate, &CavaSettings::default()).unwrap();
        let mut buf = vec![0.0; 1024];
        let mut last = None;
        // 14 s: intro + start of the groove
        for _ in 0..(14 * rate as usize / 512) {
            synth.render(&mut buf);
            an.push(&buf, |f| last = Some(f.clone()));
        }
        let f = last.unwrap();
        assert!(!f.silent);
        let bpm = f.bpm;
        assert!((bpm - 124.0).abs() < 4.0 || (bpm - 62.0).abs() < 2.0 || (bpm - 248.0).abs() < 8.0, "bpm {bpm}");
        // 4-on-the-floor at 124 BPM for ~14 s: ~28 kicks
        assert!(f.hits[0] >= 20, "kicks {}", f.hits[0]);
        assert!(f.hits[2] >= 20, "hats {}", f.hits[2]);
        // the track is in A minor: the dominant note should be in the chord tones
        assert!([9u8, 0, 4, 5, 7, 2].contains(&f.note), "note {}", f.note_name());
    }

    #[test]
    fn sections_cycle() {
        assert_eq!(DemoSynth::section_at_time(1.0), Section::Intro);
        let bar = 4.0 * 60.0 / BPM;
        assert_eq!(DemoSynth::section_at_time(bar * 9.0), Section::Groove);
        assert_eq!(DemoSynth::section_at_time(bar * 17.0), Section::Breakdown);
        assert_eq!(DemoSynth::section_at_time(bar * 21.0), Section::Drop);
        assert_eq!(DemoSynth::section_at_time(DemoSynth::loop_seconds() + 1.0), Section::Intro);
    }
}
