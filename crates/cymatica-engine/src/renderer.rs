//! Ties it together: globals → scene (HDR) → post → output.

use crate::globals::{FrameInfo, GlobalsGpu};
use crate::gpu::{Gpu, HDR_FORMAT};
use crate::params::{ParamSet, Params};
use crate::post::{LookFrame, OutputMode, Post, LOOK_PARAMS};
use crate::scene::{scene_info, Scene, SceneCtx};
use crate::theme::{note_color, NoteColors, Theme};
use std::collections::HashMap;

/// One frame's worth of choices.
pub struct RenderRequest<'a> {
    pub scene: &'a str,
    pub scene_params: &'a ParamSet,
    pub look: &'a ParamSet,
    pub theme: &'a Theme,
    pub info: FrameInfo,
    /// Size the scene renders at (window size × render scale).
    pub render_size: (u32, u32),
    pub output: &'a wgpu::TextureView,
    pub output_mode: OutputMode,
    /// Seconds to crossfade when the scene changes (0 = cut).
    pub crossfade: f32,
}

struct Fade {
    scene: String,
    params: ParamSet,
    started: f32,
}

pub struct Renderer {
    pub globals: GlobalsGpu,
    post: Post,
    scenes: HashMap<String, Box<dyn Scene>>,
    hdr: Option<(wgpu::Texture, wgpu::TextureView, (u32, u32))>,
    hdr_old: Option<(wgpu::Texture, wgpu::TextureView, (u32, u32))>,
    hdr_mix: Option<(wgpu::Texture, wgpu::TextureView, (u32, u32))>,
    palette_phase: f32,
    current: Option<(String, ParamSet)>,
    fade: Option<Fade>,
}

impl Renderer {
    pub fn new(gpu: &Gpu, output_format: wgpu::TextureFormat) -> Self {
        Self {
            globals: GlobalsGpu::new(gpu),
            post: Post::new(gpu, output_format),
            scenes: HashMap::new(),
            hdr: None,
            hdr_old: None,
            hdr_mix: None,
            palette_phase: 0.0,
            current: None,
            fade: None,
        }
    }

    pub fn output_format(&self) -> wgpu::TextureFormat {
        self.post.output_format()
    }

    fn scene(&mut self, gpu: &Gpu, id: &str) -> &mut Box<dyn Scene> {
        if !self.scenes.contains_key(id) {
            let info = scene_info(id).or_else(|| scene_info("bars")).expect("scene registry is empty");
            let mut s = (info.create)(gpu, &self.globals);
            s.reset(gpu);
            self.scenes.insert(id.to_string(), s);
        }
        self.scenes.get_mut(id).unwrap()
    }

    /// Restarts a scene's simulation.
    pub fn reset_scene(&mut self, gpu: &Gpu, id: &str) {
        if let Some(s) = self.scenes.get_mut(id) {
            s.reset(gpu);
        }
    }

    /// Frees scenes other than `keep` (they're rebuilt on demand).
    pub fn drop_other_scenes(&mut self, keep: &str) {
        self.scenes.retain(|id, _| id == keep);
    }

    fn ensure_hdr(&mut self, gpu: &Gpu, size: (u32, u32)) {
        let make = |slot: &mut Option<(wgpu::Texture, wgpu::TextureView, (u32, u32))>, label: &str| {
            if slot.as_ref().map(|h| h.2) != Some(size) {
                let t = gpu.render_texture(label, size, HDR_FORMAT);
                let v = t.create_view(&Default::default());
                *slot = Some((t, v, size));
            }
        };
        make(&mut self.hdr, "hdr");
        make(&mut self.hdr_old, "hdr-old");
        make(&mut self.hdr_mix, "hdr-mix");
    }

    /// Records one frame into `encoder`.
    pub fn render(&mut self, gpu: &Gpu, encoder: &mut wgpu::CommandEncoder, req: RenderRequest) {
        let info_scene = scene_info(req.scene).or_else(|| scene_info("bars")).unwrap();
        let size = (req.render_size.0.max(16), req.render_size.1.max(16));
        self.ensure_hdr(gpu, size);
        self.globals.set_theme(gpu, req.theme);

        let mut info = req.info;
        let look = Params::new(LOOK_PARAMS, req.look, &info.audio);
        self.palette_phase += info.dt * look.f("palette_speed") * 0.25;
        info.palette_shift = self.palette_phase;
        let mode = match look.choice("note_colors").as_str() {
            "fifths" => NoteColors::CircleOfFifths,
            "chromatic" => NoteColors::Chromatic,
            _ => NoteColors::Scriabin,
        };
        info.note_color = note_color(mode, info.audio.note);
        self.globals.update(gpu, &info, size);

        let hdr_view = self.hdr.as_ref().unwrap().1.clone();
        // start a crossfade when the scene changes
        if let Some((prev_id, prev_params)) = &self.current {
            if prev_id != info_scene.id && req.crossfade > 0.01 {
                self.fade = Some(Fade { scene: prev_id.clone(), params: prev_params.clone(), started: info.time });
            }
        }
        self.current = Some((info_scene.id.to_string(), req.scene_params.clone()));
        let fade_t = self.fade.as_ref().map(|f| ((info.time - f.started) / req.crossfade.max(0.01)).clamp(0.0, 1.0));
        if fade_t.map(|t| t >= 1.0).unwrap_or(false) {
            let old = self.fade.take().map(|f| f.scene);
            if let Some(old) = old {
                // free the faded-out scene's GPU memory (it restarts fresh if picked again)
                self.scenes.remove(&old);
            }
        }

        if self.fade.is_none() {
            // only the visible scene keeps its GPU memory
            let keep = info_scene.id;
            self.scenes.retain(|id, _| id == keep);
        }
        let params = Params::new(info_scene.params, req.scene_params, &info.audio);
        self.render_scene(gpu, encoder, info_scene.id, &params, &hdr_view, size, &info);
        let mut scene_view = hdr_view.clone();
        if let (Some(fade), Some(t)) = (self.fade.as_ref().map(|f| (f.scene.clone(), f.params.clone())), fade_t) {
            if t < 1.0 {
                if let Some(old_info) = scene_info(&fade.0) {
                    let old_view = self.hdr_old.as_ref().unwrap().1.clone();
                    let old_params = Params::new(old_info.params, &fade.1, &info.audio);
                    self.render_scene(gpu, encoder, old_info.id, &old_params, &old_view, size, &info);
                    let mix_view = self.hdr_mix.as_ref().unwrap().1.clone();
                    let s = t * t * (3.0 - 2.0 * t);
                    self.post.crossfade(gpu, encoder, &hdr_view, &old_view, &mix_view, s);
                    scene_view = mix_view;
                }
            }
        }

        let look_frame =
            LookFrame { time: info.time, frame: info.frame, kick: info.audio.onset[0], note_color: info.note_color };
        let look = Params::new(LOOK_PARAMS, req.look, &info.audio);
        self.post.run(gpu, encoder, &scene_view, size, req.output, req.output_mode, &look, &look_frame);
    }

    #[allow(clippy::too_many_arguments)]
    fn render_scene(
        &mut self,
        gpu: &Gpu,
        encoder: &mut wgpu::CommandEncoder,
        id: &str,
        params: &Params,
        target: &wgpu::TextureView,
        size: (u32, u32),
        info: &FrameInfo,
    ) {
        let globals = &self.globals as *const GlobalsGpu;
        let scene = self.scene(gpu, id);
        // SAFETY: `scene()` only touches `self.scenes`; globals is not mutated while borrowed here.
        let globals = unsafe { &*globals };
        let mut ctx = SceneCtx { encoder, target, size, globals, info, params };
        scene.render(gpu, &mut ctx);
    }
}
