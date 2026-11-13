//! Cymatica's updater, modelled on Norg's: it only installs builds from this repository's
//! `cymatica-*` GitHub releases, and only when the download matches the SHA-256 published in the
//! manifest. Downloads use curl (so macOS never quarantines them), zips are unpacked with ditto,
//! the new app is re-signed with the Mac's local identity (so the microphone permission sticks),
//! and the previous build is kept for rollback.

use anyhow::{anyhow, bail, Context, Result};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::path::{Path, PathBuf};
use std::process::Command;

pub const REPO: &str = "dylancleverdon/Baastik-Labs";
pub const CHANNEL: &str = "cymatica-channel";
pub const MANIFEST_NAME: &str = "cymatica-update.json";
pub const SCHEMA: u32 = 1;

pub fn default_manifest_url() -> String {
    format!("https://github.com/{REPO}/releases/download/{CHANNEL}/{MANIFEST_NAME}")
}

/// Every download must start with this (this repo's `cymatica-*` releases).
pub fn allowed_prefix() -> String {
    format!("https://github.com/{REPO}/releases/download/cymatica-")
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Manifest {
    pub schema: u32,
    pub version: String,
    pub build: u64,
    #[serde(default)]
    pub commit: String,
    #[serde(default)]
    pub notes: String,
    pub zip_name: String,
    pub zip_url: String,
    pub sha256: String,
    #[serde(default)]
    pub published_at: String,
}

fn is_safe_name(name: &str) -> bool {
    !name.is_empty()
        && name.len() < 128
        && name.ends_with(".zip")
        && name.chars().all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '-' | '_'))
        && !name.contains("..")
}

pub fn parse_manifest(json: &str) -> Result<Manifest> {
    let m: Manifest = serde_json::from_str(json).context("manifest is not valid JSON")?;
    if m.schema != SCHEMA {
        bail!("unsupported manifest schema {}", m.schema);
    }
    if m.version.is_empty() {
        bail!("manifest has no version");
    }
    if m.build == 0 {
        bail!("manifest has no build number");
    }
    if !is_safe_name(&m.zip_name) {
        bail!("manifest zip name is not allowed");
    }
    if m.sha256.len() != 64 || !m.sha256.chars().all(|c| c.is_ascii_hexdigit()) {
        bail!("manifest sha256 is malformed");
    }
    Ok(Manifest { sha256: m.sha256.to_lowercase(), ..m })
}

/// Only this repo's release assets (plus explicit test prefixes such as `file://`).
pub fn is_allowed_url(url: &str, extra_prefixes: &[String]) -> bool {
    if url.contains("..") {
        return false;
    }
    url.starts_with(&allowed_prefix()) || extra_prefixes.iter().any(|p| !p.is_empty() && url.starts_with(p.as_str()))
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Decision {
    UpToDate,
    /// The user rolled back from this build; don't reinstall it automatically.
    Skipped,
    Install,
}

pub fn decide(manifest: &Manifest, installed_build: u64, settings: &UpdaterSettings, manual: bool) -> Decision {
    if manifest.build <= installed_build {
        return Decision::UpToDate;
    }
    if !manual && settings.skipped_builds.contains(&manifest.build) {
        return Decision::Skipped;
    }
    Decision::Install
}

/// Persistent updater settings (`updater.json` in the support folder).
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct UpdaterSettings {
    pub auto_update: Option<bool>,
    pub skipped_builds: Vec<u64>,
    pub last_check_unix: u64,
}

impl UpdaterSettings {
    pub fn auto_update(&self) -> bool {
        self.auto_update.unwrap_or(true)
    }
}

/// Where everything lives. `home` is overridable for tests.
#[derive(Clone, Debug)]
pub struct Paths {
    pub home: PathBuf,
}

impl Paths {
    pub fn new(home: impl Into<PathBuf>) -> Self {
        Self { home: home.into() }
    }

    pub fn from_env() -> Self {
        Self::new(std::env::var("HOME").unwrap_or_else(|_| ".".into()))
    }

    pub fn support(&self) -> PathBuf {
        self.home.join("Library/Application Support/Cymatica")
    }
    pub fn staging(&self) -> PathBuf {
        self.support().join("staging")
    }
    pub fn previous(&self) -> PathBuf {
        self.support().join("previous")
    }
    pub fn settings_file(&self) -> PathBuf {
        self.support().join("updater.json")
    }
    pub fn log_file(&self) -> PathBuf {
        self.home.join("Library/Logs/Cymatica/updater.log")
    }
    pub fn default_app(&self) -> PathBuf {
        self.home.join("Applications/Cymatica.app")
    }

    pub fn load_settings(&self) -> UpdaterSettings {
        std::fs::read_to_string(self.settings_file())
            .ok()
            .and_then(|s| serde_json::from_str(&s).ok())
            .unwrap_or_default()
    }

    pub fn save_settings(&self, s: &UpdaterSettings) -> Result<()> {
        std::fs::create_dir_all(self.support())?;
        std::fs::write(self.settings_file(), serde_json::to_string_pretty(s)?)?;
        Ok(())
    }

    pub fn log(&self, msg: &str) {
        let path = self.log_file();
        if let Some(dir) = path.parent() {
            let _ = std::fs::create_dir_all(dir);
        }
        let line = format!("{} {msg}\n", unix_now());
        use std::io::Write;
        if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(path) {
            let _ = f.write_all(line.as_bytes());
        }
    }
}

pub fn unix_now() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0)
}

pub fn sha256_file(path: &Path) -> Result<String> {
    use std::io::Read;
    let mut f = std::fs::File::open(path)?;
    let mut h = Sha256::new();
    let mut buf = vec![0u8; 1 << 16];
    loop {
        let n = f.read(&mut buf)?;
        if n == 0 {
            break;
        }
        h.update(&buf[..n]);
    }
    Ok(h.finalize().iter().map(|b| format!("{b:02x}")).collect())
}

/// Downloads with curl (never a browser, so no quarantine flag).
pub fn download(url: &str, dest: &Path) -> Result<()> {
    if let Some(dir) = dest.parent() {
        std::fs::create_dir_all(dir)?;
    }
    let status = Command::new("curl")
        .args(["-fsSL", "--retry", "3", "--connect-timeout", "20", "--max-time", "600", "-o"])
        .arg(dest)
        .arg(url)
        .status()
        .context("running curl")?;
    if !status.success() {
        bail!("download failed ({status}): {url}");
    }
    Ok(())
}

/// Reads CFBundleVersion (our build number) from an app's Info.plist.
pub fn bundle_build(app: &Path) -> Option<u64> {
    let plist = std::fs::read_to_string(app.join("Contents/Info.plist")).ok()?;
    let key = plist.find("<key>CFBundleVersion</key>")?;
    let rest = &plist[key..];
    let start = rest.find("<string>")? + "<string>".len();
    let end = rest[start..].find("</string>")?;
    rest[start..start + end].trim().parse().ok()
}

pub struct CheckOptions {
    pub manifest_url: String,
    pub extra_prefixes: Vec<String>,
    pub manual: bool,
}

impl Default for CheckOptions {
    fn default() -> Self {
        Self { manifest_url: default_manifest_url(), extra_prefixes: Vec::new(), manual: false }
    }
}

/// Fetches and validates the manifest, and decides what to do about it.
pub fn check(paths: &Paths, installed_build: u64, opts: &CheckOptions) -> Result<(Manifest, Decision)> {
    if !is_allowed_url(&opts.manifest_url, &opts.extra_prefixes) && !opts.manifest_url.ends_with(MANIFEST_NAME) {
        bail!("manifest URL is not allowed");
    }
    let staging = paths.staging();
    std::fs::create_dir_all(&staging)?;
    let mfile = staging.join(MANIFEST_NAME);
    download(&opts.manifest_url, &mfile).context("could not download the update manifest")?;
    let manifest = parse_manifest(&std::fs::read_to_string(&mfile)?)?;
    if !is_allowed_url(&manifest.zip_url, &opts.extra_prefixes) {
        bail!("manifest points at a URL outside Cymatica's releases: {}", manifest.zip_url);
    }
    let mut settings = paths.load_settings();
    settings.last_check_unix = unix_now();
    let _ = paths.save_settings(&settings);
    let decision = decide(&manifest, installed_build, &settings, opts.manual);
    Ok((manifest, decision))
}

/// Downloads, verifies and unpacks an update; returns the staged `Cymatica.app`.
pub fn stage(paths: &Paths, manifest: &Manifest, opts: &CheckOptions) -> Result<PathBuf> {
    if !is_allowed_url(&manifest.zip_url, &opts.extra_prefixes) {
        bail!("refusing to download from {}", manifest.zip_url);
    }
    let staging = paths.staging();
    let zip = staging.join(&manifest.zip_name);
    download(&manifest.zip_url, &zip)?;
    let got = sha256_file(&zip)?;
    if got != manifest.sha256 {
        let _ = std::fs::remove_file(&zip);
        bail!("checksum mismatch: expected {}, got {got}", manifest.sha256);
    }
    let unpack = staging.join(format!("build-{}", manifest.build));
    let _ = std::fs::remove_dir_all(&unpack);
    std::fs::create_dir_all(&unpack)?;
    unzip(&zip, &unpack)?;
    let app = find_app(&unpack).ok_or_else(|| anyhow!("the update doesn't contain Cymatica.app"))?;
    if bundle_build(&app) != Some(manifest.build) {
        bail!("update contents are for a different build than the manifest");
    }
    let _ = std::fs::remove_file(&zip);
    // keep microphone permission: sign with this Mac's local identity
    let signer = app.join("Contents/Resources/sign-local.sh");
    if cfg!(target_os = "macos") && signer.exists() {
        let out = Command::new("/bin/bash")
            .arg(&signer)
            .arg("sign")
            .arg(&app)
            .env("CYMATICA_SUPPORT_DIR", paths.support())
            .output();
        match out {
            Ok(o) => paths.log(&format!("signed staged app: {}", String::from_utf8_lossy(&o.stdout).trim())),
            Err(e) => paths.log(&format!("signing failed: {e}")),
        }
    }
    Ok(app)
}

fn unzip(zip: &Path, dest: &Path) -> Result<()> {
    let status = if cfg!(target_os = "macos") {
        Command::new("/usr/bin/ditto").arg("-x").arg("-k").arg(zip).arg(dest).status()
    } else {
        Command::new("unzip").arg("-q").arg(zip).arg("-d").arg(dest).status()
    }
    .context("unpacking the update")?;
    if !status.success() {
        bail!("could not unpack the update ({status})");
    }
    Ok(())
}

fn find_app(dir: &Path) -> Option<PathBuf> {
    let direct = dir.join("Cymatica.app");
    if direct.is_dir() {
        return Some(direct);
    }
    for entry in std::fs::read_dir(dir).ok()?.flatten() {
        let p = entry.path();
        if p.is_dir() {
            let nested = p.join("Cymatica.app");
            if nested.is_dir() {
                return Some(nested);
            }
        }
    }
    None
}

/// Swaps `staged` into `target`, keeping the current build in `previous/` for rollback.
pub fn install(paths: &Paths, staged: &Path, target: &Path) -> Result<()> {
    let previous = paths.previous();
    std::fs::create_dir_all(&previous)?;
    let prev_app = previous.join("Cymatica.app");
    let _ = std::fs::remove_dir_all(&prev_app);
    if target.exists() {
        std::fs::rename(target, &prev_app).context("moving the current app aside")?;
    }
    if let Some(dir) = target.parent() {
        std::fs::create_dir_all(dir)?;
    }
    if let Err(e) = std::fs::rename(staged, target) {
        // put the old app back
        let _ = std::fs::rename(&prev_app, target);
        return Err(e).context("moving the new app into place");
    }
    paths.log(&format!("installed build {:?} at {}", bundle_build(target), target.display()));
    Ok(())
}

/// Swaps the previous build back in and remembers to skip the one we rolled back from.
pub fn rollback(paths: &Paths, target: &Path) -> Result<()> {
    let prev_app = paths.previous().join("Cymatica.app");
    if !prev_app.is_dir() {
        bail!("there is no previous version to roll back to");
    }
    let bad = bundle_build(target);
    let tmp = paths.previous().join("rolling-back.app");
    let _ = std::fs::remove_dir_all(&tmp);
    std::fs::rename(target, &tmp)?;
    std::fs::rename(&prev_app, target)?;
    std::fs::rename(&tmp, &prev_app)?;
    if let Some(b) = bad {
        let mut s = paths.load_settings();
        if !s.skipped_builds.contains(&b) {
            s.skipped_builds.push(b);
        }
        paths.save_settings(&s)?;
    }
    paths.log(&format!("rolled back from build {bad:?}"));
    Ok(())
}

/// Opens `app` again once process `pid` has exited.
pub fn relaunch_after_exit(app: &Path, pid: u32) -> Result<()> {
    let script = format!(
        "while kill -0 {pid} 2>/dev/null; do sleep 0.2; done; open -n \"{}\"",
        app.display().to_string().replace('"', "\\\"")
    );
    Command::new("/bin/sh").arg("-c").arg(script).spawn().context("scheduling the relaunch")?;
    Ok(())
}

/// The bundle containing the running executable (`.../Cymatica.app`), if any.
pub fn running_bundle() -> Option<PathBuf> {
    let exe = std::env::current_exe().ok()?;
    let app = exe.parent()?.parent()?.parent()?;
    (app.extension().map(|e| e == "app").unwrap_or(false)).then(|| app.to_path_buf())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn manifest(build: u64) -> Manifest {
        Manifest {
            schema: 1,
            version: format!("0.1.{build}"),
            build,
            commit: "abc".into(),
            notes: "- things".into(),
            zip_name: format!("Cymatica-0.1.{build}-mac.zip"),
            zip_url: format!("{}v0.1.{build}/Cymatica-0.1.{build}-mac.zip", allowed_prefix()),
            sha256: "a".repeat(64),
            published_at: "2026-01-01T00:00:00Z".into(),
        }
    }

    #[test]
    fn parses_and_validates_manifests() {
        let m = manifest(7);
        let json = serde_json::to_string(&m).unwrap();
        assert!(json.contains("zipUrl"));
        assert_eq!(parse_manifest(&json).unwrap(), m);
        let bad = json.replace("\"schema\":1", "\"schema\":9");
        assert!(parse_manifest(&bad).is_err());
        let bad = json.replace(&"a".repeat(64), "xyz");
        assert!(parse_manifest(&bad).is_err());
        let bad = json.replace("Cymatica-0.1.7-mac.zip\",", "../../evil.zip\",");
        assert!(parse_manifest(&bad).is_err());
    }

    #[test]
    fn only_this_repos_releases_are_allowed() {
        let ok = format!("{}v0.1.3/Cymatica-0.1.3-mac.zip", allowed_prefix());
        assert!(is_allowed_url(&ok, &[]));
        assert!(!is_allowed_url("https://evil.example.com/Cymatica.zip", &[]));
        assert!(!is_allowed_url("https://github.com/dylancleverdon/Baastik-Labs/releases/download/norg-v1/x.zip", &[]));
        assert!(!is_allowed_url(&format!("{}../norg/x.zip", allowed_prefix()), &[]));
        assert!(is_allowed_url("file:///tmp/x.zip", &["file://".into()]));
    }

    #[test]
    fn decides_like_norg() {
        let s = UpdaterSettings::default();
        assert_eq!(decide(&manifest(5), 5, &s, false), Decision::UpToDate);
        assert_eq!(decide(&manifest(4), 5, &s, false), Decision::UpToDate);
        assert_eq!(decide(&manifest(6), 5, &s, false), Decision::Install);
        let skip = UpdaterSettings { skipped_builds: vec![6], ..Default::default() };
        assert_eq!(decide(&manifest(6), 5, &skip, false), Decision::Skipped);
        assert_eq!(decide(&manifest(6), 5, &skip, true), Decision::Install);
        assert_eq!(decide(&manifest(7), 5, &skip, false), Decision::Install);
    }

    #[test]
    fn reads_the_build_number_from_info_plist() {
        let dir = std::env::temp_dir().join(format!("cym-upd-{}", std::process::id()));
        let app = dir.join("Cymatica.app/Contents");
        std::fs::create_dir_all(&app).unwrap();
        std::fs::write(
            app.join("Info.plist"),
            "<plist><dict><key>CFBundleShortVersionString</key><string>0.1.42</string>\n<key>CFBundleVersion</key>\n  <string>42</string></dict></plist>",
        )
        .unwrap();
        assert_eq!(bundle_build(&dir.join("Cymatica.app")), Some(42));
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn install_and_rollback_swap_bundles() {
        let root = std::env::temp_dir().join(format!("cym-upd-swap-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        let paths = Paths::new(&root);
        let mk = |dir: &Path, build: u64| {
            let c = dir.join("Cymatica.app/Contents");
            std::fs::create_dir_all(&c).unwrap();
            std::fs::write(c.join("Info.plist"), format!("<key>CFBundleVersion</key><string>{build}</string>")).unwrap();
            dir.join("Cymatica.app")
        };
        let target = paths.default_app();
        mk(target.parent().unwrap(), 1);
        let staged = mk(&root.join("stage"), 2);
        install(&paths, &staged, &target).unwrap();
        assert_eq!(bundle_build(&target), Some(2));
        assert_eq!(bundle_build(&paths.previous().join("Cymatica.app")), Some(1));
        rollback(&paths, &target).unwrap();
        assert_eq!(bundle_build(&target), Some(1));
        assert_eq!(paths.load_settings().skipped_builds, vec![2]);
        std::fs::remove_dir_all(root).ok();
    }

    #[test]
    fn sha256_matches_known_value() {
        let p = std::env::temp_dir().join(format!("cym-sha-{}", std::process::id()));
        std::fs::write(&p, b"abc").unwrap();
        assert_eq!(sha256_file(&p).unwrap(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        std::fs::remove_file(p).ok();
    }
}
