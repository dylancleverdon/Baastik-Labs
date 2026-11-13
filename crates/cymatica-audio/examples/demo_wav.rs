//! Writes a stretch of the built-in demo track as a WAV, e.g. to put under an offline render:
//! `cargo run -p cymatica-audio --example demo_wav -- <start-seconds> <seconds> <out.wav>`
//! (`cymatica render` without `--audio` hears the same track from t = 0).

use anyhow::{Context, Result};
use cymatica_audio::synth::DemoSynth;
use cymatica_audio::wav::{write_wav, StereoAudio};

fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let start: f64 = args.first().context("start seconds")?.parse()?;
    let seconds: f64 = args.get(1).context("length in seconds")?.parse()?;
    let out = args.get(2).context("output path")?;
    let rate = 48000u32;
    let mut synth = DemoSynth::new(rate);
    let skip = (start * rate as f64).round() as usize * 2;
    let keep = (seconds * rate as f64).round() as usize * 2;
    let mut samples = vec![0.0f32; skip + keep];
    synth.render(&mut samples);
    write_wav(std::path::Path::new(out), &StereoAudio { rate, samples: samples.split_off(skip) })?;
    Ok(())
}
