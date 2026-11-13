//! Every scene, in menu order.

use crate::scene::SceneInfo;

pub mod acid;
pub mod bars;
pub mod chladni;
pub mod dancers;
pub mod ferro;
pub mod ink;
pub mod kaleido;
pub mod liquid;
pub mod mandelbrot;
pub mod phosphor;
pub mod ripples;
pub mod singularity;
pub mod stardust;

pub static ALL: [SceneInfo; 13] = [
    SceneInfo {
        id: "liquid",
        name: "Liquid",
        tagline: "A real 3D liquid in a glass tank: bass sloshes it, kicks fire geysers",
        params: liquid::PARAMS,
        create: liquid::create,
    },
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
    SceneInfo {
        id: "chladni",
        name: "Chladni",
        tagline: "Cymatics: sand on a vibrating plate draws the music's standing waves",
        params: chladni::PARAMS,
        create: chladni::create,
    },
    SceneInfo {
        id: "ferrofluid",
        name: "Ferrofluid",
        tagline: "Glossy magnetic fluid: every spike rides its own frequency band",
        params: ferro::PARAMS,
        create: ferro::create,
    },
    SceneInfo {
        id: "singularity",
        name: "Singularity",
        tagline: "A ray-traced black hole: the bass heats its accretion disk, kicks fire jets",
        params: singularity::PARAMS,
        create: singularity::create,
    },
    SceneInfo {
        id: "stardust",
        name: "Stardust",
        tagline: "A galaxy of millions of stars: bass is gravity, kicks are supernovas",
        params: stardust::PARAMS,
        create: stardust::create,
    },
    SceneInfo {
        id: "phosphor",
        name: "Phosphor",
        tagline: "An oscilloscope drawn by a simulated CRT electron beam",
        params: phosphor::PARAMS,
        create: phosphor::create,
    },
    SceneInfo {
        id: "kaleidoscope",
        name: "Kaleidoscope",
        tagline: "Stained glass, petals and spectrum lace in mirrors that stream outward forever",
        params: kaleido::PARAMS,
        create: kaleido::create,
    },
    SceneInfo {
        id: "acid",
        name: "Acid",
        tagline: "Psychedelic feedback: rainbow trails pour outward, melt and loop forever",
        params: acid::PARAMS,
        create: acid::create,
    },
    SceneInfo {
        id: "dancers",
        name: "Dancers",
        tagline: "A crowd of neon stick figures dancing to the beat on a disco floor",
        params: dancers::PARAMS,
        create: dancers::create,
    },
    SceneInfo {
        id: "mandelbrot",
        name: "Mandelbrot",
        tagline: "An endless dive into the Mandelbrot set that follows the detail down",
        params: mandelbrot::PARAMS,
        create: mandelbrot::create,
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
        ("liquid-sim", include_str!("../shaders/scenes/liquid_sim.wgsl").to_string()),
        ("liquid-draw", include_str!("../shaders/scenes/liquid_draw.wgsl").to_string()),
        (
            "chladni-sim",
            format!(
                "{}\n{}",
                include_str!("../shaders/scenes/chladni_common.wgsl"),
                include_str!("../shaders/scenes/chladni_sim.wgsl")
            ),
        ),
        (
            "chladni-draw",
            format!(
                "{}\n{}",
                include_str!("../shaders/scenes/chladni_common.wgsl"),
                include_str!("../shaders/scenes/chladni_draw.wgsl")
            ),
        ),
        ("phosphor", include_str!("../shaders/scenes/phosphor.wgsl").to_string()),
        ("singularity", include_str!("../shaders/scenes/singularity.wgsl").to_string()),
        ("ferro", include_str!("../shaders/scenes/ferro.wgsl").to_string()),
        ("kaleido", include_str!("../shaders/scenes/kaleido.wgsl").to_string()),
        ("acid", include_str!("../shaders/scenes/acid.wgsl").to_string()),
        ("dancers", include_str!("../shaders/scenes/dancers.wgsl").to_string()),
        ("mandelbrot", include_str!("../shaders/scenes/mandelbrot.wgsl").to_string()),
        (
            "stardust-sim",
            format!(
                "{}\n{}",
                include_str!("../shaders/scenes/stardust_common.wgsl"),
                include_str!("../shaders/scenes/stardust_sim.wgsl")
            ),
        ),
        (
            "stardust-draw",
            format!(
                "{}\n{}",
                include_str!("../shaders/scenes/stardust_common.wgsl"),
                include_str!("../shaders/scenes/stardust_draw.wgsl")
            ),
        ),
    ]
}
