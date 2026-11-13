//! Cymatica rendering engine: GPU physics scenes, themes, the post-processing "look", and
//! headless rendering.

pub mod globals;
pub mod gpu;
pub mod offline;
pub mod params;
pub mod post;
pub mod renderer;
pub mod scene;
pub mod scenes;
pub mod shaders;
pub mod theme;

pub use gpu::Gpu;
pub use params::{ParamSet, ParamValue, Params};
pub use renderer::{RenderRequest, Renderer};
pub use theme::Theme;
