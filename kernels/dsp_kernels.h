/* dsp_kernels.h - shared DSP kernel API for isa-portability-lab
 *
 * This is the CONTRACT. Every implementation - the scalar reference
 * (ground truth), the HiFi intrinsic build, the RVV intrinsic build -
 * exposes exactly these signatures and must produce bit-identical output
 * for identical input. All kernels are fixed-point / integer: that is
 * deliberate. Floating-point bit-exactness across ISAs is exactly the
 * thing this lab's portable-C legs already probe and where FMA
 * contraction / rounding legitimately diverge. Fixed-point integer
 * kernels give an unambiguous golden vector, and it is also how real
 * HiFi/Vision kernels actually run.
 *
 * Fixed-point formats used:
 *   Q15  : int16_t, scale 2^15, range [-1, 1)
 *   Q14  : int16_t, scale 2^14, range [-2, 2)   (biquad coeffs need |c|<2)
 *   Q31  : int32_t
 *   acc  : int64_t (64-bit accumulators, matching real DSP MAC datapaths)
 */
#ifndef DSP_KERNELS_H
#define DSP_KERNELS_H

#include <stdint.h>
#include <stddef.h>

/* ---- saturation / rounding helpers (shared, header-inline) ---------- */
static inline int16_t dsp_sat16(int64_t v) {
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}
static inline int8_t dsp_sat8(int32_t v) {
    if (v >  127) return  127;
    if (v < -128) return -128;
    return (int8_t)v;
}
/* round a QN accumulator down to Q15 (add half-LSB then arithmetic shift) */
static inline int16_t dsp_round_q_to_q15(int64_t acc, int shift) {
    int64_t half = (int64_t)1 << (shift - 1);
    return dsp_sat16((acc + half) >> shift);
}

/* ---- complex Q15 sample: interleaved {re, im} ----------------------- */
typedef struct { int16_t re; int16_t im; } cq15_t;

/* ====================================================================
 *  KERNEL SET
 *
 *  Two symbol families:
 *    k_*_ref  - GROUND TRUTH, scalar, in dsp_kernels_scalar.c. Always
 *               compiled and linked. Never touched by target code. The
 *               golden vectors are generated from these.
 *    k_*      - UNDER TEST. Implemented by exactly one per-target file:
 *               dsp_kernels_baseline.c (forwards to _ref; used by the
 *               scalar / dc233c / cortex-m7 legs), dsp_kernels_hifi.c
 *               (HiFi intrinsics), or dsp_kernels_rvv.c (RVV intrinsics).
 *               The accelerator files fall back to _ref when their
 *               intrinsic macro is undefined, so they build green
 *               everywhere and "light up" only when the real toolchain
 *               defines HAVE_HIFI / HAVE_RVV.
 *
 *  The runtime harness runs k_* and diffs against golden; equivalence ==
 *  "the accelerated datapath reproduces the reference bit-for-bit."
 * ==================================================================== */

/* ---- ground-truth reference symbols (always available) ------------- */
void    k_fir_q15_ref(const int16_t*, size_t, const int16_t*, size_t, int16_t*);
void    k_biquad_q14_ref(const int16_t*, size_t, const int16_t[5], int16_t[4], int16_t*);
int64_t k_dotprod_q15_ref(const int16_t*, const int16_t*, size_t, int16_t*);
void    k_cdotprod_q15_ref(const cq15_t*, const cq15_t*, size_t, cq15_t*);
void    k_fft64_q15_ref(cq15_t*);
void    k_requantize_s8_ref(const int32_t*, size_t, int32_t, int, int32_t, int8_t*);
void    k_matmul_s8_ref(const int8_t*, const int8_t*, int32_t*, int, int, int);

/* 1. FIR filter. h[] Q15 taps (newest-first convolution), x[] Q15 input,
 *    y[] Q15 output. 64-bit accumulate, round back to Q15. */
void k_fir_q15(const int16_t *x, size_t n,
               const int16_t *h, size_t taps,
               int16_t *y);

/* 2. Biquad IIR, Direct Form I, Q14 coeffs {b0,b1,b2,a1,a2}.
 *    state[] length 4 = {x[n-1], x[n-2], y[n-1], y[n-2]} in Q15,
 *    updated in place. */
void k_biquad_q14(const int16_t *x, size_t n,
                  const int16_t coeff[5], int16_t state[4],
                  int16_t *y);

/* 3. Real dot product, Q15 * Q15 -> Q31, returned as raw int64 accumulate
 *    AND rounded Q15. out_q15 may be NULL. */
int64_t k_dotprod_q15(const int16_t *a, const int16_t *b, size_t n,
                      int16_t *out_q15);

/* 4. Complex dot product (conjugate NOT applied), Q15 -> rounded Q15. */
void k_cdotprod_q15(const cq15_t *a, const cq15_t *b, size_t n,
                    cq15_t *out_q15);

/* 5. Radix-2 DIT FFT, N=64, in-place, Q15, per-stage >>1 scaling.
 *    Twiddles supplied by dsp_twiddle_q15() so every impl uses the
 *    identical table. */
void k_fft64_q15(cq15_t *data /* length 64, in/out */);

/* 6. NN requantize: int32 accumulator -> int8, using a Q31 multiplier
 *    and right shift, then add zero-point, saturate. This is the exact
 *    integer op an NPU/Vision-DSP requant stage performs. */
void k_requantize_s8(const int32_t *acc, size_t n,
                     int32_t multiplier_q31, int shift, int32_t zero_point,
                     int8_t *out);

/* 7. int8 x int8 -> int32 GEMM, C[M][N] = A[M][K] * B[K][N],
 *    row-major, 32-bit accumulate. Maps to Vision/AI-DSP MAC arrays. */
void k_matmul_s8(const int8_t *A, const int8_t *B, int32_t *C,
                 int M, int K, int N);

/* shared twiddle table accessor (defined once, in dsp_kernels_scalar.c) */
const cq15_t *dsp_twiddle_q15(void); /* length 32 = N/2 for N=64 */

#endif /* DSP_KERNELS_H */
