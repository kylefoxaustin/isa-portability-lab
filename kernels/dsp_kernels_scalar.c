/* dsp_kernels_scalar.c - GROUND TRUTH scalar reference.
 *
 * Pure C, pure integer, no intrinsics. This defines the semantics that
 * every accelerated build (HiFi, RVV) must reproduce bit-exactly. Keep
 * this file free of any target-specific code forever - it is the oracle.
 */
#include "dsp_kernels.h"

/* ---- shared twiddle table (Q15), N=64, N/2 entries ------------------ */
static const cq15_t TW[32] = {
    { 32767,     0}, { 32609, -3212}, { 32137, -6393}, { 31356, -9512},
    { 30273,-12539}, { 28898,-15446}, { 27245,-18204}, { 25329,-20787},
    { 23170,-23170}, { 20787,-25329}, { 18204,-27245}, { 15446,-28898},
    { 12539,-30273}, {  9512,-31356}, {  6393,-32137}, {  3212,-32609},
    {     0,-32767}, { -3212,-32609}, { -6393,-32137}, { -9512,-31356},
    {-12539,-30273}, {-15446,-28898}, {-18204,-27245}, {-20787,-25329},
    {-23170,-23170}, {-25329,-20787}, {-27245,-18204}, {-28898,-15446},
    {-30273,-12539}, {-31356, -9512}, {-32137, -6393}, {-32609, -3212},
};
const cq15_t *dsp_twiddle_q15(void) { return TW; }

/* 1. FIR ------------------------------------------------------------- */
void k_fir_q15_ref(const int16_t *x, size_t n,
               const int16_t *h, size_t taps, int16_t *y) {
    for (size_t i = 0; i < n; i++) {
        int64_t acc = 0;
        for (size_t k = 0; k < taps; k++) {
            int64_t xi = (i >= k) ? (int64_t)x[i - k] : 0; /* zero history */
            acc += (int64_t)h[k] * xi;
        }
        y[i] = dsp_round_q_to_q15(acc, 15);
    }
}

/* 2. Biquad DF-I, Q14 coeffs ---------------------------------------- */
void k_biquad_q14_ref(const int16_t *x, size_t n,
                  const int16_t coeff[5], int16_t state[4], int16_t *y) {
    int16_t x1 = state[0], x2 = state[1], y1 = state[2], y2 = state[3];
    const int16_t b0 = coeff[0], b1 = coeff[1], b2 = coeff[2];
    const int16_t a1 = coeff[3], a2 = coeff[4];
    for (size_t i = 0; i < n; i++) {
        int16_t xn = x[i];
        int64_t acc = (int64_t)b0 * xn + (int64_t)b1 * x1 + (int64_t)b2 * x2
                    - (int64_t)a1 * y1 - (int64_t)a2 * y2;
        int16_t yn = dsp_round_q_to_q15(acc, 14); /* Q14 coeffs -> >>14 */
        x2 = x1; x1 = xn;
        y2 = y1; y1 = yn;
        y[i] = yn;
    }
    state[0] = x1; state[1] = x2; state[2] = y1; state[3] = y2;
}

/* 3. real dot product ------------------------------------------------ */
int64_t k_dotprod_q15_ref(const int16_t *a, const int16_t *b, size_t n,
                      int16_t *out_q15) {
    int64_t acc = 0;
    for (size_t i = 0; i < n; i++) acc += (int64_t)a[i] * b[i];
    if (out_q15) *out_q15 = dsp_round_q_to_q15(acc, 15);
    return acc;
}

/* 4. complex dot product -------------------------------------------- */
void k_cdotprod_q15_ref(const cq15_t *a, const cq15_t *b, size_t n,
                    cq15_t *out_q15) {
    int64_t are = 0, aim = 0;
    for (size_t i = 0; i < n; i++) {
        are += (int64_t)a[i].re * b[i].re - (int64_t)a[i].im * b[i].im;
        aim += (int64_t)a[i].re * b[i].im + (int64_t)a[i].im * b[i].re;
    }
    out_q15->re = dsp_round_q_to_q15(are, 15);
    out_q15->im = dsp_round_q_to_q15(aim, 15);
}

/* complex Q15 multiply, rounding >>15 (used inside FFT) */
static inline cq15_t cmul_q15(cq15_t a, cq15_t w) {
    int64_t re = (int64_t)a.re * w.re - (int64_t)a.im * w.im;
    int64_t im = (int64_t)a.re * w.im + (int64_t)a.im * w.re;
    cq15_t r;
    r.re = dsp_round_q_to_q15(re, 15);
    r.im = dsp_round_q_to_q15(im, 15);
    return r;
}

/* 5. radix-2 DIT FFT, N=64, in place, >>1 per stage ------------------ */
void k_fft64_q15_ref(cq15_t *data) {
    const int N = 64, LOG2N = 6;
    /* bit-reversal permutation */
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { cq15_t t = data[i]; data[i] = data[j]; data[j] = t; }
    }
    /* stages */
    for (int s = 1; s <= LOG2N; s++) {
        int m = 1 << s;          /* butterfly span   */
        int half = m >> 1;
        int tw_step = N / m;     /* stride into TW[] */
        for (int k = 0; k < N; k += m) {
            for (int j = 0; j < half; j++) {
                cq15_t w = TW[j * tw_step];
                cq15_t a = data[k + j];
                cq15_t b = cmul_q15(data[k + j + half], w);
                /* >>1 scaling to prevent overflow, arithmetic shift */
                cq15_t up, dn;
                up.re = (int16_t)((a.re + b.re) >> 1);
                up.im = (int16_t)((a.im + b.im) >> 1);
                dn.re = (int16_t)((a.re - b.re) >> 1);
                dn.im = (int16_t)((a.im - b.im) >> 1);
                data[k + j]        = up;
                data[k + j + half] = dn;
            }
        }
    }
}

/* 6. NN requantize int32 -> int8 ------------------------------------- */
void k_requantize_s8_ref(const int32_t *acc, size_t n,
                     int32_t multiplier_q31, int shift, int32_t zero_point,
                     int8_t *out) {
    for (size_t i = 0; i < n; i++) {
        /* (acc * mult) >> 31, then >> shift, round half-up, + zp, sat */
        int64_t prod = (int64_t)acc[i] * multiplier_q31;
        int64_t scaled = prod >> 31;
        int64_t rnd = (shift > 0) ? ((scaled + ((int64_t)1 << (shift - 1))) >> shift)
                                  : (scaled << (-shift));
        out[i] = dsp_sat8((int32_t)(rnd + zero_point));
    }
}

/* 7. int8 GEMM ------------------------------------------------------- */
void k_matmul_s8_ref(const int8_t *A, const int8_t *B, int32_t *C,
                 int M, int K, int N) {
    for (int i = 0; i < M; i++)
        for (int j = 0; j < N; j++) {
            int32_t acc = 0;
            for (int k = 0; k < K; k++)
                acc += (int32_t)A[i * K + k] * B[k * N + j];
            C[i * N + j] = acc;
        }
}
