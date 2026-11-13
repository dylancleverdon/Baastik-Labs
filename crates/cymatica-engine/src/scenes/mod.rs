//! Every scene, in menu order.

use crate::scene::SceneInfo;

pub mod bars;
pub mod ink;
pub mod ripples;

pub static ALL: [SceneInfo; 3] = [
    SceneInfo {
        id: "bars",
        name: "Bars",
        tagline: "cava's bars in HD, with springs, caps and sparks",
        params: bars::PARAMS,
        create: bars::create,
    },
    SceneInfo {
        id: "ink",
        name: "Ink",
        tagline: "Coloured ink in a real fluid: every band is a jet, kicks splash",
        params: ink::PARAMS,
        create: ink::create,
    },
    SceneInfo {
        id: "ripples",
        name: "Ripples",
        tagline: "A pool of water: the music drops pebbles, caustics dance on the floor",
        params: ripples::PARAMS,
        create: ripples::create,
    },
];

/// Scene-specific WGSL (without the shared prelude), for validation.
pub fn shader_sources() -> Vec<(&'static str, String)> {
    let bars_common = include_str!("../shaders/scenes/bars_common.wgsl");
    vec![
        ("bars-sim", format!("{bars_common}\n{}", include_str!("../shaders/scenes/bars_sim.wgsl"))),
        ("bars-draw", format!("{bars_common}\n{}", include_str!("../shaders/scenes/bars_draw.wgsl"))),
        ("ink", include_str!("../shaders/scenes/ink.wgsl").to_string()),
        ("ripples", include_str!("../shaders/scenes/ripples.wgsl").to_string()),
    ]
}
