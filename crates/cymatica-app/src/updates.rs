//! In-app update flow: checks at launch and hourly, downloads and verifies in the background, then
//! offers "Restart now" (or installs on quit).

use cymatica_update as upd;
use parking_lot::Mutex;
use std::path::PathBuf;
use std::sync::Arc;
use std::time::{Duration, Instant};

pub const VERSION: &str = match option_env!("CYMATICA_VERSION") {
    Some(v) => v,
    None => "dev",
};

pub fn build_number() -> u64 {
    option_env!("CYMATICA_BUILD").and_then(|b| b.parse().ok()).unwrap_or(0)
}

#[derive(Clone, Debug, PartialEq)]
pub enum UpdateState {
    /// Not running from an installed Cymatica.app (development build).
    Disabled,
    Idle,
    Checking,
    UpToDate,
    /// A newer build exists but automatic updates are off.
    Available {
        version: String,
        notes: String,
    },
    Downloading {
        version: String,
    },
    Ready {
        version: String,
        notes: String,
        staged: PathBuf,
    },
    Failed(String),
}

pub struct Updates {
    state: Arc<Mutex<UpdateState>>,
    paths: upd::Paths,
    bundle: Option<PathBuf>,
    started: Instant,
    last_check: Option<Instant>,
    busy: Arc<Mutex<bool>>,
    pub auto_update: bool,
}

impl Updates {
    pub fn new() -> Self {
        let paths = upd::Paths::from_env();
        let bundle = upd::running_bundle().filter(|_| build_number() > 0);
        let auto_update = paths.load_settings().auto_update();
        let state = if bundle.is_some() { UpdateState::Idle } else { UpdateState::Disabled };
        Self {
            state: Arc::new(Mutex::new(state)),
            paths,
            bundle,
            started: Instant::now(),
            last_check: None,
            busy: Arc::new(Mutex::new(false)),
            auto_update,
        }
    }

    pub fn state(&self) -> UpdateState {
        self.state.lock().clone()
    }

    /// Call every frame: checks 20 s after launch, then every hour.
    pub fn tick(&mut self) {
        if self.bundle.is_none() {
            return;
        }
        let due = match self.last_check {
            None => self.started.elapsed() > Duration::from_secs(20),
            Some(t) => t.elapsed() > Duration::from_secs(3600),
        };
        if due {
            self.check(false);
        }
    }

    pub fn set_auto_update(&mut self, on: bool) {
        self.auto_update = on;
        let mut s = self.paths.load_settings();
        s.auto_update = Some(on);
        let _ = self.paths.save_settings(&s);
    }

    pub fn check(&mut self, manual: bool) {
        if self.bundle.is_none() || *self.busy.lock() {
            return;
        }
        if matches!(self.state(), UpdateState::Ready { .. }) {
            return;
        }
        self.last_check = Some(Instant::now());
        *self.busy.lock() = true;
        let state = self.state.clone();
        let busy = self.busy.clone();
        let paths = self.paths.clone();
        let auto = self.auto_update || manual;
        *state.lock() = UpdateState::Checking;
        std::thread::spawn(move || {
            let opts = upd::CheckOptions { manual, ..Default::default() };
            let result = (|| -> anyhow::Result<UpdateState> {
                let (m, decision) = upd::check(&paths, build_number(), &opts)?;
                match decision {
                    upd::Decision::UpToDate | upd::Decision::Skipped => Ok(UpdateState::UpToDate),
                    upd::Decision::Install if !auto => {
                        Ok(UpdateState::Available { version: m.version.clone(), notes: m.notes.clone() })
                    }
                    upd::Decision::Install => {
                        *state.lock() = UpdateState::Downloading { version: m.version.clone() };
                        paths.log(&format!("downloading Cymatica {}", m.version));
                        let staged = upd::stage(&paths, &m, &opts)?;
                        paths.log(&format!("Cymatica {} is ready", m.version));
                        Ok(UpdateState::Ready { version: m.version.clone(), notes: m.notes.clone(), staged })
                    }
                }
            })();
            let next = match result {
                Ok(s) => s,
                Err(e) => {
                    paths.log(&format!("update check failed: {e:#}"));
                    UpdateState::Failed(format!("{e:#}"))
                }
            };
            *state.lock() = next;
            *busy.lock() = false;
        });
    }

    /// Download and install the available update now (used when auto-update is off).
    pub fn download(&mut self) {
        let was_auto = self.auto_update;
        self.auto_update = true;
        self.check(true);
        self.auto_update = was_auto;
    }

    /// Swaps in a staged update. Returns true if the app should now quit (and relaunch).
    pub fn install_ready(&mut self, relaunch: bool) -> bool {
        let (UpdateState::Ready { staged, .. }, Some(bundle)) = (self.state(), self.bundle.clone()) else {
            return false;
        };
        match upd::install(&self.paths, &staged, &bundle) {
            Ok(()) => {
                if relaunch {
                    let _ = upd::relaunch_after_exit(&bundle, std::process::id());
                }
                true
            }
            Err(e) => {
                self.paths.log(&format!("install failed: {e:#}"));
                *self.state.lock() = UpdateState::Failed(format!("{e:#}"));
                false
            }
        }
    }

    pub fn can_roll_back(&self) -> bool {
        self.bundle.is_some() && self.paths.previous().join("Cymatica.app").is_dir()
    }

    /// Restores the previous build. Returns true if the app should quit (it relaunches).
    pub fn roll_back(&mut self) -> bool {
        let Some(bundle) = self.bundle.clone() else { return false };
        match upd::rollback(&self.paths, &bundle) {
            Ok(()) => {
                let _ = upd::relaunch_after_exit(&bundle, std::process::id());
                true
            }
            Err(e) => {
                *self.state.lock() = UpdateState::Failed(format!("{e:#}"));
                false
            }
        }
    }
}
