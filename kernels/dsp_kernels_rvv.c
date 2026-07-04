/* dsp_kernels_rvv.c - RISC-V Vector (RVV 1.0) intrinsic implementations.
 *
 * This is the OPEN-SOURCE-REACHABLE accelerator side, and it is what makes
 * the DSP comparison fair: HiFi intrinsics vs. RVV intrinsics, not HiFi
 * vs. auto-vectorized portable C. Fill these on your riscv leg's real
 * toolchain (the one your existing rv64 leg already uses).
 *
 * Same rules as the HiFi file: bit-exact against kernels/golden/<name>.txt,
 * identical Q formats, identical 64-bit accumulate + rounding + saturation.
 *
 * >>> VERIFY THE EXEMPLAR BELOW ON YOUR ACTUAL riscv TOOLCHAIN. <<<
 * It targets RVV 1.0 (v extension, -march=...v). Intrinsic names follow
 * the ratified __riscv_* API. VLEN-agnostic via vsetvl strip-mining.
 * The scalar reduction fallback is only compiled when HAVE_RVV is unset,
 * so this file is green on non-vector legs too.
 */
#include "dsp_kernels.h"

#ifdef HAVE_RVV
#include <riscv_vector.h>
#endif

/* ---- 3. dot product: WORKED EXEMPLAR ------------------------------- *
 * Q15 * Q15 widened to i32, widening-reduced into an i64 accumulator.
 * Reproduces k_dotprod_q15_ref: raw 64-bit sum, then round>>15 to Q15. */
int64_t k_dotprod_q15(const int16_t *a, const int16_t *b, size_t n, int16_t *o) {
#ifdef HAVE_RVV
    vint64m1_t vacc = __riscv_vmv_s_x_i64m1(0, 1);
    size_t vl;
    for (size_t i = 0; i < n; i += vl) {
        vl = __riscv_vsetvl_e16m1(n - i);
        vint16m1_t va = __riscv_vle16_v_i16m1(a + i, vl);
        vint16m1_t vb = __riscv_vle16_v_i16m1(b + i, vl);
        /* widen-multiply i16 x i16 -> i32 */
        vint32m2_t vp = __riscv_vwmul_vv_i32m2(va, vb, vl);
        /* widening reduce i32 -> i64, chaining the running accumulator */
        vacc = __riscv_vwredsum_vs_i32m2_i64m1(vp, vacc, vl);
    }
    int64_t acc = __riscv_vmv_x_s_i64m1_i64(vacc);
    if (o) *o = dsp_round_q_to_q15(acc, 15);
    return acc;
#else
    return k_dotprod_q15_ref(a, b, n, o);
#endif
}

/* ---- everything below: seams, same pattern as the exemplar --------- */

/* 1. FIR -> strip-mined vwmul + running i64 vwredsum per output sample,
 *           or a vslide-based sliding window. Match _ref zero-history. */
void k_fir_q15(const int16_t *x, size_t n, const int16_t *h, size_t t, int16_t *y) {
#ifdef HAVE_RVV
    /* TODO: RVV FIR. Placeholder keeps build correct until implemented. */
    k_fir_q15_ref(x, n, h, t, y);
#else
    k_fir_q15_ref(x, n, h, t, y);
#endif
}

/* 2. Biquad is recurrent (y[n] depends on y[n-1]); vectorize across
 *    channels/blocks, not within one channel. Scalar-equivalent per lane. */
void k_biquad_q14(const int16_t *x, size_t n, const int16_t c[5], int16_t s[4], int16_t *y) {
#ifdef HAVE_RVV
    /* TODO: RVV biquad (block/channel parallel). */
    k_biquad_q14_ref(x, n, c, s, y);
#else
    k_biquad_q14_ref(x, n, c, s, y);
#endif
}

/* 4. complex dot -> deinterleave re/im (vlseg2e16), two widening MACs. */
void k_cdotprod_q15(const cq15_t *a, const cq15_t *b, size_t n, cq15_t *o) {
#ifdef HAVE_RVV
    /* TODO: RVV complex dot via segmented loads. */
    k_cdotprod_q15_ref(a, b, n, o);
#else
    k_cdotprod_q15_ref(a, b, n, o);
#endif
}

/* 5. FFT -> vectorize butterflies within a stage (vlseg for stride).
 *           Reproduce >>1/stage scaling and the shared twiddle table. */
void k_fft64_q15(cq15_t *d) {
#ifdef HAVE_RVV
    /* TODO: RVV radix-2 FFT. */
    k_fft64_q15_ref(d);
#else
    k_fft64_q15_ref(d);
#endif
}

/* 6. requantize -> vwmul by multiplier, variable shift, saturating
 *                  narrow i32 -> i8 (vnclip family). */
void k_requantize_s8(const int32_t *a, size_t n, int32_t m, int s, int32_t z, int8_t *o) {
#ifdef HAVE_RVV
    /* TODO: RVV requantize with vnclip saturation. */
    k_requantize_s8_ref(a, n, m, s, z, o);
#else
    k_requantize_s8_ref(a, n, m, s, z, o);
#endif
}

/* 7. int8 GEMM -> vwmul i8->i16, widening reduce to i32 per (i,j), or
 *                 blocked with vmacc. */
void k_matmul_s8(const int8_t *A, const int8_t *B, int32_t *C, int M, int K, int N) {
#ifdef HAVE_RVV
    /* TODO: RVV int8 GEMM. */
    k_matmul_s8_ref(A, B, C, M, K, N);
#else
    k_matmul_s8_ref(A, B, C, M, K, N);
#endif
}
