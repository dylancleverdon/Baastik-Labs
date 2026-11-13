//! Where Cymatica keeps its files.

use std::path::PathBuf;

pub fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("."))
}

/// ~/Library/Application Support/Cymatica on macOS (XDG config dir elsewhere, for development).
pub fn support_dir() -> PathBuf {
    if let Some(dir) = std::env::var_os("CYMATICA_SUPPORT_DIR") {
        return PathBuf::from(dir);
    }
    if cfg!(target_os = "macos") {
        home().join("Library/Application Support/Cymatica")
    } else {
        std::env::var_os("XDG_CONFIG_HOME")
            .map(PathBuf::from)
            .unwrap_or_else(|| home().join(".config"))
            .join("cymatica")
    }
}

pub fn settings_file() -> PathBuf {
    support_dir().join("settings.json")
}

pub fn presets_dir() -> PathBuf {
    support_dir().join("presets")
}

pub fn screenshots_dir() -> PathBuf {
    home().join("Pictures/Cymatica")
}

pub fn log_dir() -> PathBuf {
    if cfg!(target_os = "macos") {
        home().join("Library/Logs/Cymatica")
    } else {
        support_dir().join("logs")
    }
}
