//! Compares the Rust port of cavacore against output recorded from cava's real C code
//! (see tools/cava-golden). Both sides generate the same integer-only signal.

use cymatica_audio::cava::{CavaPlan, Scaling};

struct Reader<'a> {
    data: &'a [u8],
    pos: usize,
}

impl Reader<'_> {
    fn take<const N: usize>(&mut self) -> [u8; N] {
        let out: [u8; N] = self.data[self.pos..self.pos + N].try_into().unwrap();
        self.pos += N;
        out
    }
    fn u32(&mut self) -> u32 {
        u32::from_le_bytes(self.take())
    }
    fn i32(&mut self) -> i32 {
        i32::from_le_bytes(self.take())
    }
    fn f32(&mut self) -> f32 {
        f32::from_le_bytes(self.take())
    }
    fn f64(&mut self) -> f64 {
        f64::from_le_bytes(self.take())
    }
}

struct Lcg(u32);

impl Lcg {
    fn next(&mut self) -> u32 {
        self.0 = self.0.wrapping_mul(1664525).wrapping_add(1013904223);
        self.0
    }
}

/// Must match `golden_frame` in tools/cava-golden/harness.c.
fn golden_frame(rng: &mut Lcg, n: u32, channels: usize, out: &mut Vec<f64>) {
    let saw = ((n % 436) * 16000 / 436) as i32 - 8000;
    let sq = if (n / 27) % 2 == 0 { 3000 } else { -3000 };
    let noise = ((rng.next() >> 16) & 0x7fff) as i32 - 16384;
    let k = n % 24000;
    let mut kick = 0i32;
    if k < 4800 {
        let env = (4800 - k) as i32 * 12000 / 4800;
        let phase = (k % 120) as i32;
        let tri = if phase < 60 { phase * 2 - 60 } else { 180 - phase * 2 };
        kick = env * tri / 60;
    }
    let left = (saw + sq + noise / 4 + kick).clamp(-32768, 32767);
    let right = (saw / 2 + noise / 2 + kick).clamp(-32768, 32767);
    if channels == 2 {
        out.push(left as f64);
        out.push(right as f64);
    } else {
        out.push(((left + right) / 2) as f64);
    }
}

const CHUNK_FRAMES: [u32; 6] = [400, 800, 367, 1024, 0, 512];

#[test]
fn matches_cava_c_implementation() {
    let data = include_bytes!("data/cava_golden.bin");
    let mut r = Reader { data, pos: 0 };
    assert_eq!(&r.take::<8>(), b"CAVAGOLD");
    assert_eq!(r.u32(), 1);
    let configs = r.u32();
    let mut worst = 0f64;
    for c in 0..configs {
        let bars = r.u32() as usize;
        let rate = r.u32();
        let channels = r.u32() as usize;
        let autosens = r.i32();
        let nr = r.f64();
        let low = r.i32();
        let high = r.i32();
        let scaling = if r.u32() == 0 { Scaling::Linear } else { Scaling::Decibel };
        let frames = r.u32();
        let mut plan =
            CavaPlan::new(bars, rate, channels, autosens, nr, low, high, scaling).unwrap();

        assert_eq!(plan.bass_cut_off_bar as u32, r.u32(), "config {c}: bass_cut_off_bar");
        for i in 0..=bars {
            assert_eq!(plan.lower_cut_off[i], r.i32(), "config {c}: lower_cut_off[{i}]");
        }
        for i in 0..bars {
            assert_eq!(plan.upper_cut_off[i], r.i32(), "config {c}: upper_cut_off[{i}]");
        }
        for i in 0..=bars {
            let want = r.f32();
            assert_eq!(plan.cut_off_frequency[i], want, "config {c}: cut_off_frequency[{i}]");
        }

        let mut rng = Lcg(12345);
        let mut n = 0u32;
        let mut out = vec![0f64; bars * channels];
        for fr in 0..frames as usize {
            let chunk = CHUNK_FRAMES[fr % 6];
            let mut input = Vec::new();
            for _ in 0..chunk {
                golden_frame(&mut rng, n, channels, &mut input);
                n += 1;
            }
            assert_eq!(r.u32() as usize, input.len());
            plan.execute(&input, &mut out);
            for (i, &got) in out.iter().enumerate() {
                let want = r.f32() as f64;
                let err = (got - want).abs() / want.abs().max(1e-3);
                worst = worst.max(err);
                assert!(
                    err < 2e-4,
                    "config {c} frame {fr} bar {i}: rust {got} vs cava {want} (rel err {err})"
                );
            }
        }
    }
    assert_eq!(r.pos, data.len(), "fixture fully consumed");
    eprintln!("worst relative error vs cava: {worst:e}");
}
