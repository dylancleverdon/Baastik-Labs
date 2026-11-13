//! Renders a scene as for the see-through overlay window and reports its alpha:
//! `cargo run -p cymatica-engine --example overlay_probe -- bars [bloom=0 ...]` (writes overlay_<scene>.png).

use cymatica_engine::offline::{OfflineAudio, OfflineRenderer};
use cymatica_engine::params::ParamSet;
use cymatica_engine::theme::builtin_themes;
fn main() {
    let mut r = OfflineRenderer::new((320, 180)).unwrap();
    r.transparent = true;
    let theme = &builtin_themes()[0];
    let scene = std::env::args().nth(1).unwrap_or("bars".into());
    let mut look = ParamSet::default();
    for kv in std::env::args().skip(2) {
        let (k, v) = kv.split_once('=').unwrap();
        look.set(
            cymatica_engine::post::LOOK_PARAMS,
            k,
            cymatica_engine::params::ParamValue::Number(v.parse().unwrap()),
        );
    }
    let mut last = None;
    r.render_sequence(
        &scene,
        &ParamSet::default(),
        &look,
        theme,
        &mut OfflineAudio::demo(48000),
        22.0,
        30.0,
        1,
        1.0,
        |_, img| {
            last = Some(img.clone());
            Ok(())
        },
    )
    .unwrap();
    let img = last.unwrap();
    for (x, y) in [(2, 2), (160, 10), (300, 20), (10, 90)] {
        println!("px({x},{y}) = {:?}", img.get_pixel(x, y).0);
    }
    let mut hist = [0usize; 8];
    for p in img.pixels() {
        hist[(p[3] / 32) as usize] += 1;
    }
    println!("alpha histogram (32-wide buckets): {hist:?}");
    img.save(format!("overlay_{scene}.png")).unwrap();
}
