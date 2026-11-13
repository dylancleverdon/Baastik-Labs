struct StarU {
    view: mat4x4f,
    proj: mat4x4f,
    attractors: array<vec4f, 6>,   // xyz position, w mass
    count: u32,
    n_attractors: u32,
    seed: u32,
    color_by: u32,
    gravity: f32,
    turbulence: f32,
    spin: f32,
    damping: f32,
    nova: vec4f,                   // xyz centre, w strength
    size: f32,
    brightness: f32,
    noise_scale: f32,
    time: f32,
};

struct Star {
    pos: vec3f,
    hue: f32,
    vel: vec3f,
    size: f32,
};
