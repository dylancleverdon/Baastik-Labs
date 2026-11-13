//! Every scene, in menu order.

use crate::scene::SceneInfo;

pub mod bars;

pub static ALL: [SceneInfo; 1] = [
    SceneInfo {
        id: "bars",
        name: "Bars",
        tagline: "cava's bars in HD, with springs, caps and sparks",
        params: bars::PARAMS,
        create: bars::create,
    },
];

/// Scene-specific WGSL (without the shared prelude), for validation.
pub fn shader_sources() -> Vec<(&'static str, String)> {
    let bars_common = include_str!("../shaders/scenes/bars_common.wgsl");
    vec![
        ("bars-sim", format!("{bars_common}\n{}", include_str!("../shaders/scenes/bars_sim.wgsl"))),
        ("bars-draw", format!("{bars_common}\n{}", include_str!("../shaders/scenes/bars_draw.wgsl"))),
    ]
}
