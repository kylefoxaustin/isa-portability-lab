/* test_vectors.h - deterministic, portable input generation.
 *
 * Every implementation generates its inputs from this same LCG with the
 * same seed, so inputs are bit-identical across scalar/HiFi/RVV WITHOUT
 * checking in large input arrays. Only the OUTPUTS are checked in as
 * golden vectors. The LCG is the classic Numerical Recipes constants;
 * we take bits [30:15] of the state to land in int16 range with a mild
 * bias toward smaller magnitudes (keeps fixed-point kernels off the
 * saturation rails so the golden vectors exercise the arithmetic, not
 * just the clamps).
 */
#ifndef TEST_VECTORS_H
#define TEST_VECTORS_H

#include <stdint.h>
#include <stddef.h>
#include "dsp_kernels.h"

typedef struct { uint32_t s; } lcg_t;

static inline void lcg_seed(lcg_t *g, uint32_t seed) { g->s = seed; }

static inline uint32_t lcg_next(lcg_t *g) {
    g->s = g->s * 1664525u + 1013904223u;
    return g->s;
}

/* signed 16-bit sample in roughly [-8192, 8192) (Q15 ~ [-0.25, 0.25)) */
static inline int16_t lcg_q15(lcg_t *g) {
    int32_t v = (int32_t)((lcg_next(g) >> 17) & 0x3FFF) - 0x2000;
    return (int16_t)v;
}

/* signed 8-bit sample in [-64, 64) */
static inline int8_t lcg_s8(lcg_t *g) {
    int32_t v = (int32_t)((lcg_next(g) >> 24) & 0x7F) - 0x40;
    return (int8_t)v;
}

/* int32 accumulator-ish value in [-2^20, 2^20) for requant tests */
static inline int32_t lcg_s32(lcg_t *g) {
    return (int32_t)(lcg_next(g) & 0x1FFFFF) - 0x100000;
}

static inline void fill_q15(lcg_t *g, int16_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) p[i] = lcg_q15(g);
}
static inline void fill_s8(lcg_t *g, int8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) p[i] = lcg_s8(g);
}
static inline void fill_cq15(lcg_t *g, cq15_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) { p[i].re = lcg_q15(g); p[i].im = lcg_q15(g); }
}
static inline void fill_s32(lcg_t *g, int32_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) p[i] = lcg_s32(g);
}

/* Canonical seeds per kernel so runs are independent & reproducible. */
#define SEED_FIR      0x1000u
#define SEED_BIQUAD   0x2000u
#define SEED_DOT      0x3000u
#define SEED_CDOT     0x4000u
#define SEED_FFT      0x5000u
#define SEED_REQUANT  0x6000u
#define SEED_MATMUL   0x7000u

#endif /* TEST_VECTORS_H */
