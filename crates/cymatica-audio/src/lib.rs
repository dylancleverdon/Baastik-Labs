//! Cymatica's audio side: capture (BlackHole 2ch by default, like cava), cava's bar algorithm,
//! and the music features the scenes react to.

pub mod analysis;
pub mod capture;
pub mod cava;
pub mod cava_config;
pub mod engine;
pub mod synth;
pub mod wav;

pub use analysis::{Analyzer, AudioFrame};
pub use cava::{CavaSettings, ChannelLayout, Scaling};
pub use engine::{AudioEngine, AudioStatus, InputChoice, SourceKind};
