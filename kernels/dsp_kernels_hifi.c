/* dsp_kernels_hifi.c - Cadence HiFi intrinsic implementations.
 * =====================================================================
 *  >>> THIS IS THE FILE ACC CLAUDE FILLS IN, INSIDE THE FIREWALL. <<<
 * =====================================================================
 *
 * Out here in the wild, HAVE_HIFI is undefined: every kernel forwards to
 * the scalar reference so the whole repo builds and the harness passes
 * green. That green baseline is the proof that the plumbing is correct
 * BEFORE licensed-tool time is spent.
 *
 * Inside the firewall, the xtensa leg's Makefile is built with the HiFi
 * toolchain and adds -DHAVE_HIFI. ACC Claude then replaces each TODO
 * block with a real HiFi intrinsic implementation. Rules for that work:
 *
 *   1. Include the correct HiFi intrinsics header for the licensed core,
 *      e.g.  #include <xtensa/tie/xt_hifi<N>.h>  (N per the core config).
 *   2. Match the reference's fixed-point semantics EXACTLY - same Q
 *      formats, same 64-bit accumulate, same rounding (add half-LSB then
 *      arithmetic shift), same saturation. The equivalence bar is
 *      bit-exact against kernels/golden/<name>.txt, NOT "close enough".
 *   3. If HiFi's native rounding/saturation mode differs from the
 *      reference, reconcile it explicitly (adjust the reference AND
 *      regenerate golden via harness/gen_golden.c, or match HiFi's mode
 *      in the reference) - do not let the two drift silently.
 *   4. Keep each kernel's public signature identical to dsp_kernels.h.
 *   5. Record cycle counts from the ISS (xt-run --mem_model or the
 *      profiling build) into the metrics sink the harness expects.
 *
 * Each kernel below is annotated with the HiFi datapath it should target.
 */
#include "dsp_kernels.h"

#ifdef HAVE_HIFI
/* #include <xtensa/tie/xt_hifi4.h>   // or hifi3/hifi5 per licensed core */
#endif

/* 1. FIR  -> HiFi has fused multiply-accumulate over ae_int16x4 vectors
 *            (AE_MULAAAAQ16 family); 4- or 8-wide taps per issue. */
void k_fir_q15(const int16_t *x, size_t n, const int16_t *h, size_t t, int16_t *y) {
#ifdef HAVE_HIFI
    /* TODO(ACC): HiFi FIR. Load x/h as ae_int16x4, MAC into ae_int64,
     * round+sat to Q15 via AE_ROUND16X4F32 / AE_SAT... Match _ref. */
    k_fir_q15_ref(x, n, h, t, y); /* placeholder until implemented */
#else
    k_fir_q15_ref(x, n, h, t, y);
#endif
}

/* 2. Biquad -> HiFi biquad primitives / DF-I MAC chain. Watch the Q14
 *              coeff shift (>>14, not >>15). */
void k_biquad_q14(const int16_t *x, size_t n, const int16_t c[5], int16_t s[4], int16_t *y) {
#ifdef HAVE_HIFI
    /* TODO(ACC): HiFi biquad. */
    k_biquad_q14_ref(x, n, c, s, y);
#else
    k_biquad_q14_ref(x, n, c, s, y);
#endif
}

/* 3. dot product -> AE_MULAAAAQ16 accumulate into ae_int64. */
int64_t k_dotprod_q15(const int16_t *a, const int16_t *b, size_t n, int16_t *o) {
#ifdef HAVE_HIFI
    /* TODO(ACC): HiFi dot product; return raw 64-bit accumulate. */
    return k_dotprod_q15_ref(a, b, n, o);
#else
    return k_dotprod_q15_ref(a, b, n, o);
#endif
}

/* 4. complex dot -> HiFi has native complex MAC (AE_MULAC / AE_MULZAAFD). */
void k_cdotprod_q15(const cq15_t *a, const cq15_t *b, size_t n, cq15_t *o) {
#ifdef HAVE_HIFI
    /* TODO(ACC): HiFi complex MAC. */
    k_cdotprod_q15_ref(a, b, n, o);
#else
    k_cdotprod_q15_ref(a, b, n, o);
#endif
}

/* 5. FFT -> HiFi FFT butterfly intrinsics (AE_ADDANDSUB, complex mul).
 *           MUST reproduce the reference's per-stage >>1 scaling and the
 *           same twiddle table (dsp_twiddle_q15). */
void k_fft64_q15(cq15_t *d) {
#ifdef HAVE_HIFI
    /* TODO(ACC): HiFi radix-2 DIT, 64pt, >>1/stage. Bit-exact vs _ref. */
    k_fft64_q15_ref(d);
#else
    k_fft64_q15_ref(d);
#endif
}

/* 6. requantize -> AE_MULFP32X2RAS / variable-shift + saturate to int8. */
void k_requantize_s8(const int32_t *a, size_t n, int32_t m, int s, int32_t z, int8_t *o) {
#ifdef HAVE_HIFI
    /* TODO(ACC): HiFi requant; match the >>31 then >>shift round chain. */
    k_requantize_s8_ref(a, n, m, s, z, o);
#else
    k_requantize_s8_ref(a, n, m, s, z, o);
#endif
}

/* 7. int8 GEMM -> HiFi/Vision int8 MAC. (On a HiFi DSP this is a MAC
 *                 loop; on Vision it maps to wide SIMD - note which core.) */
void k_matmul_s8(const int8_t *A, const int8_t *B, int32_t *C, int M, int K, int N) {
#ifdef HAVE_HIFI
    /* TODO(ACC): HiFi int8 GEMM. */
    k_matmul_s8_ref(A, B, C, M, K, N);
#else
    k_matmul_s8_ref(A, B, C, M, K, N);
#endif
}
