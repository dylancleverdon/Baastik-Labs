//! Command-line modes: offline rendering, the self-test contact sheet, and updater test hooks.

use anyhow::{bail, Context, Result};
use cymatica_engine::offline::{image_stats, OfflineAudio, OfflineRenderer};
use cymatica_engine::params::{ParamSet, ParamValue};
use cymatica_engine::post::LOOK_PARAMS;
use cymatica_engine::scene::{scene_info, scenes};
use cymatica_engine::theme::builtin_themes;
use std::path::PathBuf;

struct Args(Vec<String>);

impl Args {
    fn value(&self, name: &str) -> Option<String> {
        self.0.iter().position(|a| a == name).and_then(|i| self.0.get(i + 1)).cloned()
    }
    fn values(&self, name: &str) -> Vec<String> {
        self.0.windows(2).filter(|w| w[0] == name).map(|w| w[1].clone()).collect()
    }
    fn flag(&self, name: &str) -> bool {
        self.0.iter().any(|a| a == name)
    }
}

fn parse_size(s: &str) -> Result<(u32, u32)> {
    let (w, h) = s.split_once('x').context("size must look like 1920x1080")?;
    Ok((w.parse()?, h.parse()?))
}

fn parse_value(v: &str) -> ParamValue {
    if let Ok(n) = v.parse::<f64>() {
        ParamValue::Number(n)
    } else if v == "true" || v == "false" {
        ParamValue::Bool(v == "true")
    } else {
        ParamValue::Text(v.to_string())
    }
}

/// `cymatica render --scene ink --start 20 --seconds 4 --fps 60 --size 3840x2160 --out frames/`
pub fn render(args: Vec<String>) -> Result<()> {
    let a = Args(args);
    let scene = a.value("--scene").unwrap_or_else(|| "bars".into());
    let info = scene_info(&scene).with_context(|| format!("unknown scene {scene}"))?;
    let size = parse_size(&a.value("--size").unwrap_or_else(|| "1920x1080".into()))?;
    let fps: f64 = a.value("--fps").map(|v| v.parse()).transpose()?.unwrap_or(60.0);
    let start: f64 = a.value("--start").map(|v| v.parse()).transpose()?.unwrap_or(20.0);
    let seconds: f64 = a.value("--seconds").map(|v| v.parse()).transpose()?.unwrap_or(0.0);
    let scale: f32 = a.value("--scale").map(|v| v.parse()).transpose()?.unwrap_or(1.0);
    let out = PathBuf::from(a.value("--out").unwrap_or_else(|| "cymatica-render".into()));
    let theme_id = a.value("--theme").unwrap_or_else(|| "synthwave".into());
    let theme = builtin_themes().into_iter().find(|t| t.id == theme_id).context("unknown theme")?;
    let mut params = ParamSet::default();
    for kv in a.values("--set") {
        let (k, v) = kv.split_once('=').context("--set needs key=value")?;
        params.set(info.params, k, parse_value(v));
    }
    let mut look = ParamSet::default();
    for kv in a.values("--look") {
        let (k, v) = kv.split_once('=').context("--look needs key=value")?;
        look.set(LOOK_PARAMS, k, parse_value(v));
    }
    let mut audio = match a.value("--audio") {
        Some(p) => {
            let wav = cymatica_audio::wav::read_wav(std::path::Path::new(&p))?;
            OfflineAudio::samples(wav.rate, wav.samples)
        }
        None => OfflineAudio::demo(48000),
    };
    let frames = ((seconds * fps).round() as u64).max(1);
    let single = out.extension().map(|e| e == "png").unwrap_or(false) && frames == 1;
    if !single {
        std::fs::create_dir_all(&out)?;
    }
    let mut r = OfflineRenderer::new(size)?;
    eprintln!("rendering {frames} frame(s) of {scene} at {}x{} on {}", size.0, size.1, r.gpu.adapter_info.name);
    r.render_sequence(&scene, &params, &look, &theme, &mut audio, start, fps, frames, scale, |i, img| {
        let path = if single { out.clone() } else { out.join(format!("frame_{i:05}.png")) };
        img.save(&path)?;
        Ok(())
    })?;
    Ok(())
}

/// Renders every scene with the demo track, checks the pictures, writes a contact sheet.
pub fn self_test(args: Vec<String>) -> Result<()> {
    let a = Args(args);
    let size = parse_size(&a.value("--size").unwrap_or_else(|| "480x270".into()))?;
    let out = PathBuf::from(a.value("--out").unwrap_or_else(|| "cymatica-self-test.png".into()));
    let at: f64 = a.value("--at").map(|v| v.parse()).transpose()?.unwrap_or(23.0);
    let mut r = OfflineRenderer::new(size)?;
    eprintln!("self-test on {} ({:?})", r.gpu.adapter_info.name, r.gpu.adapter_info.backend);
    let themes = builtin_themes();
    let all = scenes();
    let cols = 3u32;
    let rows = (all.len() as u32).div_ceil(cols);
    let mut sheet = image::RgbaImage::new(size.0 * cols, size.1 * rows);
    let mut failures = Vec::new();
    for (i, sc) in all.iter().enumerate() {
        let theme = &themes[i % themes.len()];
        let mut audio = OfflineAudio::demo(48000);
        let mut last = None;
        let t0 = std::time::Instant::now();
        r.render_sequence(
            sc.id,
            &ParamSet::default(),
            &ParamSet::default(),
            theme,
            &mut audio,
            at,
            30.0,
            1,
            1.0,
            |_, img| {
                last = Some(img.clone());
                Ok(())
            },
        )?;
        let img = last.context("no frame rendered")?;
        let stats = image_stats(&img);
        eprintln!(
            "  {:<12} mean {:.3} max {:.3} lit {:.3}  ({:.1}s)",
            sc.id,
            stats.mean_luma,
            stats.max_luma,
            stats.lit_fraction,
            t0.elapsed().as_secs_f32()
        );
        if stats.mean_luma < 0.01 || stats.lit_fraction < 0.005 {
            failures.push(format!("{} rendered an (almost) black frame", sc.id));
        }
        let (x, y) = ((i as u32 % cols) * size.0, (i as u32 / cols) * size.1);
        image::imageops::overlay(&mut sheet, &img, x as i64, y as i64);
    }
    sheet.save(&out)?;
    eprintln!("contact sheet: {}", out.display());
    if !failures.is_empty() {
        bail!("self-test failed:\n{}", failures.join("\n"));
    }
    Ok(())
}

/// Renders every built-in preset's thumbnail (as the Presets tab shows them) into a contact sheet,
/// failing if any comes out black: `cymatica thumbnails --out presets.png [--only liquid]`.
pub fn thumbnails(args: Vec<String>) -> Result<()> {
    use crate::thumbnails::{demo_frames, render, Job, SIZE};
    let a = Args(args);
    let out = PathBuf::from(a.value("--out").unwrap_or_else(|| "cymatica-presets.png".into()));
    let only = a.value("--only");
    let settings = crate::settings::Settings::default();
    let presets: Vec<_> = crate::presets::builtin_presets()
        .into_iter()
        .filter(|p| only.as_deref().is_none_or(|s| p.scene == s))
        .collect();
    let mut r = OfflineRenderer::new(SIZE)?;
    eprintln!("{} preset thumbnails on {} ({:?})", presets.len(), r.gpu.adapter_info.name, r.gpu.adapter_info.backend);
    let frames = demo_frames();
    let cols = 6u32;
    let rows = (presets.len() as u32).div_ceil(cols).max(1);
    let mut sheet = image::RgbaImage::new(SIZE.0 * cols, SIZE.1 * rows);
    let mut failures = Vec::new();
    for (i, p) in presets.iter().enumerate() {
        let t0 = std::time::Instant::now();
        let img = render(&mut r, &Job::for_preset(p, &settings), &frames)?;
        let stats = image_stats(&img);
        eprintln!(
            "  {:<36} mean {:.3} lit {:.3}  ({:.1}s)",
            p.key(),
            stats.mean_luma,
            stats.lit_fraction,
            t0.elapsed().as_secs_f32()
        );
        if stats.mean_luma < 0.01 || stats.lit_fraction < 0.005 {
            failures.push(format!("{} rendered an (almost) black thumbnail", p.key()));
        }
        let (x, y) = ((i as u32 % cols) * SIZE.0, (i as u32 / cols) * SIZE.1);
        image::imageops::overlay(&mut sheet, &img, x as i64, y as i64);
    }
    sheet.save(&out)?;
    eprintln!("contact sheet: {}", out.display());
    if !failures.is_empty() {
        bail!("thumbnails failed:\n{}", failures.join("\n"));
    }
    Ok(())
}

/// Updater test hooks used by CI (mirrors Norg's updater e2e test).
/// `cymatica update --home H --manifest-url file://... --allow-url-prefix file:// --current-build N --check-now`
/// `cymatica update --home H --rollback`
pub fn update(args: Vec<String>) -> Result<()> {
    use cymatica_update as upd;
    let a = Args(args);
    let home = a.value("--home").map(PathBuf::from).unwrap_or_else(crate::paths::home);
    let paths = upd::Paths::new(&home);
    let target = a.value("--install-to").map(PathBuf::from).unwrap_or_else(|| paths.default_app());
    if a.flag("--rollback") {
        upd::rollback(&paths, &target)?;
        println!("rolled back: {} is build {:?}", target.display(), upd::bundle_build(&target));
        return Ok(());
    }
    let opts = upd::CheckOptions {
        manifest_url: a.value("--manifest-url").unwrap_or_else(upd::default_manifest_url),
        extra_prefixes: a.values("--allow-url-prefix"),
        manual: true,
    };
    let installed =
        a.value("--current-build").and_then(|b| b.parse().ok()).or_else(|| upd::bundle_build(&target)).unwrap_or(0);
    let (m, decision) = upd::check(&paths, installed, &opts)?;
    println!("manifest: {} (build {}), installed build {installed}: {decision:?}", m.version, m.build);
    if a.flag("--check-now") && decision == upd::Decision::Install {
        let staged = upd::stage(&paths, &m, &opts)?;
        upd::install(&paths, &staged, &target)?;
        println!("installed {} at {}", m.version, target.display());
    }
    Ok(())
}
