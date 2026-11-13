// Runs cava's real cavacore.c on a deterministic, integer-only test signal and writes the
// results to a fixture that crates/cymatica-audio/tests/cava_golden.rs compares against.
// The signal generator here must stay in sync with `golden_frame` in that test.
#include "cavacore.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t lcg;
static uint32_t next_rand(void) {
    lcg = lcg * 1664525u + 1013904223u;
    return lcg;
}

// One interleaved stereo frame (or mono) of the golden signal at sample index n.
static void golden_frame(uint32_t n, int channels, double *out) {
    int32_t saw = (int32_t)((n % 436u) * 16000u / 436u) - 8000;
    int32_t sq = ((n / 27u) % 2u == 0u) ? 3000 : -3000;
    int32_t noise = (int32_t)((next_rand() >> 16) & 0x7fffu) - 16384;
    uint32_t k = n % 24000u;
    int32_t kick = 0;
    if (k < 4800u) {
        int32_t env = (int32_t)(4800u - k) * 12000 / 4800;
        int32_t phase = (int32_t)(k % 120u);
        int32_t tri = phase < 60 ? phase * 2 - 60 : 180 - phase * 2; // -60..60
        kick = env * tri / 60;
    }
    int32_t left = saw + sq + noise / 4 + kick;
    int32_t right = saw / 2 + noise / 2 + kick;
    if (left > 32767) left = 32767;
    if (left < -32768) left = -32768;
    if (right > 32767) right = 32767;
    if (right < -32768) right = -32768;
    if (channels == 2) {
        out[0] = (double)left;
        out[1] = (double)right;
    } else {
        out[0] = (double)((left + right) / 2);
    }
}

static const uint32_t CHUNK_FRAMES[] = {400, 800, 367, 1024, 0, 512};

static void put_u32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put_i32(FILE *f, int32_t v) { fwrite(&v, 4, 1, f); }
static void put_f32(FILE *f, float v) { fwrite(&v, 4, 1, f); }
static void put_f64(FILE *f, double v) { fwrite(&v, 8, 1, f); }

static void run(FILE *f, int bars, unsigned rate, int channels, int autosens, double nr, int low,
                int high, int scaling, int frames) {
    struct cava_plan *p = cava_init(bars, rate, channels, autosens, nr, low, high, scaling);
    if (p->status != 0) {
        fprintf(stderr, "cava_init failed: %s\n", p->error_message);
        exit(1);
    }
    put_u32(f, bars); put_u32(f, rate); put_u32(f, channels); put_i32(f, autosens);
    put_f64(f, nr); put_i32(f, low); put_i32(f, high); put_u32(f, scaling); put_u32(f, frames);
    put_u32(f, p->bass_cut_off_bar);
    for (int i = 0; i <= bars; i++) put_i32(f, p->FFTbuffer_lower_cut_off[i]);
    for (int i = 0; i < bars; i++) put_i32(f, p->FFTbuffer_upper_cut_off[i]);
    for (int i = 0; i <= bars; i++) put_f32(f, p->cut_off_frequency[i]);

    lcg = 12345u;
    uint32_t n = 0;
    double *in = malloc(sizeof(double) * 2048 * 2);
    double *out = malloc(sizeof(double) * bars * channels);
    for (int fr = 0; fr < frames; fr++) {
        uint32_t chunk = CHUNK_FRAMES[fr % 6];
        for (uint32_t i = 0; i < chunk; i++) golden_frame(n++, channels, &in[i * channels]);
        int new_samples = (int)(chunk * channels);
        cava_execute(in, new_samples, out, p);
        put_u32(f, (uint32_t)new_samples);
        for (int i = 0; i < bars * channels; i++) put_f32(f, (float)out[i]);
    }
    free(in);
    free(out);
    cava_destroy(p);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: harness <out.bin>\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "wb");
    if (!f) {
        perror("fopen");
        return 1;
    }
    fwrite("CAVAGOLD", 1, 8, f);
    put_u32(f, 1); // version
    put_u32(f, 5); // configs
    run(f, 24, 48000, 2, 1, 0.77, 50, 10000, CAVA_SCALING_LINEAR, 150);
    run(f, 10, 44100, 2, 0, 0.0, 20, 16000, CAVA_SCALING_LINEAR, 60);
    run(f, 32, 48000, 2, 1, 0.5, 100, 8000, CAVA_SCALING_DECIBEL, 60);
    run(f, 16, 48000, 1, 1, 0.77, 50, 10000, CAVA_SCALING_LINEAR, 60);
    run(f, 100, 96000, 2, 1, 0.9, 30, 20000, CAVA_SCALING_LINEAR, 40);
    fclose(f);
    return 0;
}
