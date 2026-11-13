struct BarsU {
    count: u32,
    style: u32,      // 0 classic, 1 jelly, 2 sparks
    arrange: u32,    // 0 bottom, 1 mirror, 2 radial
    color_by: u32,   // 0 height, 1 frequency, 2 time
    gap: f32,
    height: f32,
    roundness: f32,
    glow: f32,
    reflection: f32,
    spring: f32,
    wobble: f32,
    caps: u32,
    cap_gravity: f32,
    sparks: f32,
    spark_gravity: f32,
    radial_radius: f32,
    spin: f32,
    bg: f32,
    burst: f32,
    max_particles: u32,
};

struct Particle {
    pos: vec2f,      // pixels, y up
    vel: vec2f,      // pixels / s
    color_t: f32,
    life: f32,
    max_life: f32,
    size: f32,
};
