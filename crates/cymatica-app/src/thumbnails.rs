//! Preset thumbnails: small previews of what each preset looks like, rendered in the background
//! (the demo track, the preset's own theme and look) and cached on disk, so the Presets tab can
//! show pictures instead of names.

use crate::presets::Preset;
use crate::settings::Settings;
use cymatica_audio::AudioFrame;
use cymatica_engine::globals::FrameInfo;
use cymatica_engine::offline::{OfflineAudio, OfflineRenderer};
use cymatica_engine::params::ParamSet;
use cymatica_engine::theme::Theme;
use std::collections::{HashMap, HashSet};
use std::hash::{Hash, Hasher};
use std::path::{Path, PathBuf};
use std::sync::mpsc;
use std::time::{Duration, Instant};

/// Rendered size (shown at half that in points, so it's sharp on Retina screens).
pub const SIZE: (u32, u32) = (256, 144);
/// Bump to render every thumbnail again (they're also redone after each update).
const VERSION: u32 = 1;
/// Demo-track stretch a thumbnail shows: the simulation runs for `WARM_FRAMES`, then one frame
/// is kept.
const START: f64 = 20.0;
const FPS: f64 = 30.0;
const WARM_FRAMES: u64 = 90;

/// Everything that decides what a preset's thumbnail looks like.
#[derive(Clone)]
pub struct Job {
    /// The preset's key (scene/name).
    pub key: String,
    /// Content hash: the cache file name, and how changes are noticed.
    pub hash: u64,
    pub scene: String,
    pub params: ParamSet,
    pub theme: Theme,
    pub look: ParamSet,
}

impl Job {
    /// The preset as loading it would show it: its own theme (or the current one) and its own
    /// look (or the default).
    pub fn for_preset(p: &Preset, settings: &Settings) -> Self {
        let theme =
            p.theme.as_deref().and_then(|t| settings.theme_by_id(t)).unwrap_or_else(|| settings.current_theme());
        let look = p.look.clone().unwrap_or_default();
        let mut h = std::collections::hash_map::DefaultHasher::new();
        VERSION.hash(&mut h);
        crate::updates::build_number().hash(&mut h);
        p.scene.hash(&mut h);
        serde_json::to_string(&p.params).unwrap_or_default().hash(&mut h);
        serde_json::to_string(&theme).unwrap_or_default().hash(&mut h);
        serde_json::to_string(&look).unwrap_or_default().hash(&mut h);
        Self { key: p.key(), hash: h.finish(), scene: p.scene.clone(), params: p.params.clone(), theme, look }
    }
}

/// The demo track's analysis for the frames a thumbnail runs through (the same for all of them).
pub fn demo_frames() -> Vec<AudioFrame> {
    let mut audio = OfflineAudio::demo(48000);
    (0..=WARM_FRAMES).map(|i| audio.advance_to(START + i as f64 / FPS).clone()).collect()
}

/// Renders one thumbnail with a renderer of `SIZE`.
pub fn render(r: &mut OfflineRenderer, job: &Job, frames: &[AudioFrame]) -> anyhow::Result<image::RgbaImage> {
    let (last, warm) = frames.split_last().ok_or_else(|| anyhow::anyhow!("no audio frames"))?;
    let info = |i: usize, a: &AudioFrame| {
        FrameInfo::new((START + i as f64 / FPS) as f32, (1.0 / FPS) as f32, i as u64, a.clone())
    };
    for (i, a) in warm.iter().enumerate() {
        r.step(&job.scene, &job.params, &job.look, &job.theme, info(i, a), 1.0)?;
    }
    r.render(&job.scene, &job.params, &job.look, &job.theme, info(warm.len(), last), 1.0)
}

fn cache_file(dir: &Path, hash: u64) -> PathBuf {
    dir.join(format!("{hash:016x}.png"))
}

/// One folder per build (thumbnails are redone after an update, so older ones are never used).
fn cache_dir() -> PathBuf {
    crate::paths::support_dir().join("thumbnails").join(format!("b{}", crate::updates::build_number()))
}

/// Deletes thumbnail folders of other builds (only folders named like ours: "b" + digits).
fn remove_other_builds(current: &Path) {
    let (Some(parent), Some(mine)) = (current.parent(), current.file_name()) else { return };
    let Ok(entries) = std::fs::read_dir(parent) else { return };
    for e in entries.flatten() {
        let name = e.file_name();
        let s = name.to_string_lossy();
        let ours = s.len() > 1 && s.starts_with('b') && s[1..].chars().all(|c| c.is_ascii_digit());
        if ours && name != mine && e.path().is_dir() {
            let _ = std::fs::remove_dir_all(e.path());
        }
    }
}

type Done = (String, u64, Option<image::RgbaImage>);

/// Keeps the Presets tab's pictures: asks the background renderer for missing or outdated ones
/// and turns finished ones into textures.
pub struct Thumbnails {
    jobs: Option<mpsc::Sender<Job>>,
    done: Option<mpsc::Receiver<Done>>,
    textures: HashMap<String, (u64, egui::TextureHandle)>,
    pending: HashSet<u64>,
    last_want: Option<Instant>,
}

impl Thumbnails {
    pub fn new() -> Self {
        Self { jobs: None, done: None, textures: HashMap::new(), pending: HashSet::new(), last_want: None }
    }

    /// The picture for a preset, if it's ready (possibly from before the preset last changed).
    pub fn get(&self, key: &str) -> Option<&egui::TextureHandle> {
        self.textures.get(key).map(|(_, t)| t)
    }

    /// Asks for pictures of these presets; cheap to call every frame (it checks twice a second).
    pub fn want<'a>(&mut self, presets: impl IntoIterator<Item = &'a Preset>, settings: &Settings) {
        if self.last_want.is_some_and(|t| t.elapsed() < Duration::from_millis(500)) {
            return;
        }
        self.last_want = Some(Instant::now());
        for p in presets {
            let job = Job::for_preset(p, settings);
            let current = self.textures.get(&job.key).is_some_and(|(h, _)| *h == job.hash);
            if current || self.pending.contains(&job.hash) {
                continue;
            }
            self.pending.insert(job.hash);
            if self.jobs.is_none() {
                self.start_worker();
            }
            if let Some(tx) = &self.jobs {
                let _ = tx.send(job);
            }
        }
    }

    /// Turns finished renders into textures (call once per frame).
    pub fn poll(&mut self, ctx: &egui::Context) {
        let Some(rx) = &self.done else { return };
        while let Ok((key, hash, img)) = rx.try_recv() {
            self.pending.remove(&hash);
            if let Some(img) = img {
                let size = [img.width() as usize, img.height() as usize];
                let color = egui::ColorImage::from_rgba_unmultiplied(size, img.as_raw());
                let tex = ctx.load_texture(format!("preset-{key}"), color, egui::TextureOptions::LINEAR);
                self.textures.insert(key, (hash, tex));
            }
        }
    }

    fn start_worker(&mut self) {
        let (job_tx, job_rx) = mpsc::channel::<Job>();
        let (done_tx, done_rx) = mpsc::channel::<Done>();
        let dir = cache_dir();
        let spawned = std::thread::Builder::new().name("cymatica-thumbnails".into()).spawn(move || {
            remove_other_builds(&dir);
            let mut renderer: Option<OfflineRenderer> = None;
            let mut frames: Option<Vec<AudioFrame>> = None;
            let mut gpu_failed = false;
            loop {
                let job = match job_rx.recv_timeout(Duration::from_secs(20)) {
                    Ok(job) => job,
                    // idle: give the GPU memory back (it's made again on the next request)
                    Err(mpsc::RecvTimeoutError::Timeout) => {
                        renderer = None;
                        continue;
                    }
                    Err(mpsc::RecvTimeoutError::Disconnected) => break,
                };
                let path = cache_file(&dir, job.hash);
                let mut img = image::open(&path).ok().map(|i| i.to_rgba8());
                if img.is_none() && !gpu_failed {
                    if renderer.is_none() {
                        match OfflineRenderer::new(SIZE) {
                            Ok(r) => renderer = Some(r),
                            Err(e) => {
                                log::warn!("preset thumbnails: no GPU ({e:#})");
                                gpu_failed = true;
                            }
                        }
                    }
                    if let Some(r) = renderer.as_mut() {
                        let frames = frames.get_or_insert_with(demo_frames);
                        match render(r, &job, frames) {
                            Ok(rendered) => {
                                let _ = std::fs::create_dir_all(&dir);
                                if let Err(e) = rendered.save(&path) {
                                    log::warn!("preset thumbnail for {} not saved: {e}", job.key);
                                }
                                img = Some(rendered);
                            }
                            Err(e) => log::warn!("preset thumbnail for {} failed: {e:#}", job.key),
                        }
                    }
                }
                if done_tx.send((job.key, job.hash, img)).is_err() {
                    break;
                }
            }
        });
        match spawned {
            Ok(_) => {
                self.jobs = Some(job_tx);
                self.done = Some(done_rx);
            }
            Err(e) => log::warn!("preset thumbnails: no worker thread ({e})"),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_thumbnail_is_redone_when_its_preset_or_theme_changes() {
        let mut settings = Settings::default();
        let presets = crate::presets::builtin_presets();
        let p = presets.iter().find(|p| p.theme.is_none()).expect("a preset that follows the current theme");
        let a = Job::for_preset(p, &settings);
        assert_eq!(a.hash, Job::for_preset(p, &settings).hash, "the hash must be stable");
        settings.theme = "nord".into();
        assert_ne!(a.hash, Job::for_preset(p, &settings).hash, "theme-less presets follow the current theme");
        let mut changed = p.clone();
        changed.params = presets.iter().find(|q| q.scene == p.scene && q.params != p.params).unwrap().params.clone();
        assert_ne!(Job::for_preset(p, &settings).hash, Job::for_preset(&changed, &settings).hash);
    }

    #[test]
    fn only_other_builds_folders_are_cleaned_up() {
        let root = std::env::temp_dir().join(format!("cymatica-thumbs-test-{}", std::process::id()));
        let mine = root.join("b7");
        for d in ["b7", "b6", "b12", "keep", "bx"] {
            std::fs::create_dir_all(root.join(d)).unwrap();
        }
        std::fs::write(root.join("b5"), b"a file, not a folder").unwrap();
        remove_other_builds(&mine);
        let mut left: Vec<String> =
            std::fs::read_dir(&root).unwrap().flatten().map(|e| e.file_name().to_string_lossy().into_owned()).collect();
        left.sort();
        assert_eq!(left, ["b5", "b7", "bx", "keep"]);
        let _ = std::fs::remove_dir_all(&root);
    }
}
