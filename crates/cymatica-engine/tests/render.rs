//! GPU integration tests (run on whatever adapter is available: lavapipe on Linux CI, Metal on
//! macOS). Skipped when there is no GPU at all.

use cymatica_engine::globals::FrameInfo;
use cymatica_engine::offline::{image_difference, image_stats, OfflineAudio, OfflineRenderer};
use cymatica_engine::params::{ParamSet, ParamValue};
use cymatica_engine::theme::builtin_themes;

fn renderer(size: (u32, u32)) -> Option<OfflineRenderer> {
    match OfflineRenderer::new(size) {
        Ok(r) => Some(r),
        Err(e) => {
            eprintln!("no GPU, skipping: {e:#}");
            None
        }
    }
}

#[test]
fn switching_scenes_crossfades_without_errors() {
    let Some(mut r) = renderer((160, 90)) else { return };
    r.crossfade = 0.5;
    let theme = &builtin_themes()[0];
    let mut audio = OfflineAudio::demo(48000);
    let mut light = ParamSet::default();
    light.set(cymatica_engine::scenes::bars::PARAMS, "style", ParamValue::Text("classic".into()));
    let mut frames = Vec::new();
    for i in 0..40u64 {
        let t = 20.0 + i as f64 / 30.0;
        let a = audio.advance_to(t).clone();
        // bars for 10 frames, then ink (crossfading for 0.5 s = 15 frames)
        let scene = if i < 10 { "bars" } else { "ink" };
        let params = if i < 10 { light.clone() } else { ParamSet::default() };
        let info = FrameInfo::new(t as f32, 1.0 / 30.0, i, a);
        let img = r.render(scene, &params, &ParamSet::default(), theme, info, 1.0).expect("render");
        frames.push(img);
    }
    for (i, f) in frames.iter().enumerate() {
        let s = image_stats(f);
        assert!(s.mean_luma > 0.005, "frame {i} is black");
    }
    // mid-fade frames differ from both the pure bars frame and the final ink frame
    let mid = &frames[17];
    assert!(image_difference(mid, &frames[9]) > 0.005);
    assert!(image_difference(mid, &frames[39]) > 0.005);
}

#[test]
fn scenes_react_to_the_music() {
    let Some(mut r) = renderer((160, 90)) else { return };
    let theme = &builtin_themes()[0];
    for scene in ["bars", "ink", "phosphor"] {
        let render_with = |r: &mut OfflineRenderer, audio: &mut OfflineAudio| {
            let mut last = None;
            r.render_sequence(
                scene,
                &ParamSet::default(),
                &ParamSet::default(),
                theme,
                audio,
                4.0,
                30.0,
                1,
                1.0,
                |_, img| {
                    last = Some(img.clone());
                    Ok(())
                },
            )
            .unwrap();
            last.unwrap()
        };
        let with_music = render_with(&mut r, &mut OfflineAudio::demo(48000));
        let silent = render_with(&mut r, &mut OfflineAudio::silence(48000));
        let d = image_difference(&with_music, &silent);
        assert!(d > 0.01, "{scene} looks the same with and without music ({d})");
    }
}
