//! Camera matrices (right-handed, depth 0..1 as wgpu expects).

use glam::{Mat4, Vec3, Vec4};

pub fn look_at_rh(eye: Vec3, target: Vec3, up: Vec3) -> Mat4 {
    let f = (target - eye).normalize();
    let s = f.cross(up).normalize();
    let u = s.cross(f);
    Mat4::from_cols(
        Vec4::new(s.x, u.x, -f.x, 0.0),
        Vec4::new(s.y, u.y, -f.y, 0.0),
        Vec4::new(s.z, u.z, -f.z, 0.0),
        Vec4::new(-s.dot(eye), -u.dot(eye), f.dot(eye), 1.0),
    )
}

pub fn perspective_rh(fov_y: f32, aspect: f32, near: f32, far: f32) -> Mat4 {
    let f = 1.0 / (fov_y * 0.5).tan();
    let r = far / (near - far);
    Mat4::from_cols(
        Vec4::new(f / aspect, 0.0, 0.0, 0.0),
        Vec4::new(0.0, f, 0.0, 0.0),
        Vec4::new(0.0, 0.0, r, -1.0),
        Vec4::new(0.0, 0.0, r * near, 0.0),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn projects_points_where_expected() {
        let view = look_at_rh(Vec3::new(0.0, 0.0, 5.0), Vec3::ZERO, Vec3::Y);
        let proj = perspective_rh(60f32.to_radians(), 1.0, 0.1, 100.0);
        // the target lands in the middle of the screen
        let c = proj * view * Vec4::new(0.0, 0.0, 0.0, 1.0);
        assert!((c.x / c.w).abs() < 1e-5 && (c.y / c.w).abs() < 1e-5);
        let z = c.z / c.w;
        assert!(z > 0.0 && z < 1.0);
        // near plane maps to 0, far to 1
        let near = proj * Vec4::new(0.0, 0.0, -0.1, 1.0);
        let far = proj * Vec4::new(0.0, 0.0, -100.0, 1.0);
        assert!((near.z / near.w).abs() < 1e-5);
        assert!((far.z / far.w - 1.0).abs() < 1e-4);
        // up is up
        let up = proj * view * Vec4::new(0.0, 1.0, 0.0, 1.0);
        assert!(up.y / up.w > 0.0);
    }
}
