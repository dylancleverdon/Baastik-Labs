//! cargo run -p cymatica-engine --example render -- <scene> <seconds> <WxH> <out.png> [theme] [key=value ...]
use cymatica_engine::offline::{OfflineAudio, OfflineRenderer};
use cymatica_engine::params::{ParamSet, ParamValue};
use cymatica_engine::scene::scene_info;
use cymatica_engine::theme::builtin_themes;

fn main() -> anyhow::Result<()> {
    let args: Vec<String> = std::env::args().collect();
    let scene = args.get(1).map(String::as_str).unwrap_or("bars");
    let seconds: f64 = args.get(2).and_then(|s| s.parse().ok()).unwrap_or(20.0);
    let (w, h) = args
        .get(3)
        .and_then(|s| s.split_once('x'))
        .map(|(a, b)| (a.parse().unwrap(), b.parse().unwrap()))
        .unwrap_or((1280, 720));
    let out = args.get(4).cloned().unwrap_or_else(|| "frame.png".into());
    let theme_id = args.get(5).cloned().unwrap_or_else(|| "synthwave".into());
    let theme = builtin_themes().into_iter().find(|t| t.id == theme_id).expect("theme");
    let info = scene_info(scene).expect("scene");
    let mut params = ParamSet::default();
    let mut look = ParamSet::default();
    for kv in args.iter().skip(6) {
        let (k, v) = kv.split_once('=').expect("key=value");
        let (set, defs, key) = if let Some(k) = k.strip_prefix("look.") {
            (&mut look, cymatica_engine::post::LOOK_PARAMS, k)
        } else {
            (&mut params, info.params, k)
        };
        let value = if let Ok(n) = v.parse::<f64>() {
            ParamValue::Number(n)
        } else if v == "true" || v == "false" {
            ParamValue::Bool(v == "true")
        } else {
            ParamValue::Text(v.to_string())
        };
        set.set(defs, key, value);
    }
    let mut r = OfflineRenderer::new((w, h))?;
    let mut audio = OfflineAudio::demo(48000);
    let t0 = std::time::Instant::now();
    r.render_sequence(scene, &params, &look, &theme, &mut audio, seconds, 60.0, 1, 1.0, |_, img| {
        img.save(&out)?;
        Ok(())
    })?;
    eprintln!("rendered {out} in {:.1}s on {}", t0.elapsed().as_secs_f32(), r.gpu.adapter_info.name);
    Ok(())
}
