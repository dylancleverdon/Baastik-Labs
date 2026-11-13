//! Live input via cpal (CoreAudio on macOS). Opens BlackHole 2ch by default, exactly the device
//! cava reads with `source = "BlackHole 2ch"`.

use anyhow::{anyhow, Result};
use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

/// The device cava's macOS guide sets up.
pub const PREFERRED_DEVICE: &str = "BlackHole 2ch";

/// Names of every input device (as macOS shows them in Audio MIDI Setup).
pub fn list_inputs() -> Vec<String> {
    let host = cpal::default_host();
    match host.input_devices() {
        Ok(devs) => devs.map(|d| d.to_string()).collect(),
        Err(_) => Vec::new(),
    }
}

/// Picks the device to open for `wanted` (None = automatic: BlackHole 2ch, then any BlackHole).
pub fn choose_device(names: &[String], wanted: Option<&str>) -> Option<String> {
    if let Some(w) = wanted {
        if let Some(n) = names.iter().find(|n| n.as_str() == w) {
            return Some(n.clone());
        }
        let wl = w.to_lowercase();
        if let Some(n) = names.iter().find(|n| n.to_lowercase().contains(&wl)) {
            return Some(n.clone());
        }
        return None;
    }
    if let Some(n) = names.iter().find(|n| n.as_str() == PREFERRED_DEVICE) {
        return Some(n.clone());
    }
    names.iter().find(|n| n.to_lowercase().contains("blackhole")).cloned()
}

/// A running capture stream. Samples arrive as interleaved stereo f32 in the ring buffer.
pub struct LiveInput {
    pub device_name: String,
    pub rate: u32,
    pub channels: u16,
    pub consumer: rtrb::Consumer<f32>,
    pub failed: Arc<AtomicBool>,
    pub error: Arc<parking_lot::Mutex<Option<String>>>,
    _stream: cpal::Stream,
}

fn push_frames<T: Copy>(data: &[T], channels: usize, to_f32: impl Fn(T) -> f32, producer: &mut rtrb::Producer<f32>) {
    if channels == 0 {
        return;
    }
    let frames = data.len() / channels;
    let writable = producer.slots() / 2;
    let n = frames.min(writable);
    if n == 0 {
        return;
    }
    if let Ok(mut chunk) = producer.write_chunk(n * 2) {
        let (a, b) = chunk.as_mut_slices();
        let mut idx = 0;
        for f in 0..n {
            let l = to_f32(data[f * channels]);
            let r = if channels > 1 { to_f32(data[f * channels + 1]) } else { l };
            for v in [l, r] {
                if idx < a.len() {
                    a[idx] = v;
                } else {
                    b[idx - a.len()] = v;
                }
                idx += 1;
            }
        }
        chunk.commit_all();
    }
}

impl LiveInput {
    pub fn open(name: &str) -> Result<Self> {
        let host = cpal::default_host();
        let device = host
            .input_devices()?
            .find(|d| d.to_string() == name)
            .ok_or_else(|| anyhow!("input device \"{name}\" not found"))?;
        let supported = device.default_input_config()?;
        let format = supported.sample_format();
        let mut config = supported.config();
        let channels = config.channels;
        let rate = config.sample_rate;
        config.buffer_size = cpal::BufferSize::Default;

        let (mut producer, consumer) = rtrb::RingBuffer::<f32>::new((rate as usize * 2).max(8192) * 2);
        let failed = Arc::new(AtomicBool::new(false));
        let error = Arc::new(parking_lot::Mutex::new(None));
        let (f2, e2) = (failed.clone(), error.clone());
        let on_error = move |err: cpal::Error| {
            *e2.lock() = Some(err.to_string());
            f2.store(true, Ordering::SeqCst);
        };
        let ch = channels as usize;
        use cpal::SampleFormat as F;
        let stream = match format {
            F::F32 => device.build_input_stream(
                config,
                move |d: &[f32], _: &_| push_frames(d, ch, |s| s, &mut producer),
                on_error,
                None,
            )?,
            F::F64 => device.build_input_stream(
                config,
                move |d: &[f64], _: &_| push_frames(d, ch, |s| s as f32, &mut producer),
                on_error,
                None,
            )?,
            F::I16 => device.build_input_stream(
                config,
                move |d: &[i16], _: &_| push_frames(d, ch, |s| s as f32 / 32768.0, &mut producer),
                on_error,
                None,
            )?,
            F::I32 => device.build_input_stream(
                config,
                move |d: &[i32], _: &_| push_frames(d, ch, |s| s as f32 / 2147483648.0, &mut producer),
                on_error,
                None,
            )?,
            F::U16 => device.build_input_stream(
                config,
                move |d: &[u16], _: &_| push_frames(d, ch, |s| (s as f32 - 32768.0) / 32768.0, &mut producer),
                on_error,
                None,
            )?,
            other => return Err(anyhow!("unsupported sample format {other:?} on \"{name}\"")),
        };
        stream.play()?;
        Ok(Self { device_name: name.to_string(), rate, channels, consumer, failed, error, _stream: stream })
    }

    pub fn has_failed(&self) -> bool {
        self.failed.load(Ordering::SeqCst)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn names(v: &[&str]) -> Vec<String> {
        v.iter().map(|s| s.to_string()).collect()
    }

    #[test]
    fn prefers_blackhole_2ch_like_cava() {
        let n = names(&["MacBook Pro Microphone", "BlackHole 16ch", "BlackHole 2ch", "ZoomAudioDevice"]);
        assert_eq!(choose_device(&n, None).as_deref(), Some("BlackHole 2ch"));
        let n = names(&["MacBook Pro Microphone", "BlackHole 16ch"]);
        assert_eq!(choose_device(&n, None).as_deref(), Some("BlackHole 16ch"));
        let n = names(&["MacBook Pro Microphone"]);
        assert_eq!(choose_device(&n, None), None);
    }

    #[test]
    fn explicit_choice_matches_exactly_then_by_substring() {
        let n = names(&["MacBook Pro Microphone", "BlackHole 2ch", "Background Music"]);
        assert_eq!(choose_device(&n, Some("Background Music")).as_deref(), Some("Background Music"));
        assert_eq!(choose_device(&n, Some("macbook")).as_deref(), Some("MacBook Pro Microphone"));
        assert_eq!(choose_device(&n, Some("Scarlett")), None);
    }

    #[test]
    fn frames_are_converted_to_stereo() {
        let (mut p, mut c) = rtrb::RingBuffer::<f32>::new(64);
        push_frames(&[0.5f32, -0.5, 0.25], 1, |s| s, &mut p);
        let got: Vec<f32> = (0..6).map(|_| c.pop().unwrap()).collect();
        assert_eq!(got, vec![0.5, 0.5, -0.5, -0.5, 0.25, 0.25]);
        // 4-channel device: only the first two channels are used
        push_frames(&[1i16, 2, 3, 4], 4, |s| s as f32, &mut p);
        assert_eq!((c.pop().unwrap(), c.pop().unwrap()), (1.0, 2.0));
    }
}
