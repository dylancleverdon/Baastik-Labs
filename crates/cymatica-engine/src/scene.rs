//! The scene interface and the registry of all scenes.

use crate::globals::{FrameInfo, GlobalsGpu};
use crate::gpu::Gpu;
use crate::params::{ParamDef, Params};

/// What a scene gets each frame.
pub struct SceneCtx<'a> {
    pub encoder: &'a mut wgpu::CommandEncoder,
    /// HDR target (`gpu::HDR_FORMAT`) the scene must fully overwrite.
    pub target: &'a wgpu::TextureView,
    pub size: (u32, u32),
    pub globals: &'a GlobalsGpu,
    pub info: &'a FrameInfo,
    pub params: &'a Params<'a>,
}

pub trait Scene {
    /// Steps the simulation and draws into `ctx.target`.
    fn render(&mut self, gpu: &Gpu, ctx: &mut SceneCtx);
    /// Starts the simulation over (e.g. when a preset loads).
    fn reset(&mut self, _gpu: &Gpu) {}
}

/// Static description of a scene for menus and presets.
pub struct SceneInfo {
    /// Permanent ID (stored in presets).
    pub id: &'static str,
    pub name: &'static str,
    pub tagline: &'static str,
    pub params: &'static [ParamDef],
    pub create: fn(&Gpu, &GlobalsGpu) -> Box<dyn Scene>,
}

pub fn scenes() -> &'static [SceneInfo] {
    &crate::scenes::ALL
}

pub fn scene_info(id: &str) -> Option<&'static SceneInfo> {
    scenes().iter().find(|s| s.id == id)
}

/// WGSL for a scene: the common prelude + globals + the scene's own code.
pub fn scene_source(scene_wgsl: &str) -> String {
    format!(
        "{}\n{}\n{}",
        include_str!("shaders/common.wgsl"),
        include_str!("shaders/globals.wgsl"),
        scene_wgsl
    )
}
