//! Lists every WGSL module the engine builds (for validation tests).

use crate::scene::scene_source;

/// (name, full WGSL source) for every shader module.
pub fn all_shader_sources() -> Vec<(&'static str, String)> {
    let common = include_str!("shaders/common.wgsl");
    let mut v = vec![("post", format!("{common}\n{}", include_str!("shaders/post.wgsl")))];
    for (name, src) in crate::scenes::shader_sources() {
        v.push((name, scene_source(&src)));
    }
    v
}
