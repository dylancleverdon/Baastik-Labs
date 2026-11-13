//! The windowed app: event loop, GPU surface, UI, input and the frame loop.

use crate::autopilot::Autopilot;
use crate::macos;
use crate::presets::{self, Preset};
use crate::settings::{Settings, WindowMode};
use crate::ui::{self, UiInfo, UiState};
use crate::updates::{self, UpdateState, Updates};
use cymatica_audio::{AudioEngine, AudioFrame};
use cymatica_engine::globals::FrameInfo;
use cymatica_engine::offline::OfflineRenderer;
use cymatica_engine::params::ParamSet;
use cymatica_engine::post::OutputMode;
use cymatica_engine::scene::scenes;
use cymatica_engine::{Gpu, RenderRequest, Renderer};
use std::sync::Arc;
use std::time::{Duration, Instant};
use winit::application::ApplicationHandler;
use winit::event::{ElementState, WindowEvent};
use winit::event_loop::{ActiveEventLoop, EventLoop};
use winit::keyboard::{Key, NamedKey};
use winit::window::{Fullscreen, Window, WindowId, WindowLevel};

struct Running {
    window: Arc<Window>,
    _instance: wgpu::Instance,
    surface: wgpu::Surface<'static>,
    config: wgpu::SurfaceConfiguration,
    gpu: Gpu,
    renderer: Renderer,
    egui_ctx: egui::Context,
    egui_state: egui_winit::State,
    egui_renderer: egui_wgpu::Renderer,
    output_mode: OutputMode,
    hdr_active: bool,
    edr_supported: bool,
    last: Instant,
    sim_time: f32,
    frame: u64,
    fps: f32,
    accent: [u8; 3],
}

pub struct App {
    settings: Settings,
    audio: AudioEngine,
    updates: Updates,
    presets: Vec<Preset>,
    ui: UiState,
    run: Option<Running>,
    dirty_at: Option<Instant>,
    frozen: Option<AudioFrame>,
    last_input: Instant,
    seed: u64,
    preset_cursor: i64,
    autopilot: Autopilot,
    cava_config_path: Option<std::path::PathBuf>,
    mic_requested: bool,
}

pub fn run() -> anyhow::Result<()> {
    let settings = Settings::load();
    let audio = AudioEngine::start(settings.input.clone(), settings.cava.clone());
    let mut presets = presets::builtin_presets();
    presets.extend(presets::user_presets());
    let cava_config_path = cymatica_audio::cava_config::default_path().filter(|p| p.exists());
    let mut app = App {
        ui: UiState::new(!settings.welcomed),
        settings,
        audio,
        updates: Updates::new(),
        presets,
        run: None,
        dirty_at: None,
        frozen: None,
        last_input: Instant::now(),
        seed: std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos() as u64).unwrap_or(7),
        preset_cursor: -1,
        autopilot: Autopilot::new(),
        cava_config_path,
        mic_requested: false,
    };
    let event_loop = EventLoop::new()?;
    event_loop.run_app(&mut app)?;
    app.settings.save();
    Ok(())
}

fn pick_format(caps: &wgpu::SurfaceCapabilities, hdr: bool) -> wgpu::TextureFormat {
    if hdr && caps.formats.contains(&wgpu::TextureFormat::Rgba16Float) {
        return wgpu::TextureFormat::Rgba16Float;
    }
    caps.formats
        .iter()
        .copied()
        .find(|f| f.is_srgb())
        .unwrap_or(caps.formats[0])
}

impl App {
    fn mark_dirty(&mut self) {
        self.dirty_at = Some(Instant::now());
    }

    fn create_running(&mut self, event_loop: &ActiveEventLoop) -> anyhow::Result<Running> {
        let attrs = Window::default_attributes()
            .with_title("Cymatica")
            .with_inner_size(winit::dpi::LogicalSize::new(1440.0, 900.0))
            .with_min_inner_size(winit::dpi::LogicalSize::new(480.0, 320.0));
        let window = Arc::new(event_loop.create_window(attrs)?);
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::new_with_display_handle_from_env(Box::new(
            event_loop.owned_display_handle(),
        )));
        let surface = instance.create_surface(window.clone())?;
        let gpu = pollster::block_on(Gpu::new(&instance, Some(&surface)))?;
        let caps = surface.get_capabilities(&gpu.adapter);
        // EDR: wgpu drives the Metal layer's extended dynamic range itself (scRGB, fp16)
        let hdr_info = surface.display_hdr_info(&gpu.adapter);
        let edr_supported = hdr_info.headroom.and_then(|h| h.potential).map(|p| p > 1.0).unwrap_or(false)
            && caps.formats.contains(&wgpu::TextureFormat::Rgba16Float);
        let hdr_active = self.settings.hdr && edr_supported;
        let format = pick_format(&caps, hdr_active);
        let size = window.inner_size();
        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format,
            width: size.width.max(1),
            height: size.height.max(1),
            present_mode: wgpu::PresentMode::AutoVsync,
            desired_maximum_frame_latency: 2,
            alpha_mode: caps.alpha_modes[0],
            view_formats: vec![],
            color_space: if hdr_active { wgpu::SurfaceColorSpace::ExtendedSrgbLinear } else { wgpu::SurfaceColorSpace::Auto },
        };
        surface.configure(&gpu.device, &config);
        let renderer = Renderer::new(&gpu, format);
        let output_mode = if format == wgpu::TextureFormat::Rgba16Float {
            OutputMode::Edr { headroom: hdr_info.headroom.and_then(|h| h.current).unwrap_or(1.0) }
        } else {
            OutputMode::for_format(format)
        };
        let egui_ctx = egui::Context::default();
        let accent = self.settings.current_theme().accent;
        ui::apply_style(&egui_ctx, accent);
        let egui_state = egui_winit::State::new(
            egui_ctx.clone(),
            egui::ViewportId::ROOT,
            &window,
            Some(window.scale_factor() as f32),
            None,
            Some(gpu.limits.max_texture_dimension_2d as usize),
        );
        let egui_renderer = egui_wgpu::Renderer::new(&gpu.device, format, egui_wgpu::RendererOptions::default());
        Ok(Running {
            window,
            _instance: instance,
            surface,
            config,
            gpu,
            renderer,
            egui_ctx,
            egui_state,
            egui_renderer,
            output_mode,
            hdr_active,
            edr_supported,
            last: Instant::now(),
            sim_time: 0.0,
            frame: 0,
            fps: 60.0,
            accent,
        })
    }

    fn apply_window_mode(&self) {
        let Some(r) = &self.run else { return };
        let mode = self.settings.window_mode;
        match mode {
            WindowMode::Fullscreen => r.window.set_fullscreen(Some(Fullscreen::Borderless(None))),
            _ => r.window.set_fullscreen(None),
        }
        macos::set_wallpaper(&r.window, mode == WindowMode::Wallpaper);
        if mode == WindowMode::Wallpaper {
            if let Some(m) = r.window.current_monitor() {
                let _ = r.window.request_inner_size(m.size());
                r.window.set_outer_position(m.position());
            }
        }
        r.window.set_window_level(if self.settings.always_on_top && mode != WindowMode::Wallpaper {
            WindowLevel::AlwaysOnTop
        } else {
            WindowLevel::Normal
        });
        r.window.set_decorations(mode != WindowMode::Wallpaper);
    }

    fn switch_scene(&mut self, delta: i64) {
        let all = scenes();
        let idx = all.iter().position(|s| s.id == self.settings.scene).unwrap_or(0) as i64;
        let next = (idx + delta).rem_euclid(all.len() as i64) as usize;
        self.set_scene(all[next].id);
    }

    fn set_scene(&mut self, id: &str) {
        if self.settings.scene != id {
            self.settings.scene = id.to_string();
            self.preset_cursor = -1;
            if let Some(info) = cymatica_engine::scene::scene_info(id) {
                self.ui.toast(info.name);
            }
            self.mark_dirty();
        }
    }

    fn cycle_theme(&mut self, delta: i64) {
        let themes = self.settings.all_themes();
        let idx = themes.iter().position(|t| t.id == self.settings.theme).unwrap_or(0) as i64;
        let next = (idx + delta).rem_euclid(themes.len() as i64) as usize;
        self.settings.theme = themes[next].id.clone();
        self.ui.toast(format!("Theme: {}", themes[next].name));
        self.mark_dirty();
    }

    fn cycle_preset(&mut self, delta: i64) {
        let scene_presets: Vec<Preset> = self.presets.iter().filter(|p| p.scene == self.settings.scene).cloned().collect();
        if scene_presets.is_empty() {
            return;
        }
        self.preset_cursor = (self.preset_cursor + delta).rem_euclid(scene_presets.len() as i64);
        let p = scene_presets[self.preset_cursor as usize].clone();
        self.load_preset(&p);
    }

    fn load_preset(&mut self, p: &Preset) {
        self.settings.scene = p.scene.clone();
        *self.settings.params_for(&p.scene) = p.params.clone();
        if let Some(t) = &p.theme {
            if self.settings.theme_by_id(t).is_some() {
                self.settings.theme = t.clone();
            }
        }
        self.settings.look = p.look.clone().unwrap_or_default();
        self.ui.toast(p.name.clone());
        self.mark_dirty();
    }

    fn randomize(&mut self, amount: f32) {
        self.seed = self.seed.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        let scene = self.settings.scene.clone();
        presets::randomize(&scene, self.settings.params_for(&scene), self.seed, amount);
        if amount >= 0.99 {
            let themes = self.settings.all_themes();
            let i = (self.seed >> 33) as usize % themes.len();
            self.settings.theme = themes[i].id.clone();
        }
        self.ui.toast(if amount >= 0.99 { "Randomized" } else { "Mutated" });
        self.mark_dirty();
    }

    fn import_cava(&mut self) {
        let Some(path) = self.cava_config_path.clone() else { return };
        match cymatica_audio::cava_config::load(&path) {
            Ok(cfg) => {
                self.settings.cava = cfg.settings.clone();
                if let Some(src) = cfg.source.clone() {
                    self.settings.input = cymatica_audio::InputChoice::Device(src);
                    self.audio.set_input(self.settings.input.clone());
                }
                self.audio.set_cava(self.settings.cava.clone());
                if let Some(theme) = cymatica_engine::theme::theme_from_cava(&cfg.colors) {
                    self.settings.custom_themes.retain(|t| t.id != theme.id);
                    self.settings.theme = theme.id.clone();
                    self.settings.custom_themes.push(theme);
                }
                self.ui.toast(format!("Imported {} settings from cava", cfg.imported_keys.len()));
                self.mark_dirty();
            }
            Err(e) => self.ui.toast(format!("Couldn't read cava's config: {e}")),
        }
    }

    fn screenshot(&mut self, scale: f32) {
        let Some(r) = &self.run else { return };
        let (w, h) = (r.config.width as f32, r.config.height as f32);
        let (sw, sh) = if scale < 0.0 {
            let target_w = -scale;
            (target_w, (target_w * h / w).round())
        } else {
            (w * scale, h * scale)
        };
        let size = ((sw as u32).max(16), (sh as u32).max(16));
        let scene = self.settings.scene.clone();
        let params = self.settings.scene_params.get(&scene).cloned().unwrap_or_default();
        let look = self.settings.look.clone();
        let theme = self.settings.current_theme();
        let audio = self.audio.frame_at(Duration::from_millis(self.settings.av_offset_ms as u64));
        let time = r.sim_time;
        let dir = crate::paths::screenshots_dir();
        // render on a separate GPU context so the live view isn't disturbed
        std::thread::spawn(move || {
            let result = (|| -> anyhow::Result<std::path::PathBuf> {
                let mut off = OfflineRenderer::new(size)?;
                let mut img = None;
                // a few frames so simulations have something to show
                for i in 0..90u64 {
                    let info = FrameInfo::new(time + i as f32 / 60.0, 1.0 / 60.0, i, audio.clone());
                    img = Some(off.render(&scene, &params, &look, &theme, info, 1.0)?);
                }
                std::fs::create_dir_all(&dir)?;
                let stamp = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH)?.as_secs();
                let path = dir.join(format!("Cymatica-{scene}-{stamp}.png"));
                img.unwrap().save(&path)?;
                Ok(path)
            })();
            match result {
                Ok(p) => log::info!("screenshot saved to {}", p.display()),
                Err(e) => log::warn!("screenshot failed: {e:#}"),
            }
        });
        self.ui.toast(format!("Screenshot {}×{} saved to Pictures/Cymatica", size.0, size.1));
    }

    fn handle_key(&mut self, event_loop: &ActiveEventLoop, key: &Key, shift: bool) {
        match key {
            Key::Named(NamedKey::ArrowRight) => self.switch_scene(1),
            Key::Named(NamedKey::ArrowLeft) => self.switch_scene(-1),
            Key::Named(NamedKey::ArrowDown) => self.cycle_preset(1),
            Key::Named(NamedKey::ArrowUp) => self.cycle_preset(-1),
            Key::Named(NamedKey::Tab) => {
                self.settings.panel_open = !self.settings.panel_open;
                self.mark_dirty();
            }
            Key::Named(NamedKey::Space) => {
                self.frozen = if self.frozen.is_some() {
                    None
                } else {
                    Some(self.audio.frame_at(Duration::ZERO))
                };
            }
            Key::Named(NamedKey::Escape) => {
                if self.settings.window_mode != WindowMode::Window {
                    self.settings.window_mode = WindowMode::Window;
                    self.apply_window_mode();
                    self.mark_dirty();
                }
            }
            Key::Character(c) => match c.to_lowercase().as_str() {
                "h" => {
                    self.settings.panel_open = !self.settings.panel_open;
                    self.mark_dirty();
                }
                "f" => {
                    self.settings.window_mode = if self.settings.window_mode == WindowMode::Fullscreen {
                        WindowMode::Window
                    } else {
                        WindowMode::Fullscreen
                    };
                    self.apply_window_mode();
                    self.mark_dirty();
                }
                "t" => self.cycle_theme(if shift { -1 } else { 1 }),
                "r" => self.randomize(1.0),
                "m" => self.randomize(0.25),
                "i" => {
                    self.settings.show_hud = !self.settings.show_hud;
                    self.mark_dirty();
                }
                "d" => {
                    self.settings.autopilot.enabled = !self.settings.autopilot.enabled;
                    self.ui.toast(if self.settings.autopilot.enabled { "Auto-pilot on" } else { "Auto-pilot off" });
                    self.mark_dirty();
                }
                "s" => self.screenshot(1.0),
                "q" if cfg!(not(target_os = "macos")) => event_loop.exit(),
                d if d.len() == 1 && d.chars().all(|ch| ch.is_ascii_digit()) => {
                    let n = d.parse::<usize>().unwrap_or(0);
                    if n >= 1 {
                        if let Some(sc) = scenes().get(n - 1) {
                            self.set_scene(sc.id);
                        }
                    }
                }
                _ => {}
            },
            _ => {}
        }
    }

    fn quit(&mut self, event_loop: &ActiveEventLoop) {
        self.settings.save();
        // a downloaded update installs on quit, so the next launch is the new version
        if matches!(self.updates.state(), UpdateState::Ready { .. }) {
            self.updates.install_ready(false);
        }
        event_loop.exit();
    }

    fn frame(&mut self, event_loop: &ActiveEventLoop) {
        let Some(r) = self.run.as_mut() else { return };
        let now = Instant::now();
        let mut dt = (now - r.last).as_secs_f32().min(0.1);
        r.last = now;
        if dt > 0.0 {
            r.fps = r.fps * 0.95 + (1.0 / dt) * 0.05;
        }
        let audio = match &self.frozen {
            Some(f) => {
                dt = 0.0;
                f.clone()
            }
            None => self.audio.frame_at(Duration::from_micros((self.settings.av_offset_ms * 1000.0) as u64)),
        };
        r.sim_time += dt;
        r.frame += 1;
        self.updates.tick();

        // auto-pilot
        if let Some(change) = self.autopilot.tick(&self.settings.autopilot, &audio, dt) {
            let r_seed = self.seed;
            self.seed = self.seed.wrapping_mul(6364136223846793005).wrapping_add(1);
            crate::autopilot::apply(change, &mut self.settings, &self.presets, r_seed);
            self.dirty_at = Some(Instant::now());
        }

        // keep the UI accent in sync with the theme
        let theme = self.settings.current_theme();
        if theme.accent != r.accent {
            r.accent = theme.accent;
            ui::apply_style(&r.egui_ctx, theme.accent);
        }

        // ---- UI
        let status = self.audio.status();
        let window_size = (r.config.width, r.config.height);
        let render_size = (
            ((r.config.width as f32 * self.settings.render_scale).round() as u32).max(16),
            ((r.config.height as f32 * self.settings.render_scale).round() as u32).max(16),
        );
        let gpu_name = r.gpu.adapter_info.name.clone();
        let info = UiInfo {
            audio: &audio,
            audio_status: &status,
            fps: r.fps,
            render_size,
            window_size,
            gpu_name: &gpu_name,
            update: self.updates.state(),
            auto_update: self.updates.auto_update,
            can_roll_back: self.updates.can_roll_back(),
            version: updates::VERSION,
            build: updates::build_number(),
            presets: &self.presets,
            hdr_available: r.edr_supported,
            cava_config_found: self.cava_config_path.is_some(),
            is_macos: cfg!(target_os = "macos"),
            frozen: self.frozen.is_some(),
        };
        let raw = r.egui_state.take_egui_input(&r.window);
        let mut actions = ui::UiActions::default();
        let settings = &mut self.settings;
        let ui_state = &mut self.ui;
        let full = r.egui_ctx.run_pass(raw, |ctx| {
            ui::draw_hud(ctx, settings, &info);
            actions = ui::draw(ctx, ui_state, settings, &info);
        });
        r.egui_state.handle_platform_output(&r.window, full.platform_output);
        let paint_jobs = r.egui_ctx.tessellate(full.shapes, full.pixels_per_point);
        // egui requires every texture delta to be consumed, even if this frame can't be shown
        let mut textures = full.textures_delta;
        for (id, deltas) in textures.set.drain() {
            for delta in deltas.iter() {
                r.egui_renderer.update_texture(&r.gpu.device, &r.gpu.queue, id, delta);
            }
        }
        let to_free: Vec<egui::TextureId> = textures.free.drain().collect();

        // ---- render
        let output = match r.surface.get_current_texture() {
            wgpu::CurrentSurfaceTexture::Success(t) | wgpu::CurrentSurfaceTexture::Suboptimal(t) => Some(t),
            wgpu::CurrentSurfaceTexture::Outdated | wgpu::CurrentSurfaceTexture::Lost => {
                r.surface.configure(&r.gpu.device, &r.config);
                None
            }
            _ => None,
        };
        if let Some(output) = output {
            let view = output.texture.create_view(&Default::default());
            let mut enc = r.gpu.device.create_command_encoder(&Default::default());
            if let (OutputMode::Edr { .. }, true) = (r.output_mode, r.frame % 30 == 0) {
                let h = r.surface.display_hdr_info(&r.gpu.adapter).headroom.and_then(|h| h.current).unwrap_or(1.0);
                r.output_mode = OutputMode::Edr { headroom: h.max(1.0) };
            }
            let scene = self.settings.scene.clone();
            let empty = ParamSet::default();
            let params = self.settings.scene_params.get(&scene).unwrap_or(&empty);
            let mut fi = FrameInfo::new(r.sim_time, dt, r.frame, audio.clone());
            fi.seed = (r.frame % 1000) as f32;
            r.renderer.render(
                &r.gpu,
                &mut enc,
                RenderRequest {
                    scene: &scene,
                    scene_params: params,
                    look: &self.settings.look,
                    theme: &theme,
                    info: fi,
                    render_size,
                    output: &view,
                    output_mode: r.output_mode,
                },
            );
            // UI on top
            let screen = egui_wgpu::ScreenDescriptor {
                size_in_pixels: [r.config.width, r.config.height],
                pixels_per_point: full.pixels_per_point,
            };
            r.egui_renderer.update_buffers(&r.gpu.device, &r.gpu.queue, &mut enc, &paint_jobs, &screen);
            {
                let rp = cymatica_engine::gpu::pass(&mut enc, "ui", &view, None);
                let mut rp = rp.forget_lifetime();
                r.egui_renderer.render(&mut rp, &paint_jobs, &screen);
            }
            r.gpu.queue.submit([enc.finish()]);
            r.window.pre_present_notify();
            r.gpu.queue.present(output);
        }
        for id in &to_free {
            r.egui_renderer.free_texture(id);
        }

        // cursor auto-hide when the panel is closed and the mouse rests
        let idle = self.last_input.elapsed() > Duration::from_secs(3);
        r.window.set_cursor_visible(!(idle && !self.settings.panel_open));

        self.handle_actions(actions, event_loop);

        // first launch: ask for microphone access up front (BlackHole counts as a microphone)
        if !self.mic_requested && cfg!(target_os = "macos") {
            self.mic_requested = true;
            if macos::mic_permission() == macos::Mic::NotDetermined {
                macos::request_mic_permission();
            }
        }

        if let Some(t) = self.dirty_at {
            if t.elapsed() > Duration::from_millis(800) {
                self.settings.save();
                self.dirty_at = None;
            }
        }
    }

    fn handle_actions(&mut self, a: ui::UiActions, event_loop: &ActiveEventLoop) {
        if a.changed {
            self.mark_dirty();
        }
        if a.scene_switched {
            self.preset_cursor = -1;
        }
        if a.restart_scene {
            if let Some(r) = self.run.as_mut() {
                let scene = self.settings.scene.clone();
                r.renderer.reset_scene(&r.gpu, &scene);
            }
        }
        if a.input_changed {
            self.audio.set_input(self.settings.input.clone());
        }
        if a.cava_changed {
            self.audio.set_cava(self.settings.cava.clone());
        }
        if a.window_changed {
            self.apply_window_mode();
            let hdr_now = self.run.as_ref().map(|r| r.hdr_active).unwrap_or(false);
            let want = self.settings.hdr && self.run.as_ref().map(|r| r.edr_supported).unwrap_or(false);
            if hdr_now != want {
                self.ui.toast("HDR changes apply the next time Cymatica starts");
            }
        }
        if let Some(p) = a.load_preset {
            self.load_preset(&p);
        }
        if let Some(name) = a.save_preset {
            let scene = self.settings.scene.clone();
            let p = Preset {
                name: name.clone(),
                scene: scene.clone(),
                theme: Some(self.settings.theme.clone()),
                params: self.settings.scene_params.get(&scene).cloned().unwrap_or_default(),
                look: Some(self.settings.look.clone()),
                builtin: false,
            };
            match presets::save_user_preset(&p) {
                Ok(_) => {
                    self.presets.retain(|q| q.key() != p.key() || q.builtin);
                    self.presets.push(p);
                    self.ui.toast(format!("Saved preset {name}"));
                }
                Err(e) => self.ui.toast(format!("Couldn't save the preset: {e}")),
            }
        }
        if let Some(p) = a.delete_preset {
            presets::delete_user_preset(&p);
            self.presets.retain(|q| q.builtin || q.key() != p.key());
        }
        if let Some(amount) = a.randomize {
            self.randomize(amount);
        }
        if let Some(scale) = a.screenshot {
            self.screenshot(scale);
        }
        if a.check_updates {
            self.updates.check(true);
        }
        if a.download_update {
            self.updates.download();
        }
        if let Some(on) = a.set_auto_update {
            self.updates.set_auto_update(on);
        }
        if a.install_update {
            self.settings.save();
            if self.updates.install_ready(true) {
                event_loop.exit();
            }
        }
        if a.roll_back {
            self.settings.save();
            if self.updates.roll_back() {
                event_loop.exit();
            }
        }
        if a.import_cava {
            self.import_cava();
        }
        if a.open_presets_folder {
            let dir = crate::paths::presets_dir();
            let _ = std::fs::create_dir_all(&dir);
            macos::open(&dir.display().to_string());
        }
        if a.open_mic_settings {
            macos::open_microphone_settings();
        }
        if let Some(url) = a.open_url {
            macos::open(&url);
        }
    }
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.run.is_some() {
            return;
        }
        match self.create_running(event_loop) {
            Ok(r) => {
                self.run = Some(r);
                self.apply_window_mode();
            }
            Err(e) => {
                log::error!("could not start: {e:#}");
                eprintln!("Cymatica could not start the GPU: {e:#}");
                event_loop.exit();
            }
        }
    }

    fn window_event(&mut self, event_loop: &ActiveEventLoop, _id: WindowId, event: WindowEvent) {
        let Some(r) = self.run.as_mut() else { return };
        let response = r.egui_state.on_window_event(&r.window, &event);
        match &event {
            WindowEvent::CloseRequested => self.quit(event_loop),
            WindowEvent::Resized(size) => {
                r.config.width = size.width.max(1);
                r.config.height = size.height.max(1);
                r.surface.configure(&r.gpu.device, &r.config);
            }
            WindowEvent::RedrawRequested => self.frame(event_loop),
            WindowEvent::CursorMoved { .. } | WindowEvent::MouseInput { .. } => {
                self.last_input = Instant::now();
            }
            WindowEvent::KeyboardInput { event: key_event, .. } => {
                self.last_input = Instant::now();
                let wants_kb = r.egui_ctx.egui_wants_keyboard_input();
                if key_event.state == ElementState::Pressed && !key_event.repeat && !wants_kb && !response.consumed {
                    let shift = r.egui_ctx.input(|i| i.modifiers.shift);
                    let key = key_event.logical_key.clone();
                    self.handle_key(event_loop, &key, shift);
                }
            }
            _ => {}
        }
    }

    fn about_to_wait(&mut self, _event_loop: &ActiveEventLoop) {
        if let Some(r) = &self.run {
            r.window.request_redraw();
        }
    }
}
