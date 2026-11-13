//! WAV file input/output (for offline renders, tests and the demo track export).

use anyhow::{bail, Context, Result};
use std::path::Path;

/// Interleaved stereo audio in -1..1.
#[derive(Clone, Debug)]
pub struct StereoAudio {
    pub rate: u32,
    pub samples: Vec<f32>,
}

impl StereoAudio {
    pub fn frames(&self) -> usize {
        self.samples.len() / 2
    }

    pub fn seconds(&self) -> f64 {
        self.frames() as f64 / self.rate as f64
    }
}

/// Reads any PCM/float WAV; mono is duplicated, extra channels beyond two are dropped.
pub fn read_wav(path: &Path) -> Result<StereoAudio> {
    let mut reader = hound::WavReader::open(path).with_context(|| format!("opening {}", path.display()))?;
    let spec = reader.spec();
    let channels = spec.channels as usize;
    if channels == 0 {
        bail!("{} has no channels", path.display());
    }
    let raw: Vec<f32> = match spec.sample_format {
        hound::SampleFormat::Float => reader.samples::<f32>().collect::<Result<_, _>>()?,
        hound::SampleFormat::Int => {
            let scale = 1.0 / (1u64 << (spec.bits_per_sample - 1)) as f32;
            reader.samples::<i32>().map(|s| s.map(|v| v as f32 * scale)).collect::<Result<_, _>>()?
        }
    };
    let frames = raw.len() / channels;
    let mut samples = Vec::with_capacity(frames * 2);
    for f in 0..frames {
        let l = raw[f * channels];
        let r = if channels > 1 { raw[f * channels + 1] } else { l };
        samples.push(l);
        samples.push(r);
    }
    Ok(StereoAudio { rate: spec.sample_rate, samples })
}

pub fn write_wav(path: &Path, audio: &StereoAudio) -> Result<()> {
    let spec = hound::WavSpec {
        channels: 2,
        sample_rate: audio.rate,
        bits_per_sample: 16,
        sample_format: hound::SampleFormat::Int,
    };
    let mut w = hound::WavWriter::create(path, spec)?;
    for &s in &audio.samples {
        w.write_sample((s.clamp(-1.0, 1.0) * 32767.0) as i16)?;
    }
    w.finalize()?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trips_through_a_file() {
        let dir = std::env::temp_dir().join(format!("cymatica-wav-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("t.wav");
        let audio = StereoAudio { rate: 44100, samples: vec![0.0, 0.5, -0.5, 0.25] };
        write_wav(&path, &audio).unwrap();
        let back = read_wav(&path).unwrap();
        assert_eq!(back.rate, 44100);
        assert_eq!(back.frames(), 2);
        for (a, b) in audio.samples.iter().zip(back.samples.iter()) {
            assert!((a - b).abs() < 1e-4);
        }
        std::fs::remove_dir_all(dir).ok();
    }
}
