/*
 * portable/probes/p_dsp.c - a realistic float DSP kernel (the codegen story).
 *
 * An 8-tap symmetric FIR low-pass filter over a 256-sample block. This is
 * bread-and-butter embedded DSP and a vectorizable multiply-accumulate loop,
 * so it's where `make compare-codegen` is most interesting: RISC-V lowers the
 * inner MAC to RVV (vfmacc), while Cortex-M7 stays scalar (no SIMD unit) -
 * same source, very different machine code.
 *
 * Determinism: coefficients are dyadic (k/32) and samples are small integers,
 * so every product and partial sum is an exact integer < 2^24 and the final
 * scale is a division by a power of two. The result is therefore bit-exact
 * regardless of whether the compiler fuses the multiply-add - so it stays
 * comparable across targets (expected: behavioral match, codegen divergence).
 */
#include <stdint.h>
#include "probe.h"

#define NTAPS 8
#define NSAMP 256
#define NOUT  (NSAMP - NTAPS + 1)

static uint32_t k_fir_lowpass(void)
{
    /* symmetric low-pass, coefficients sum to 32 -> normalize by 1/32 (exact) */
    static const float coeff[NTAPS] = { 1, 3, 5, 7, 7, 5, 3, 1 };
    float x[NSAMP], y[NOUT];

    for (int i = 0; i < NSAMP; i++)
        x[i] = (float)(((i * 5 + 3) % 17) - 8);   /* small ints in [-8, 8] */

    for (int i = 0; i < NOUT; i++) {
        float acc = 0.0f;
        for (int k = 0; k < NTAPS; k++)
            acc += coeff[k] * x[i + k];            /* exact-integer MAC */
        y[i] = acc * (1.0f / 32.0f);               /* exact: /power-of-two */
    }
    return probe_fnv1a(y, sizeof y);
}

REGISTER_PROBE("K15 fir_lowpass ", k_fir_lowpass);
