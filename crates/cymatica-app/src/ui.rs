//! The overlay UI (egui): scene picker, every setting, themes, audio, presets, display, updates.

use crate::presets::Preset;
use crate::settings::{Settings, WindowMode};
use crate::updates::UpdateState;
use cymatica_audio::analysis::{AudioFrame, BAND_NAMES, NOTE_NAMES};
use cymatica_audio::{AudioStatus, ChannelLayout, InputChoice, SourceKind};
use cymatica_engine::params::{ModSource, Modulation, ParamDef, ParamKind, ParamSet, ParamValue};
use cymatica_engine::post::LOOK_PARAMS;
use cymatica_engine::scene::scenes;
use cymatica_engine::theme::Theme;
use egui::{Align2, Color32, CornerRadius, RichText, Sense, Stroke, Vec2};
use std::time::{Duration, Instant};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Tab {
    Scene,
    Look,
    Audio,
    Presets,
    Display,
    About,
}

pub struct UiState {
    pub tab: Tab,
    pub preset_name: String,
    pub toasts: Vec<(String, Instant)>,
    pub editing_mod: Option<String>,
    pub show_welcome: bool,
}

impl UiState {
    pub fn new(show_welcome: bool) -> Self {
        Self { tab: Tab::Scene, preset_name: String::new(), toasts: Vec::new(), editing_mod: None, show_welcome }
    }

    pub fn toast(&mut self, msg: impl Into<String>) {
        self.toasts.push((msg.into(), Instant::now()));
    }
}

/// What the UI needs to show.
pub struct UiInfo<'a> {
    pub audio: &'a AudioFrame,
    pub audio_status: &'a AudioStatus,
    pub fps: f32,
    pub render_size: (u32, u32),
    pub window_size: (u32, u32),
    pub gpu_name: &'a str,
    pub update: UpdateState,
    pub auto_update: bool,
    pub can_roll_back: bool,
    pub version: &'a str,
    pub build: u64,
    pub presets: &'a [Preset],
    pub hdr_available: bool,
    pub cava_config_found: bool,
    pub is_macos: bool,
    pub frozen: bool,
    pub thumbnails: &'a crate::thumbnails::Thumbnails,
}

/// What the app should do after the UI ran.
#[derive(Default)]
pub struct UiActions {
    pub changed: bool,
    pub scene_switched: bool,
    pub restart_scene: bool,
    pub input_changed: bool,
    pub cava_changed: bool,
    pub window_changed: bool,
    pub load_preset: Option<Preset>,
    pub save_preset: Option<String>,
    pub delete_preset: Option<Preset>,
    pub randomize: Option<f32>,
    pub screenshot: Option<f32>,
    pub check_updates: bool,
    pub download_update: bool,
    pub install_update: bool,
    pub roll_back: bool,
    pub set_auto_update: Option<bool>,
    pub import_cava: bool,
    pub open_presets_folder: bool,
    pub open_mic_settings: bool,
    pub open_url: Option<String>,
}

pub fn apply_style(ctx: &egui::Context, accent: [u8; 3]) {
    let accent = Color32::from_rgb(accent[0], accent[1], accent[2]);
    ctx.all_styles_mut(|style| {
        let v = &mut style.visuals;
        *v = egui::Visuals::dark();
        v.panel_fill = Color32::from_rgba_unmultiplied(14, 14, 22, 215);
        v.window_fill = Color32::from_rgba_unmultiplied(14, 14, 22, 235);
        v.window_stroke = Stroke::new(1.0, Color32::from_rgba_unmultiplied(255, 255, 255, 22));
        v.window_corner_radius = CornerRadius::same(14);
        v.selection.bg_fill = accent.linear_multiply(0.55);
        // selected text stays white on the accent background
        v.selection.stroke = Stroke::new(1.0, Color32::WHITE);
        v.hyperlink_color = accent;
        v.widgets.inactive.corner_radius = CornerRadius::same(6);
        v.widgets.hovered.corner_radius = CornerRadius::same(6);
        v.widgets.active.corner_radius = CornerRadius::same(6);
        v.widgets.active.bg_fill = accent.linear_multiply(0.7);
        v.slider_trailing_fill = true;
        style.spacing.slider_width = 150.0;
        style.spacing.item_spacing = Vec2::new(8.0, 6.0);
    });
}

fn color32(c: [f32; 3]) -> Color32 {
    let to8 = |x: f32| (cymatica_engine::theme::linear_to_srgb(x.clamp(0.0, 1.0)) * 255.0) as u8;
    Color32::from_rgb(to8(c[0]), to8(c[1]), to8(c[2]))
}

/// The whole UI for one frame.
pub fn draw(ctx: &egui::Context, st: &mut UiState, s: &mut Settings, info: &UiInfo) -> UiActions {
    let mut act = UiActions::default();
    draw_toasts(ctx, st, s, info, &mut act);
    if st.show_welcome {
        draw_welcome(ctx, st, s, info, &mut act);
    }
    if !s.panel_open {
        return act;
    }
    let screen = ctx.content_rect();
    let width = 372.0f32.min(screen.width() - 24.0);
    egui::Area::new(egui::Id::unique("cymatica-panel")).anchor(Align2::RIGHT_TOP, [-12.0, 12.0]).show(ctx, |ui| {
        egui::Frame::new()
            .fill(ui.visuals().panel_fill)
            .corner_radius(CornerRadius::same(16))
            .stroke(Stroke::new(1.0, Color32::from_rgba_unmultiplied(255, 255, 255, 20)))
            .inner_margin(egui::Margin::same(14))
            .show(ui, |ui| {
                ui.set_width(width);
                ui.set_max_height(screen.height() - 52.0);
                header(ui, s, info);
                ui.add_space(6.0);
                tabs(ui, st);
                ui.separator();
                egui::ScrollArea::vertical().auto_shrink([false, true]).show(ui, |ui| match st.tab {
                    Tab::Scene => scene_tab(ui, st, s, info, &mut act),
                    Tab::Look => look_tab(ui, st, s, &mut act),
                    Tab::Audio => audio_tab(ui, s, info, &mut act),
                    Tab::Presets => presets_tab(ui, st, s, info, &mut act),
                    Tab::Display => display_tab(ui, s, info, &mut act),
                    Tab::About => about_tab(ui, info, &mut act),
                });
                ui.separator();
                ui.label(
                    RichText::new("H hide · Left/Right scenes · Up/Down presets · T theme · R random · F fullscreen")
                        .small()
                        .weak(),
                );
            });
    });
    act
}

fn header(ui: &mut egui::Ui, s: &Settings, info: &UiInfo) {
    ui.horizontal(|ui| {
        ui.label(RichText::new("CYMATICA").size(20.0).strong().extra_letter_spacing(3.0));
        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
            let (dot, text) = match info.audio_status.kind {
                SourceKind::Live if info.audio.silent => (Color32::from_rgb(230, 180, 60), "silent"),
                SourceKind::Live => (Color32::from_rgb(80, 220, 120), "live"),
                SourceKind::Demo => (Color32::from_rgb(120, 160, 255), "demo"),
                SourceKind::Off => (Color32::GRAY, "off"),
            };
            ui.label(RichText::new(format!("{} · {:.0} fps", text, info.fps)).small().weak());
            let (r, _) = ui.allocate_exact_size(Vec2::splat(8.0), Sense::hover());
            ui.painter().circle_filled(r.center(), 4.0, dot);
            let _ = s;
        });
    });
}

fn tabs(ui: &mut egui::Ui, st: &mut UiState) {
    ui.horizontal_wrapped(|ui| {
        for (t, label) in [
            (Tab::Scene, "Scene"),
            (Tab::Look, "Look"),
            (Tab::Audio, "Audio"),
            (Tab::Presets, "Presets"),
            (Tab::Display, "Display"),
            (Tab::About, "Updates"),
        ] {
            if ui.selectable_label(st.tab == t, label).clicked() {
                st.tab = t;
            }
        }
    });
}

// ---------------------------------------------------------------- scene

fn scene_tab(ui: &mut egui::Ui, st: &mut UiState, s: &mut Settings, info: &UiInfo, act: &mut UiActions) {
    let _ = info;
    egui::Grid::new("scenes").num_columns(3).spacing([6.0, 6.0]).show(ui, |ui| {
        for (i, sc) in scenes().iter().enumerate() {
            let selected = s.scene == sc.id;
            let text = RichText::new(sc.name).strong();
            let b = egui::Button::new(text).selected(selected).min_size(Vec2::new(110.0, 30.0));
            if ui.add(b).on_hover_text(sc.tagline).clicked() && !selected {
                s.scene = sc.id.to_string();
                act.scene_switched = true;
                act.changed = true;
            }
            if i % 3 == 2 {
                ui.end_row();
            }
        }
    });
    ui.add_space(4.0);
    if let Some(info) = cymatica_engine::scene::scene_info(&s.scene) {
        ui.label(RichText::new(info.tagline).italics().weak());
        ui.horizontal(|ui| {
            if ui.button("Randomize").on_hover_text("New random settings (R)").clicked() {
                act.randomize = Some(1.0);
            }
            if ui.button("Mutate").on_hover_text("Small random changes (M)").clicked() {
                act.randomize = Some(0.25);
            }
            if ui.button("Reset").on_hover_text("Back to this scene's defaults").clicked() {
                s.params_for(info.id).values.clear();
                s.params_for(info.id).mods.clear();
                act.changed = true;
            }
            if ui.button("Restart").on_hover_text("Restart the simulation").clicked() {
                act.restart_scene = true;
            }
        });
        ui.add_space(4.0);
        let scene_id = info.id.to_string();
        let set = s.params_for(&scene_id);
        if param_editor(ui, st, info.params, set, &format!("scene-{scene_id}")) {
            act.changed = true;
        }
    }
}

/// Generic editor for a parameter list, grouped. Returns true when something changed.
fn param_editor(
    ui: &mut egui::Ui,
    st: &mut UiState,
    defs: &'static [ParamDef],
    set: &mut ParamSet,
    salt: &str,
) -> bool {
    let mut changed = false;
    let mut groups: Vec<&str> = Vec::new();
    for d in defs {
        if !groups.contains(&d.group) {
            groups.push(d.group);
        }
    }
    for g in groups {
        egui::CollapsingHeader::new(RichText::new(g).strong()).id_salt(format!("{salt}-{g}")).default_open(true).show(
            ui,
            |ui| {
                for d in defs.iter().filter(|d| d.group == g) {
                    changed |= param_row(ui, st, defs, d, set, salt);
                }
            },
        );
    }
    changed
}

fn param_row(
    ui: &mut egui::Ui,
    st: &mut UiState,
    defs: &'static [ParamDef],
    d: &ParamDef,
    set: &mut ParamSet,
    salt: &str,
) -> bool {
    let mut changed = false;
    let key = format!("{salt}/{}", d.id);
    ui.horizontal(|ui| {
        ui.add_sized([118.0, 18.0], egui::Label::new(d.label).truncate()).on_hover_text(d.help);
        match d.kind {
            ParamKind::Float { min, max, .. } => {
                let mut v = set.number(defs, d.id);
                if ui.add(egui::Slider::new(&mut v, min..=max).show_value(true)).on_hover_text(d.help).changed() {
                    set.set(defs, d.id, ParamValue::Number(v as f64));
                    changed = true;
                }
            }
            ParamKind::Int { min, max, .. } => {
                let mut v = set.number(defs, d.id).round() as i32;
                if ui.add(egui::Slider::new(&mut v, min..=max)).on_hover_text(d.help).changed() {
                    set.set(defs, d.id, ParamValue::Number(v as f64));
                    changed = true;
                }
            }
            ParamKind::Toggle { .. } => {
                let mut b = matches!(set.get(defs, d.id), ParamValue::Bool(true));
                if ui.checkbox(&mut b, "").on_hover_text(d.help).changed() {
                    set.set(defs, d.id, ParamValue::Bool(b));
                    changed = true;
                }
            }
            ParamKind::Choice { options, .. } => {
                let cur = match set.get(defs, d.id) {
                    ParamValue::Text(t) => t,
                    _ => String::new(),
                };
                let label = options.iter().find(|(id, _)| *id == cur).map(|o| o.1).unwrap_or("?");
                egui::ComboBox::from_id_salt(&key).selected_text(label).width(160.0).show_ui(ui, |ui| {
                    for (id, l) in options {
                        if ui.selectable_label(cur == *id, *l).clicked() && cur != *id {
                            set.set(defs, d.id, ParamValue::Text(id.to_string()));
                            changed = true;
                        }
                    }
                });
            }
            ParamKind::Color { .. } => {
                let mut c = match set.get(defs, d.id) {
                    ParamValue::Color(c) => c,
                    _ => [255, 255, 255],
                };
                if ui.color_edit_button_srgb(&mut c).changed() {
                    set.set(defs, d.id, ParamValue::Color(c));
                    changed = true;
                }
            }
        }
        if d.is_numeric() {
            let modulated = set.mods.contains_key(d.id);
            let b = egui::Button::new(RichText::new("~").strong().color(if modulated {
                ui.visuals().selection.stroke.color
            } else {
                ui.visuals().weak_text_color()
            }))
            .small()
            .frame(false);
            if ui.add(b).on_hover_text("Link this setting to the music").clicked() {
                st.editing_mod = if st.editing_mod.as_deref() == Some(key.as_str()) { None } else { Some(key.clone()) };
            }
        }
    });
    if st.editing_mod.as_deref() == Some(key.as_str()) {
        ui.indent(&key, |ui| {
            let current = set.mods.get(d.id).copied();
            ui.horizontal(|ui| {
                ui.label("Follows");
                let label = current.map(|m| m.source.label()).unwrap_or("nothing");
                egui::ComboBox::from_id_salt(format!("{key}-src")).selected_text(label).width(150.0).show_ui(
                    ui,
                    |ui| {
                        if ui.selectable_label(current.is_none(), "nothing").clicked() {
                            set.mods.remove(d.id);
                            changed = true;
                        }
                        for src in ModSource::ALL {
                            if ui.selectable_label(current.map(|m| m.source) == Some(src), src.label()).clicked() {
                                let amount = current.map(|m| m.amount).unwrap_or(0.3);
                                set.mods.insert(d.id.to_string(), Modulation { source: src, amount });
                                changed = true;
                            }
                        }
                    },
                );
            });
            if let Some(mut m) = set.mods.get(d.id).copied() {
                ui.horizontal(|ui| {
                    ui.label("Amount");
                    if ui.add(egui::Slider::new(&mut m.amount, -1.0..=1.0)).changed() {
                        set.mods.insert(d.id.to_string(), m);
                        changed = true;
                    }
                });
            }
        });
    }
    changed
}

// ---------------------------------------------------------------- look

fn theme_swatch(ui: &mut egui::Ui, t: &Theme, selected: bool) -> egui::Response {
    let (rect, resp) = ui.allocate_exact_size(Vec2::new(104.0, 34.0), Sense::click());
    let painter = ui.painter_at(rect);
    let bg = color32(t.background_linear()[1]);
    painter.rect_filled(rect, 6.0, bg);
    let pal = t.palette();
    let inner = rect.shrink2(Vec2::new(6.0, 13.0)).translate(Vec2::new(0.0, -5.0));
    let n = 24;
    for i in 0..n {
        let c = pal[i * (pal.len() - 1) / (n - 1)];
        let x0 = inner.left() + inner.width() * i as f32 / n as f32;
        let x1 = inner.left() + inner.width() * (i + 1) as f32 / n as f32 + 0.5;
        painter.rect_filled(
            egui::Rect::from_min_max(egui::pos2(x0, inner.top()), egui::pos2(x1, inner.bottom())),
            0.0,
            color32(c),
        );
    }
    painter.text(
        egui::pos2(rect.center().x, rect.bottom() - 7.0),
        Align2::CENTER_CENTER,
        &t.name,
        egui::FontId::proportional(10.5),
        Color32::from_white_alpha(220),
    );
    let stroke = if selected {
        Stroke::new(2.0, ui.visuals().selection.stroke.color)
    } else if resp.hovered() {
        Stroke::new(1.0, Color32::from_white_alpha(120))
    } else {
        Stroke::new(1.0, Color32::from_white_alpha(25))
    };
    painter.rect_stroke(rect, 6.0, stroke, egui::StrokeKind::Inside);
    resp.on_hover_text(&t.name)
}

fn look_tab(ui: &mut egui::Ui, st: &mut UiState, s: &mut Settings, act: &mut UiActions) {
    ui.label(RichText::new("Theme").strong());
    let themes = s.all_themes();
    egui::Grid::new("themes").num_columns(3).spacing([6.0, 6.0]).show(ui, |ui| {
        for (i, t) in themes.iter().enumerate() {
            if theme_swatch(ui, t, s.theme == t.id).clicked() {
                s.theme = t.id.clone();
                act.changed = true;
            }
            if i % 3 == 2 {
                ui.end_row();
            }
        }
    });
    ui.add_space(6.0);
    if param_editor(ui, st, LOOK_PARAMS, &mut s.look, "look") {
        act.changed = true;
    }
    if ui.button("Reset look").clicked() {
        s.look = ParamSet::default();
        act.changed = true;
    }
}

// ---------------------------------------------------------------- audio

fn meter_bars(ui: &mut egui::Ui, values: &[f32], height: f32, color: impl Fn(usize, f32) -> Color32) {
    let width = ui.available_width();
    let (rect, _) = ui.allocate_exact_size(Vec2::new(width, height), Sense::hover());
    let painter = ui.painter_at(rect);
    painter.rect_filled(rect, 4.0, Color32::from_black_alpha(90));
    let n = values.len().max(1);
    let w = rect.width() / n as f32;
    for (i, &v) in values.iter().enumerate() {
        let h = v.clamp(0.0, 1.0) * (rect.height() - 2.0);
        let x = rect.left() + i as f32 * w;
        let r = egui::Rect::from_min_max(
            egui::pos2(x + w * 0.12, rect.bottom() - h - 1.0),
            egui::pos2(x + w * 0.88, rect.bottom() - 1.0),
        );
        painter.rect_filled(r, 1.0, color(i, v));
    }
}

fn audio_tab(ui: &mut egui::Ui, s: &mut Settings, info: &UiInfo, act: &mut UiActions) {
    let st = info.audio_status;
    ui.label(RichText::new("Input").strong());
    let current = match &s.input {
        InputChoice::Auto => "Automatic (BlackHole 2ch)".to_string(),
        InputChoice::Device(n) => n.clone(),
        InputChoice::Demo => "Demo track".into(),
        InputChoice::Off => "Off".into(),
    };
    egui::ComboBox::from_id_salt("input").selected_text(current).width(300.0).show_ui(ui, |ui| {
        let mut pick = |ui: &mut egui::Ui, choice: InputChoice, label: String| {
            if ui.selectable_label(s.input == choice, label).clicked() && s.input != choice {
                s.input = choice;
                act.input_changed = true;
                act.changed = true;
            }
        };
        pick(ui, InputChoice::Auto, "Automatic (BlackHole 2ch, like cava)".into());
        for name in &st.inputs {
            pick(ui, InputChoice::Device(name.clone()), name.clone());
        }
        pick(ui, InputChoice::Demo, "Demo track".into());
        pick(ui, InputChoice::Off, "Off".into());
    });
    match st.kind {
        SourceKind::Live => {
            ui.label(format!("Listening to {} at {} Hz", st.label, st.sample_rate));
        }
        SourceKind::Demo => {
            if s.input == InputChoice::Auto && !st.blackhole_found {
                ui.colored_label(
                    Color32::from_rgb(240, 190, 80),
                    "BlackHole 2ch isn't installed, so the demo track is playing.",
                );
                if ui.link("How to set up BlackHole (like cava)").clicked() {
                    act.open_url =
                        Some("https://github.com/ExistentialAudio/BlackHole/wiki/Multi-Output-Device".into());
                }
            } else {
                ui.label("Playing the built-in demo track.");
            }
        }
        SourceKind::Off => {
            ui.label("No input.");
        }
    }
    if let Some(e) = &st.error {
        ui.colored_label(Color32::from_rgb(240, 120, 100), e);
    }
    if st.kind == SourceKind::Live && st.silent_seconds > 8.0 {
        ui.colored_label(
            Color32::from_rgb(240, 190, 80),
            "No sound is reaching Cymatica. Is your Mac's output set to the Multi-Output Device that includes BlackHole?",
        );
        if info.is_macos && ui.link("Check microphone permission").clicked() {
            act.open_mic_settings = true;
        }
    }

    ui.add_space(6.0);
    let a = info.audio;
    meter_bars(ui, &a.bars, 56.0, |i, _| {
        let t = i as f32 / a.bars.len().max(1) as f32;
        Color32::from_rgb((80.0 + 175.0 * t) as u8, 120, (255.0 - 120.0 * t) as u8)
    });
    ui.horizontal(|ui| {
        for (i, name) in ["Kick", "Snare", "Hat"].iter().enumerate() {
            let v = a.onset[i];
            let (r, _) = ui.allocate_exact_size(Vec2::splat(12.0), Sense::hover());
            ui.painter().circle_filled(
                r.center(),
                5.5,
                Color32::from_rgb(40, 40, 50).lerp_to_gamma(Color32::from_rgb(255, 120, 200), v),
            );
            ui.label(RichText::new(*name).small());
        }
        ui.label(
            RichText::new(format!("{:.0} BPM · {} · {:.0} dB", a.bpm, NOTE_NAMES[a.note as usize % 12], a.rms_db))
                .small(),
        );
    });
    meter_bars(ui, &a.bands, 28.0, |_, v| Color32::from_rgb(90, (140.0 + 100.0 * v) as u8, 255));
    ui.label(RichText::new(BAND_NAMES.join(" · ")).small().weak());

    ui.add_space(6.0);
    egui::CollapsingHeader::new(RichText::new("cava settings").strong()).default_open(true).show(ui, |ui| {
        let c = &mut s.cava;
        let mut ch = false;
        ui.horizontal(|ui| {
            ui.label("Bars");
            let mut b = c.bars as u32;
            if ui.add(egui::Slider::new(&mut b, 8..=512)).changed() {
                c.bars = b as usize;
                ch = true;
            }
        });
        ch |= ui.checkbox(&mut c.autosens, "Automatic sensitivity (autosens)").changed();
        if !c.autosens {
            ch |= ui.add(egui::Slider::new(&mut c.sensitivity, 1.0..=400.0).text("Sensitivity %")).changed();
        }
        ch |= ui.add(egui::Slider::new(&mut c.noise_reduction, 0.0..=100.0).text("Noise reduction")).changed();
        ch |= ui.add(egui::Slider::new(&mut c.lower_cutoff_freq, 20..=500).text("Low cutoff Hz")).changed();
        ch |= ui.add(egui::Slider::new(&mut c.higher_cutoff_freq, 2000..=20000).text("High cutoff Hz")).changed();
        ch |= ui.add(egui::Slider::new(&mut c.monstercat, 0.0..=3.0).text("Monstercat")).changed();
        if c.monstercat > 0.0 {
            ch |= ui.checkbox(&mut c.waves, "Waves").changed();
        }
        ui.horizontal(|ui| {
            ui.label("Channels");
            for (layout, label) in [
                (ChannelLayout::Stereo, "Stereo"),
                (ChannelLayout::MonoAverage, "Mono"),
                (ChannelLayout::MonoLeft, "Left"),
                (ChannelLayout::MonoRight, "Right"),
            ] {
                if ui.selectable_label(c.layout == layout, label).clicked() && c.layout != layout {
                    c.layout = layout;
                    ch = true;
                }
            }
        });
        ch |= ui.checkbox(&mut c.reverse, "Reverse").changed();
        if ch {
            act.cava_changed = true;
            act.changed = true;
        }
        ui.horizontal(|ui| {
            if ui
                .add_enabled(info.cava_config_found, egui::Button::new("Import my cava config"))
                .on_hover_text("Reads ~/.config/cava/config: source, these settings and your colours")
                .clicked()
            {
                act.import_cava = true;
            }
            if ui.button("cava defaults").clicked() {
                s.cava = cymatica_audio::CavaSettings::default();
                act.cava_changed = true;
                act.changed = true;
            }
        });
    });
    ui.add_space(4.0);
    if ui
        .add(egui::Slider::new(&mut s.av_offset_ms, 0.0..=500.0).text("Visual delay (ms)"))
        .on_hover_text("Delays the visuals to line up with speakers or Bluetooth headphones that lag behind.")
        .changed()
    {
        act.changed = true;
    }
}

// ---------------------------------------------------------------- presets

fn presets_tab(ui: &mut egui::Ui, st: &mut UiState, s: &mut Settings, info: &UiInfo, act: &mut UiActions) {
    let scene_name = cymatica_engine::scene::scene_info(&s.scene).map(|i| i.name).unwrap_or("");
    ui.label(RichText::new(format!("{scene_name} presets")).strong());
    // a grid of pictures (rendered in the background; a plain tile until each one is ready)
    let thumb = Vec2::new(128.0, 72.0);
    let presets: Vec<&Preset> = info.presets.iter().filter(|p| p.scene == s.scene).collect();
    egui::Grid::new("preset-grid").num_columns(3).spacing([8.0, 6.0]).show(ui, |ui| {
        for (i, p) in presets.iter().enumerate() {
            ui.vertical(|ui| {
                ui.set_width(thumb.x);
                let button = match info.thumbnails.get(&p.key()) {
                    Some(tex) => {
                        egui::Button::image(egui::Image::from_texture(tex).fit_to_exact_size(thumb).corner_radius(4.0))
                    }
                    None => egui::Button::new(RichText::new("preview soon").small().weak()).min_size(thumb),
                };
                if ui.add(button).on_hover_text(&p.name).clicked() {
                    act.load_preset = Some((*p).clone());
                }
                let name = if p.builtin { p.name.clone() } else { format!("{} (mine)", p.name) };
                ui.add(egui::Label::new(RichText::new(name).small()).truncate());
                if !p.builtin && ui.small_button("Delete").clicked() {
                    act.delete_preset = Some((*p).clone());
                }
            });
            if i % 3 == 2 {
                ui.end_row();
            }
        }
    });
    ui.add_space(6.0);
    ui.label(RichText::new("Save current settings").strong());
    ui.horizontal(|ui| {
        ui.add(egui::TextEdit::singleline(&mut st.preset_name).hint_text("Preset name").desired_width(200.0));
        if ui.add_enabled(!st.preset_name.trim().is_empty(), egui::Button::new("Save")).clicked() {
            act.save_preset = Some(st.preset_name.trim().to_string());
            st.preset_name.clear();
        }
    });
    ui.horizontal(|ui| {
        if ui.button("Open presets folder").on_hover_text("Presets are JSON files: share them with friends").clicked() {
            act.open_presets_folder = true;
        }
        if ui.button("Surprise me").clicked() {
            act.randomize = Some(1.0);
        }
    });
    ui.add_space(6.0);
    egui::CollapsingHeader::new("Other scenes").default_open(false).show(ui, |ui| {
        for p in info.presets.iter().filter(|p| p.scene != s.scene) {
            let scene = cymatica_engine::scene::scene_info(&p.scene).map(|i| i.name).unwrap_or("");
            if ui.button(format!("{scene}: {}", p.name)).clicked() {
                act.load_preset = Some(p.clone());
            }
        }
    });
}

// ---------------------------------------------------------------- display

fn display_tab(ui: &mut egui::Ui, s: &mut Settings, info: &UiInfo, act: &mut UiActions) {
    ui.label(RichText::new("Window").strong());
    ui.horizontal(|ui| {
        for (mode, label, tip) in [
            (WindowMode::Window, "Window", "A normal window"),
            (WindowMode::Fullscreen, "Fullscreen", "Fill the screen (F)"),
            (WindowMode::Wallpaper, "Wallpaper", "Live wallpaper behind your desktop icons (macOS)"),
            (
                WindowMode::Overlay,
                "Overlay",
                "See-through, over everything: best with Bars and Phosphor. Clicks pass through while this \
                 panel is hidden; Cmd-Tab to Cymatica and press Esc to leave.",
            ),
        ] {
            if ui.selectable_label(s.window_mode == mode, label).on_hover_text(tip).clicked() && s.window_mode != mode {
                s.window_mode = mode;
                act.window_changed = true;
                act.changed = true;
            }
        }
    });
    if ui.checkbox(&mut s.always_on_top, "Always on top").changed() {
        act.window_changed = true;
        act.changed = true;
    }
    ui.add_space(6.0);
    ui.label(RichText::new("Quality").strong());
    if ui
        .add(egui::Slider::new(&mut s.render_scale, 0.25..=2.0).text("Render scale").step_by(0.05))
        .on_hover_text("1.0 renders every Retina pixel; 2.0 supersamples for an even crisper picture.")
        .changed()
    {
        act.changed = true;
    }
    ui.label(
        RichText::new(format!(
            "Rendering {}×{} on a {}×{} window · {}",
            info.render_size.0, info.render_size.1, info.window_size.0, info.window_size.1, info.gpu_name
        ))
        .small()
        .weak(),
    );
    if info.hdr_available
        && ui
            .checkbox(&mut s.hdr, "HDR (extended dynamic range)")
            .on_hover_text("Highlights brighter than white on XDR / HDR displays")
            .changed()
    {
        act.window_changed = true;
        act.changed = true;
    }
    if ui.checkbox(&mut s.show_hud, "Show info overlay (I)").changed() {
        act.changed = true;
    }
    if ui.add(egui::Slider::new(&mut s.crossfade_seconds, 0.0..=6.0).text("Scene crossfade (s)")).changed() {
        act.changed = true;
    }
    ui.add_space(6.0);
    ui.label(RichText::new("Auto-pilot").strong());
    let ap = &mut s.autopilot;
    let mut ch = false;
    ch |= ui.checkbox(&mut ap.enabled, "Change things with the music (D)").changed();
    ch |= ui.add(egui::Slider::new(&mut ap.every_bars, 1..=64).text("Every N bars")).changed();
    ui.horizontal(|ui| {
        ch |= ui.checkbox(&mut ap.change_scene, "Scene").changed();
        ch |= ui.checkbox(&mut ap.change_preset, "Preset").changed();
        ch |= ui.checkbox(&mut ap.change_theme, "Theme").changed();
    });
    ch |= ui.checkbox(&mut ap.on_song_change, "Also when a new song starts").changed();
    if ch {
        act.changed = true;
    }
    ui.add_space(6.0);
    ui.label(RichText::new("Screenshot").strong());
    ui.horizontal(|ui| {
        if ui.button("Window size (S)").clicked() {
            act.screenshot = Some(1.0);
        }
        if ui.button("2×").clicked() {
            act.screenshot = Some(2.0);
        }
        if ui.button("8K").on_hover_text("7680 pixels wide").clicked() {
            act.screenshot = Some(-7680.0);
        }
    });
    ui.label(RichText::new("Saved to Pictures/Cymatica").small().weak());
}

// ---------------------------------------------------------------- about / updates

fn about_tab(ui: &mut egui::Ui, info: &UiInfo, act: &mut UiActions) {
    ui.label(RichText::new(format!("Cymatica {}", info.version)).size(16.0).strong());
    ui.label(RichText::new(format!("Build {}", info.build)).small().weak());
    ui.add_space(6.0);
    match &info.update {
        UpdateState::Disabled => {
            ui.label("Updates are off in development builds.");
        }
        UpdateState::Idle => {
            ui.label("Updates are checked at launch and every hour.");
        }
        UpdateState::Checking => {
            ui.label("Checking for updates…");
        }
        UpdateState::UpToDate => {
            ui.label("You have the latest version.");
        }
        UpdateState::Available { version, notes } => {
            ui.label(RichText::new(format!("Cymatica {version} is available.")).strong());
            if ui.button("Download and install").clicked() {
                act.download_update = true;
            }
            notes_box(ui, notes);
        }
        UpdateState::Downloading { version } => {
            ui.label(format!("Downloading Cymatica {version}…"));
        }
        UpdateState::Ready { version, notes, .. } => {
            ui.label(RichText::new(format!("Cymatica {version} is ready.")).strong());
            if ui.button("Restart now").clicked() {
                act.install_update = true;
            }
            ui.label(RichText::new("Or it installs when you quit.").small().weak());
            notes_box(ui, notes);
        }
        UpdateState::Failed(e) => {
            ui.colored_label(Color32::from_rgb(240, 140, 100), format!("Update problem: {e}"));
        }
    }
    ui.horizontal(|ui| {
        if ui
            .add_enabled(
                !matches!(info.update, UpdateState::Disabled | UpdateState::Checking | UpdateState::Downloading { .. }),
                egui::Button::new("Check now"),
            )
            .clicked()
        {
            act.check_updates = true;
        }
        let mut auto = info.auto_update;
        if ui.checkbox(&mut auto, "Install updates automatically").changed() {
            act.set_auto_update = Some(auto);
        }
    });
    if info.can_roll_back && ui.button("Roll back to the previous version").clicked() {
        act.roll_back = true;
    }
    ui.add_space(10.0);
    ui.label(RichText::new("About").strong());
    ui.label("Physics-driven music visuals by Baastik Labs. Audio comes from BlackHole 2ch, set up exactly like cava.");
    ui.label(RichText::new("Bars use a port of cava's cavacore (MIT, © Karl Stavestrand).").small().weak());
    if ui.link("Uninstall instructions").clicked() {
        act.open_url = Some("https://github.com/dylancleverdon/Baastik-Labs/releases/tag/cymatica-channel".into());
    }
}

fn notes_box(ui: &mut egui::Ui, notes: &str) {
    egui::Frame::new()
        .fill(Color32::from_black_alpha(80))
        .corner_radius(CornerRadius::same(6))
        .inner_margin(egui::Margin::same(8))
        .show(ui, |ui| {
            ui.label(RichText::new("What's new").small().strong());
            for line in notes.lines().take(12) {
                ui.label(RichText::new(line).small());
            }
        });
}

// ---------------------------------------------------------------- welcome & toasts

fn draw_welcome(ctx: &egui::Context, st: &mut UiState, s: &mut Settings, info: &UiInfo, act: &mut UiActions) {
    egui::Window::new("Welcome to Cymatica")
        .collapsible(false)
        .resizable(false)
        .anchor(Align2::CENTER_CENTER, [0.0, 0.0])
        .default_width(440.0)
        .show(ctx, |ui| {
            ui.label("Cymatica turns whatever your Mac is playing into physics: liquids, ink, ripples, sparks and more.");
            ui.add_space(6.0);
            ui.label(RichText::new("Audio, the same way as cava").strong());
            if info.audio_status.blackhole_found {
                ui.colored_label(Color32::from_rgb(110, 220, 140), "BlackHole is installed. Cymatica listens to BlackHole 2ch.");
                ui.label("Make sure your Mac's sound output is the Multi-Output Device that includes BlackHole (System Settings > Sound), just like for cava.");
            } else {
                ui.colored_label(Color32::from_rgb(240, 190, 80), "BlackHole 2ch isn't installed yet, so a demo track is playing.");
                ui.label("Install it with Homebrew (brew install blackhole-2ch), then create a Multi-Output Device with your speakers + BlackHole in Audio MIDI Setup.");
                if ui.link("BlackHole Multi-Output guide").clicked() {
                    act.open_url = Some("https://github.com/ExistentialAudio/BlackHole/wiki/Multi-Output-Device".into());
                }
            }
            if info.is_macos {
                ui.label(RichText::new("macOS treats BlackHole like a microphone, so it asks for microphone access once. Audio never leaves your Mac.").small().weak());
            }
            ui.add_space(6.0);
            ui.label(RichText::new("Keys").strong());
            ui.label("H hide panel · Left/Right scenes · Up/Down presets · T themes · R randomize · F fullscreen · D auto-pilot · S screenshot");
            ui.add_space(8.0);
            if ui.button(RichText::new("Let's go").strong()).clicked() {
                st.show_welcome = false;
                s.welcomed = true;
                act.changed = true;
            }
        });
}

fn draw_toasts(ctx: &egui::Context, st: &mut UiState, s: &Settings, info: &UiInfo, act: &mut UiActions) {
    st.toasts.retain(|(_, t)| t.elapsed() < Duration::from_secs(4));
    let ready = matches!(info.update, UpdateState::Ready { .. });
    if st.toasts.is_empty() && !ready {
        return;
    }
    let _ = s;
    egui::Area::new(egui::Id::unique("toasts")).anchor(Align2::CENTER_TOP, [0.0, 16.0]).show(ctx, |ui| {
        for (msg, _) in &st.toasts {
            egui::Frame::new()
                .fill(Color32::from_rgba_unmultiplied(20, 20, 30, 230))
                .corner_radius(CornerRadius::same(10))
                .inner_margin(egui::Margin::symmetric(14, 8))
                .show(ui, |ui| {
                    // one line each: an anchored area starts out with no width, and wrapping
                    // would then put a letter or two on each line
                    ui.add(egui::Label::new(RichText::new(msg).strong()).extend());
                });
            ui.add_space(4.0);
        }
        if let UpdateState::Ready { version, .. } = &info.update {
            egui::Frame::new()
                .fill(Color32::from_rgba_unmultiplied(20, 20, 30, 230))
                .corner_radius(CornerRadius::same(10))
                .inner_margin(egui::Margin::symmetric(14, 8))
                .show(ui, |ui| {
                    ui.horizontal(|ui| {
                        ui.add(
                            egui::Label::new(RichText::new(format!("Cymatica {version} is ready")).strong()).extend(),
                        );
                        if ui.button("Restart now").clicked() {
                            act.install_update = true;
                        }
                    });
                });
        }
    });
}

/// The small info overlay (I).
pub fn draw_hud(ctx: &egui::Context, s: &Settings, info: &UiInfo) {
    if !s.show_hud {
        return;
    }
    let a = info.audio;
    egui::Area::new(egui::Id::unique("hud")).anchor(Align2::LEFT_TOP, [12.0, 12.0]).interactable(false).show(
        ctx,
        |ui| {
            egui::Frame::new()
                .fill(Color32::from_black_alpha(140))
                .corner_radius(CornerRadius::same(8))
                .inner_margin(egui::Margin::same(8))
                .show(ui, |ui| {
                    let scene = cymatica_engine::scene::scene_info(&s.scene).map(|i| i.name).unwrap_or("");
                    ui.label(RichText::new(format!("{scene} · {}", s.current_theme().name)).strong());
                    ui.label(
                        RichText::new(format!("{:.0} fps · {}×{}", info.fps, info.render_size.0, info.render_size.1))
                            .monospace()
                            .small(),
                    );
                    ui.label(
                        RichText::new(format!(
                            "{:.0} BPM · {} · bass {:.2} · loud {:.2}",
                            a.bpm,
                            NOTE_NAMES[a.note as usize % 12],
                            a.bass,
                            a.loudness
                        ))
                        .monospace()
                        .small(),
                    );
                    if info.frozen {
                        ui.label(RichText::new("frozen (space)").small());
                    }
                });
        },
    );
}
