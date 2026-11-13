//! macOS-only glue: EDR (HDR) output, live-wallpaper window level, microphone permission.

use winit::window::Window;

#[cfg(target_os = "macos")]
mod imp {
    use super::*;
    use objc2::rc::Retained;
    use winit::raw_window_handle::{HasWindowHandle, RawWindowHandle};
    use objc2_app_kit::{NSView, NSWindow, NSWindowCollectionBehavior};

    fn ns_view(window: &Window) -> Option<&NSView> {
        let handle = window.window_handle().ok()?;
        match handle.as_raw() {
            // SAFETY: winit hands out a valid NSView pointer for the window's lifetime, and we're
            // on the main thread (event loop callbacks).
            RawWindowHandle::AppKit(h) => Some(unsafe { h.ns_view.cast::<NSView>().as_ref() }),
            _ => None,
        }
    }

    fn ns_window(window: &Window) -> Option<Retained<NSWindow>> {
        ns_view(window)?.window()
    }

    /// Live wallpaper: the window sits behind the desktop icons, on every Space, and ignores
    /// the mouse.
    pub fn set_wallpaper(window: &Window, on: bool) {
        let Some(w) = ns_window(window) else { return };
        if on {
            let level = objc2_core_graphics::CGWindowLevelForKey(objc2_core_graphics::CGWindowLevelKey::DesktopWindowLevelKey);
            w.setLevel(level as isize);
            w.setIgnoresMouseEvents(true);
            w.setCollectionBehavior(
                NSWindowCollectionBehavior::CanJoinAllSpaces
                    | NSWindowCollectionBehavior::Stationary
                    | NSWindowCollectionBehavior::IgnoresCycle,
            );
        } else {
            w.setLevel(0);
            w.setIgnoresMouseEvents(false);
            w.setCollectionBehavior(NSWindowCollectionBehavior::Default);
        }
    }

    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub enum Mic {
        NotDetermined,
        Denied,
        Authorized,
    }

    pub fn mic_permission() -> Mic {
        use objc2_av_foundation::{AVAuthorizationStatus, AVCaptureDevice, AVMediaTypeAudio};
        // SAFETY: plain class method call with a framework-provided constant.
        let status = unsafe {
            match AVMediaTypeAudio {
                Some(t) => AVCaptureDevice::authorizationStatusForMediaType(t),
                None => return Mic::Authorized,
            }
        };
        if status == AVAuthorizationStatus::Authorized {
            Mic::Authorized
        } else if status == AVAuthorizationStatus::NotDetermined {
            Mic::NotDetermined
        } else {
            Mic::Denied
        }
    }

    /// Shows macOS's microphone prompt now (BlackHole counts as a microphone).
    pub fn request_mic_permission() {
        use objc2_av_foundation::{AVCaptureDevice, AVMediaTypeAudio};
        let handler = block2::RcBlock::new(|_granted: objc2::runtime::Bool| {});
        // SAFETY: framework call with a valid block; the result is reported through the block.
        unsafe {
            if let Some(t) = AVMediaTypeAudio {
                AVCaptureDevice::requestAccessForMediaType_completionHandler(t, &handler);
            }
        }
    }
}

#[cfg(not(target_os = "macos"))]
mod imp {
    use super::*;

    pub fn set_wallpaper(_: &Window, _: bool) {}

    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub enum Mic {
        NotDetermined,
        Denied,
        Authorized,
    }
    pub fn mic_permission() -> Mic {
        let _ = (Mic::NotDetermined, Mic::Denied);
        Mic::Authorized
    }
    pub fn request_mic_permission() {}
}

pub use imp::*;

/// Opens a URL, file or folder with the system's default handler.
pub fn open(target: &str) {
    let cmd = if cfg!(target_os = "macos") { "open" } else { "xdg-open" };
    let _ = std::process::Command::new(cmd).arg(target).spawn();
}

pub fn open_microphone_settings() {
    open("x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone");
}
