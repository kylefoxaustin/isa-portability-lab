/*
 * accelerator_intrinsics.h
 *
 * The seam between application code and the (forthcoming) accelerator ISA.
 *
 * STATUS: STUBBED. Every intrinsic falls back to a software implementation
 * so code targeting the accelerator compiles and runs in QEMU today, before
 * the ISA is finalized.
 *
 * When the accelerator ISA is available, replace each body with inline
 * assembly, e.g.:
 *
 *     static inline float accel_sinf(float x) {
 *         float r;
 *         asm volatile("accel.sin %0, %1" : "=f"(r) : "f"(x));
 *         return r;
 *     }
 *
 * Keep the function signatures stable - everything above this header
 * (user code, generated kernels, the visual tool) depends on them.
 */

#ifndef ACCELERATOR_INTRINSICS_H
#define ACCELERATOR_INTRINSICS_H

#define ACCEL_INTRINSICS_VERSION "0.1-stub"

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Software fallbacks (used until ISA inline-asm replaces them).
 *
 * NOTE: we don't pull in <math.h> here because picolibc/newlib transcendentals
 * are large and we want this header to work in -nostdlib builds. The fallback
 * uses CORDIC-style or polynomial approximations sufficient for bring-up.
 * Replace with real implementations when accuracy matters - or just wait
 * for the accelerator.
 * ------------------------------------------------------------------------- */

/* Crude software sinf for bring-up - NOT accurate, NOT IEEE-correct.
 * 5-term Taylor series around 0, range-reduced into [-pi, pi].
 * Replace with hardware accel.sin once available. */
static inline float __accel_soft_sinf(float x)
{
    /* Range reduce to [-pi, pi] */
    const float PI    = 3.14159265358979323846f;
    const float TWOPI = 6.28318530717958647692f;
    while (x >  PI) x -= TWOPI;
    while (x < -PI) x += TWOPI;

    float x2 = x * x;
    /* sin x ~= x - x^3/6 + x^5/120 - x^7/5040 + x^9/362880 */
    return x * (1.0f
              - x2 * (1.0f/6.0f
              - x2 * (1.0f/120.0f
              - x2 * (1.0f/5040.0f
              - x2 *  1.0f/362880.0f))));
}

/* -------------------------------------------------------------------------
 * Public intrinsic API
 * ------------------------------------------------------------------------- */

/** Single-precision sine. Argument in radians. */
static inline float accel_sinf(float x)
{
    /* TODO: replace body with: asm volatile("accel.sin %0,%1" : "=f"(r):"f"(x)); return r; */
    return __accel_soft_sinf(x);
}

/** Single-precision cosine. Argument in radians. */
static inline float accel_cosf(float x)
{
    /* TODO: replace with hardware accel.cos */
    const float PI_2 = 1.57079632679489661923f;
    return __accel_soft_sinf(x + PI_2);
}

/** Combined sin/cos. Common in Park/Clarke transforms - having both in
 *  one accelerator op saves a full pipeline pass. */
static inline void accel_sincosf(float x, float *s, float *c)
{
    /* TODO: replace with single hardware accel.sincos */
    *s = accel_sinf(x);
    *c = accel_cosf(x);
}

/** Two-argument arctangent. */
static inline float accel_atan2f(float y, float x)
{
    /* TODO: replace with hardware accel.atan2.
     * Stub: returns 0; do not rely on this in QEMU testing until filled in. */
    (void)y; (void)x;
    return 0.0f;
}

/** Square root. */
static inline float accel_sqrtf(float x)
{
    /* RISC-V F extension already provides fsqrt.s, so just use it.
     * Keep the wrapper so the visual tool can target accel_sqrtf and the
     * codegen can later swap to a different latency profile if the
     * accelerator has a faster variant. */
    float r;
    asm volatile("fsqrt.s %0, %1" : "=f"(r) : "f"(x));
    return r;
}

/** Reciprocal square root. */
static inline float accel_rsqrtf(float x)
{
    /* TODO: replace with hardware accel.rsqrt if provided */
    return 1.0f / accel_sqrtf(x);
}

#endif /* ACCELERATOR_INTRINSICS_H */
