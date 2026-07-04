/* dsp_kernels_baseline.c - public k_* == reference.
 *
 * This is the implementation the scalar / dc233c / cortex-m7 legs link.
 * It exists so the harness always has k_* symbols to call; for these
 * legs "under test" == "reference", which is exactly right: a scalar
 * controller core has no DSP datapath to exercise, so its job in this
 * repo is to prove the harness/plumbing and provide a portability
 * baseline, not to accelerate anything.
 */
#include "dsp_kernels.h"

void k_fir_q15(const int16_t *x, size_t n, const int16_t *h, size_t t, int16_t *y)
{ k_fir_q15_ref(x, n, h, t, y); }

void k_biquad_q14(const int16_t *x, size_t n, const int16_t c[5], int16_t s[4], int16_t *y)
{ k_biquad_q14_ref(x, n, c, s, y); }

int64_t k_dotprod_q15(const int16_t *a, const int16_t *b, size_t n, int16_t *o)
{ return k_dotprod_q15_ref(a, b, n, o); }

void k_cdotprod_q15(const cq15_t *a, const cq15_t *b, size_t n, cq15_t *o)
{ k_cdotprod_q15_ref(a, b, n, o); }

void k_fft64_q15(cq15_t *d) { k_fft64_q15_ref(d); }

void k_requantize_s8(const int32_t *a, size_t n, int32_t m, int s, int32_t z, int8_t *o)
{ k_requantize_s8_ref(a, n, m, s, z, o); }

void k_matmul_s8(const int8_t *A, const int8_t *B, int32_t *C, int M, int K, int N)
{ k_matmul_s8_ref(A, B, C, M, K, N); }
