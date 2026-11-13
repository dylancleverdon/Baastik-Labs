//! Cymatica: physics-driven music visuals for macOS.

mod app;
mod autopilot;
mod cli;
mod macos;
mod paths;
mod presets;
mod settings;
mod thumbnails;
mod ui;
mod updates;

struct Logger(std::sync::Mutex<Option<std::fs::File>>);

impl log::Log for Logger {
    fn enabled(&self, m: &log::Metadata) -> bool {
        m.level() <= log::Level::Info
    }
    fn log(&self, r: &log::Record) {
        if !self.enabled(r.metadata()) || !r.target().starts_with("cymatica") {
            return;
        }
        let line = format!("{} [{}] {}\n", cymatica_update::unix_now(), r.level(), r.args());
        eprint!("{line}");
        if let Ok(mut f) = self.0.lock() {
            if let Some(f) = f.as_mut() {
                use std::io::Write;
                let _ = f.write_all(line.as_bytes());
            }
        }
    }
    fn flush(&self) {}
}

fn init_logging() {
    let dir = paths::log_dir();
    let _ = std::fs::create_dir_all(&dir);
    let file = std::fs::OpenOptions::new().create(true).append(true).open(dir.join("cymatica.log")).ok();
    let logger = Box::leak(Box::new(Logger(std::sync::Mutex::new(file))));
    let _ = log::set_logger(logger);
    log::set_max_level(log::LevelFilter::Info);
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let cmd = args.first().map(String::as_str).unwrap_or("");
    let rest = args.iter().skip(1).cloned().collect();
    let result = match cmd {
        "render" => cli::render(rest),
        "self-test" => cli::self_test(rest),
        "thumbnails" => cli::thumbnails(rest),
        "update" => cli::update(rest),
        "--version" | "version" => {
            println!("Cymatica {} (build {})", updates::VERSION, updates::build_number());
            Ok(())
        }
        _ => {
            init_logging();
            log::info!("Cymatica {} (build {}) starting", updates::VERSION, updates::build_number());
            app::run()
        }
    };
    if let Err(e) = result {
        eprintln!("error: {e:#}");
        std::process::exit(1);
    }
}
