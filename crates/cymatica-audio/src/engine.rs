//! The audio engine: a background thread that reads the chosen input (BlackHole 2ch by default),
//! runs the analyzer and keeps a short, time-stamped history of frames for the renderer.

use crate::analysis::{AudioFrame, Analyzer};
use crate::capture::{self, LiveInput};
use crate::cava::CavaSettings;
use crate::synth::DemoSynth;
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use std::collections::VecDeque;
use std::sync::mpsc;
use std::sync::Arc;
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

/// Where the audio comes from.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize, Default)]
pub enum InputChoice {
    /// BlackHole 2ch (or any BlackHole), like cava's `source = "BlackHole 2ch"`. Falls back to the
    /// demo track while no BlackHole device exists.
    #[default]
    Auto,
    /// A specific input device by name.
    Device(String),
    /// The built-in demo track.
    Demo,
    /// No input (visuals settle).
    Off,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum SourceKind {
    Live,
    Demo,
    Off,
}

#[derive(Clone, Debug)]
pub struct AudioStatus {
    pub kind: SourceKind,
    /// What's playing into the visuals ("BlackHole 2ch", "Demo track", ...).
    pub label: String,
    pub sample_rate: u32,
    pub error: Option<String>,
    /// Seconds without signal on a live input.
    pub silent_seconds: f32,
    /// Whether any BlackHole device is installed.
    pub blackhole_found: bool,
    pub inputs: Vec<String>,
}

impl Default for AudioStatus {
    fn default() -> Self {
        Self {
            kind: SourceKind::Off,
            label: "Starting...".into(),
            sample_rate: 48000,
            error: None,
            silent_seconds: 0.0,
            blackhole_found: false,
            inputs: Vec::new(),
        }
    }
}

enum Command {
    SetInput(InputChoice),
    SetCava(CavaSettings),
    Quit,
}

const HISTORY_SECONDS: f64 = 2.0;

struct Shared {
    frames: Mutex<VecDeque<(Instant, AudioFrame)>>,
    status: Mutex<AudioStatus>,
}

pub struct AudioEngine {
    shared: Arc<Shared>,
    tx: mpsc::Sender<Command>,
    thread: Option<JoinHandle<()>>,
}

enum Source {
    Live(LiveInput),
    Generated { synth: Option<DemoSynth>, start: Instant, produced: u64, rate: u32 },
}

impl AudioEngine {
    pub fn start(input: InputChoice, cava: CavaSettings) -> Self {
        let shared = Arc::new(Shared {
            frames: Mutex::new(VecDeque::new()),
            status: Mutex::new(AudioStatus::default()),
        });
        let (tx, rx) = mpsc::channel();
        let s2 = shared.clone();
        let thread = std::thread::Builder::new()
            .name("cymatica-audio".into())
            .spawn(move || run(s2, rx, input, cava))
            .expect("spawn audio thread");
        Self { shared, tx, thread: Some(thread) }
    }

    pub fn set_input(&self, input: InputChoice) {
        let _ = self.tx.send(Command::SetInput(input));
    }

    pub fn set_cava(&self, settings: CavaSettings) {
        let _ = self.tx.send(Command::SetCava(settings));
    }

    pub fn status(&self) -> AudioStatus {
        self.shared.status.lock().clone()
    }

    /// The frame that was current `delay` ago (blended between neighbours).
    pub fn frame_at(&self, delay: Duration) -> AudioFrame {
        let target = Instant::now().checked_sub(delay).unwrap_or_else(Instant::now);
        let frames = self.shared.frames.lock();
        frame_at(&frames, target)
    }
}

fn frame_at(frames: &VecDeque<(Instant, AudioFrame)>, target: Instant) -> AudioFrame {
    match frames.len() {
        0 => AudioFrame::default(),
        1 => frames[0].1.clone(),
        n => {
            if target >= frames[n - 1].0 {
                return frames[n - 1].1.clone();
            }
            if target <= frames[0].0 {
                return frames[0].1.clone();
            }
            // binary search for the first frame after target
            let idx = frames.partition_point(|(t, _)| *t <= target);
            let (ta, a) = &frames[idx - 1];
            let (tb, b) = &frames[idx];
            let span = tb.duration_since(*ta).as_secs_f32().max(1e-6);
            let t = target.duration_since(*ta).as_secs_f32() / span;
            AudioFrame::lerp(a, b, t)
        }
    }
}

impl Drop for AudioEngine {
    fn drop(&mut self) {
        let _ = self.tx.send(Command::Quit);
        if let Some(t) = self.thread.take() {
            let _ = t.join();
        }
    }
}

fn open_source(choice: &InputChoice, status: &mut AudioStatus) -> Source {
    status.inputs = capture::list_inputs();
    status.blackhole_found = status.inputs.iter().any(|n| n.to_lowercase().contains("blackhole"));
    status.error = None;
    let wanted = match choice {
        InputChoice::Auto => capture::choose_device(&status.inputs, None),
        InputChoice::Device(name) => capture::choose_device(&status.inputs, Some(name)),
        InputChoice::Demo => None,
        InputChoice::Off => None,
    };
    if let Some(name) = wanted {
        match LiveInput::open(&name) {
            Ok(live) => {
                status.kind = SourceKind::Live;
                status.label = live.device_name.clone();
                status.sample_rate = live.rate;
                return Source::Live(live);
            }
            Err(e) => status.error = Some(format!("{e:#}")),
        }
    } else if let InputChoice::Device(name) = choice {
        status.error = Some(format!("\"{name}\" is not connected"));
    }
    let rate = 48000;
    status.sample_rate = rate;
    if *choice == InputChoice::Off {
        status.kind = SourceKind::Off;
        status.label = "No input".into();
        Source::Generated { synth: None, start: Instant::now(), produced: 0, rate }
    } else {
        status.kind = SourceKind::Demo;
        status.label = "Demo track".into();
        Source::Generated { synth: Some(DemoSynth::new(rate)), start: Instant::now(), produced: 0, rate }
    }
}

fn run(shared: Arc<Shared>, rx: mpsc::Receiver<Command>, mut choice: InputChoice, mut cava: CavaSettings) {
    let mut status = AudioStatus::default();
    let mut source = open_source(&choice, &mut status);
    let mut analyzer = Analyzer::new(status.sample_rate, &cava)
        .or_else(|_| Analyzer::new(status.sample_rate, &CavaSettings::default()))
        .expect("analyzer");
    *shared.status.lock() = status.clone();
    let mut last_scan = Instant::now();
    let mut buf: Vec<f32> = Vec::with_capacity(16384);
    let mut silent_since: Option<Instant> = None;

    loop {
        let mut reopen = false;
        while let Ok(cmd) = rx.try_recv() {
            match cmd {
                Command::Quit => return,
                Command::SetInput(c) => {
                    choice = c;
                    reopen = true;
                }
                Command::SetCava(c) => {
                    cava = c;
                    if let Err(e) = analyzer.set_cava_settings(&cava) {
                        status.error = Some(e.to_string());
                    }
                }
            }
        }

        // Re-scan devices every few seconds: reconnect a lost input, or switch to BlackHole as
        // soon as it appears.
        if last_scan.elapsed() > Duration::from_secs(3) {
            last_scan = Instant::now();
            let failed = matches!(&source, Source::Live(l) if l.has_failed());
            let names = capture::list_inputs();
            let want = match &choice {
                InputChoice::Auto => capture::choose_device(&names, None),
                InputChoice::Device(n) => capture::choose_device(&names, Some(n)),
                _ => None,
            };
            let current = match &source {
                Source::Live(l) => Some(l.device_name.clone()),
                _ => None,
            };
            if failed || (want.is_some() && want != current) {
                reopen = true;
            }
            status.inputs = names;
            status.blackhole_found = status.inputs.iter().any(|n| n.to_lowercase().contains("blackhole"));
        }

        if reopen {
            drop(source);
            source = open_source(&choice, &mut status);
            if status.sample_rate != analyzer.rate() {
                if let Ok(a) = Analyzer::new(status.sample_rate, &cava) {
                    analyzer = a;
                }
            }
            silent_since = None;
        }

        // Pull samples.
        buf.clear();
        let now = Instant::now();
        let rate = analyzer.rate();
        match &mut source {
            Source::Live(live) => {
                let n = live.consumer.slots();
                if n >= 2 {
                    if let Ok(chunk) = live.consumer.read_chunk(n - n % 2) {
                        let (a, b) = chunk.as_slices();
                        buf.extend_from_slice(a);
                        buf.extend_from_slice(b);
                        chunk.commit_all();
                    }
                }
                if let Some(e) = live.error.lock().clone() {
                    status.error = Some(e);
                }
            }
            Source::Generated { synth, start, produced, rate: r } => {
                let due = (start.elapsed().as_secs_f64() * *r as f64) as u64;
                let frames = due.saturating_sub(*produced).min(*r as u64 / 4) as usize;
                if frames > 0 {
                    buf.resize(frames * 2, 0.0);
                    if let Some(s) = synth {
                        s.render(&mut buf);
                    }
                    *produced += frames as u64;
                }
            }
        }

        if !buf.is_empty() {
            // Spread frame timestamps over the batch: the batch's last sample is "now".
            let batch_frames = buf.len() / 2;
            let end_time = analyzer.frame().time + batch_frames as f64 / rate as f64;
            let mut new_frames = Vec::new();
            analyzer.push(&buf, |f| new_frames.push(f.clone()));
            let mut frames = shared.frames.lock();
            for f in new_frames {
                let behind = Duration::from_secs_f64((end_time - f.time).max(0.0));
                let stamp = now.checked_sub(behind).unwrap_or(now);
                frames.push_back((stamp, f));
            }
            while frames.len() > (HISTORY_SECONDS * crate::analysis::ANALYSIS_RATE) as usize {
                frames.pop_front();
            }
            drop(frames);

            let silent = analyzer.frame().silent;
            if silent && status.kind == SourceKind::Live {
                let since = *silent_since.get_or_insert(now);
                status.silent_seconds = since.elapsed().as_secs_f32();
            } else {
                silent_since = None;
                status.silent_seconds = 0.0;
            }
        }
        *shared.status.lock() = status.clone();
        std::thread::sleep(Duration::from_millis(3));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn demo_input_produces_live_frames() {
        let engine = AudioEngine::start(InputChoice::Demo, CavaSettings::default());
        std::thread::sleep(Duration::from_millis(1500));
        let status = engine.status();
        assert_eq!(status.kind, SourceKind::Demo);
        let f = engine.frame_at(Duration::ZERO);
        assert!(f.time > 0.5, "time {}", f.time);
        assert!(!f.silent);
        let earlier = engine.frame_at(Duration::from_millis(500));
        assert!(earlier.time < f.time);
    }

    #[test]
    fn interpolates_between_frames() {
        let t0 = Instant::now();
        let mut q = VecDeque::new();
        let a = AudioFrame { loudness: 0.0, time: 0.0, ..Default::default() };
        let b = AudioFrame { loudness: 1.0, time: 0.01, ..Default::default() };
        q.push_back((t0, a));
        q.push_back((t0 + Duration::from_millis(10), b));
        let m = frame_at(&q, t0 + Duration::from_millis(5));
        assert!((m.loudness - 0.5).abs() < 0.05);
        assert_eq!(frame_at(&q, t0 + Duration::from_secs(1)).loudness, 1.0);
    }
}
