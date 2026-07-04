/* dsp_kernels_rvv.c - RISC-V Vector (RVV 1.0) intrinsic implementations.
 *
 * This is the OPEN-SOURCE-REACHABLE accelerator side, and it is what makes
 * the DSP comparison fair: HiFi intrinsics vs. RVV intrinsics, not HiFi
 * vs. auto-vectorized portable C. Verified on the repo's rv64 leg
 * (riscv-none-elf-gcc 14.2, -march=...v, VLEN=128) under QEMU, bit-exact
 * against kernels/golden/<name>.txt.
 *
 * Bit-exactness principle: every kernel keeps the reference's Q formats and
 * 64-bit integer accumulate. Integer addition is associative + commutative
 * and none of these sums overflow int64, so a vector widening-reduce
 * (vwredsum) yields the *identical* accumulator as the scalar reference loop
 * regardless of reduction order - then we finalize with the SAME shared
 * round/saturate helpers. (This is exactly why the kernels are fixed-point:
 * float reductions would legitimately diverge; integer ones cannot.)
 *
 * The scalar reduction fallback is compiled when HAVE_RVV is unset, so this
 * file is green on non-vector legs too.
 */
#include "dsp_kernels.h"

#ifdef HAVE_RVV
#include <riscv_vector.h>

/* ---- shared primitive: widening i16 dot product -> raw int64 ------------ *
 * acc = sum_i a[i]*b[i], exact 64-bit. VLEN-agnostic strip-mining; the i64
 * accumulator is chained across strips so the result is independent of VLEN. */
static int64_t rvv_dot_i16(const int16_t *a, const int16_t *b, size_t n)
{
    vint64m1_t vacc = __riscv_vmv_s_x_i64m1(0, 1);
    size_t vl;
    for (size_t i = 0; i < n; i += vl) {
        vl = __riscv_vsetvl_e16m1(n - i);
        vint16m1_t va = __riscv_vle16_v_i16m1(a + i, vl);
        vint16m1_t vb = __riscv_vle16_v_i16m1(b + i, vl);
        vint32m2_t vp = __riscv_vwmul_vv_i32m2(va, vb, vl);
        vacc = __riscv_vwredsum_vs_i32m2_i64m1(vp, vacc, vl);
    }
    return __riscv_vmv_x_s_i64m1_i64(vacc);
}
#endif

/* ---- 3. dot product: WORKED EXEMPLAR ------------------------------------ */
int64_t k_dotprod_q15(const int16_t *a, const int16_t *b, size_t n, int16_t *o)
{
#ifdef HAVE_RVV
    int64_t acc = rvv_dot_i16(a, b, n);
    if (o) *o = dsp_round_q_to_q15(acc, 15);
    return acc;
#else
    return k_dotprod_q15_ref(a, b, n, o);
#endif
}

/* ---- 1. FIR ------------------------------------------------------------- *
 * y[i] = sum_k h[k]*x[i-k], zero history for i-k<0, round to Q15. We pad x
 * with (taps-1) leading zeros and reverse h once, turning each output into a
 * contiguous widening dot: y[i] = sum_m hr[m]*xp[i+m]. Matches _ref exactly. */
void k_fir_q15(const int16_t *x, size_t n, const int16_t *h, size_t taps, int16_t *y)
{
#ifdef HAVE_RVV
    if (taps == 0) { for (size_t i = 0; i < n; i++) y[i] = 0; return; }
    int16_t hr[64];          /* reversed taps (taps is tiny in practice) */
    int16_t xp[512];         /* (taps-1) zero pad + n samples             */
    if (taps > 64 || taps - 1 + n > 512) { k_fir_q15_ref(x, n, h, taps, y); return; }
    for (size_t k = 0; k < taps; k++) hr[k] = h[taps - 1 - k];
    for (size_t p = 0; p < taps - 1; p++) xp[p] = 0;
    for (size_t i = 0; i < n; i++) xp[taps - 1 + i] = x[i];
    for (size_t i = 0; i < n; i++) {
        int64_t acc = rvv_dot_i16(hr, xp + i, taps);
        y[i] = dsp_round_q_to_q15(acc, 15);
    }
#else
    k_fir_q15_ref(x, n, h, taps, y);
#endif
}

/* ---- 2. Biquad DF-I, Q14 ------------------------------------------------ *
 * Recurrent (y[n] depends on y[n-1]) so the time loop stays scalar, but each
 * sample's 5-term MAC is done as two vector dots (b-part minus a-part), which
 * reproduces the reference's `+b.. -a..` split without negating coeffs. */
void k_biquad_q14(const int16_t *x, size_t n, const int16_t c[5], int16_t s[4], int16_t *y)
{
#ifdef HAVE_RVV
    int16_t x1 = s[0], x2 = s[1], y1 = s[2], y2 = s[3];
    const int16_t cb[3] = { c[0], c[1], c[2] };      /* b0,b1,b2 */
    const int16_t ca[2] = { c[3], c[4] };            /* a1,a2    */
    for (size_t i = 0; i < n; i++) {
        int16_t xn = x[i];
        const int16_t sb[3] = { xn, x1, x2 };
        const int16_t sa[2] = { y1, y2 };
        int64_t acc = rvv_dot_i16(cb, sb, 3) - rvv_dot_i16(ca, sa, 2);
        int16_t yn = dsp_round_q_to_q15(acc, 14);
        x2 = x1; x1 = xn;
        y2 = y1; y1 = yn;
        y[i] = yn;
    }
    s[0] = x1; s[1] = x2; s[2] = y1; s[3] = y2;
#else
    k_biquad_q14_ref(x, n, c, s, y);
#endif
}

/* ---- 4. complex dot ----------------------------------------------------- *
 * re = sum(a.re*b.re) - sum(a.im*b.im);  im = sum(a.re*b.im) + sum(a.im*b.re).
 * sum-of-products split exactly (integer), so four real widening dots. */
void k_cdotprod_q15(const cq15_t *a, const cq15_t *b, size_t n, cq15_t *o)
{
#ifdef HAVE_RVV
    int16_t are[256], aim[256], bre[256], bim[256];
    if (n > 256) { k_cdotprod_q15_ref(a, b, n, o); return; }
    for (size_t i = 0; i < n; i++) {
        are[i] = a[i].re; aim[i] = a[i].im;
        bre[i] = b[i].re; bim[i] = b[i].im;
    }
    int64_t re = rvv_dot_i16(are, bre, n) - rvv_dot_i16(aim, bim, n);
    int64_t im = rvv_dot_i16(are, bim, n) + rvv_dot_i16(aim, bre, n);
    o->re = dsp_round_q_to_q15(re, 15);
    o->im = dsp_round_q_to_q15(im, 15);
#else
    k_cdotprod_q15_ref(a, b, n, o);
#endif
}

/* ---- 5. FFT ------------------------------------------------------------- *
 * Radix-2 DIT, N=64, >>1 per stage. Bit-reversal stays scalar (pure data
 * movement). Within a stage, for a fixed butterfly index j the twiddle
 * w=TW[j*tw_step] is CONSTANT across all N/m groups, whose (a,b) pairs sit at
 * stride m - so each (stage,j) is a strided complex-segment vector op:
 *   b' = cmul_q15(b, w)   [vwmul.vx + vnclip round>>15 == dsp_round_q_to_q15]
 *   up = (a+b')>>1        [vwadd + vnsra.wi 1, truncating cast, always in range]
 *   dn = (a-b')>>1
 * Reproduces the reference's twiddle table, rounding and scaling bit-exactly. */
void k_fft64_q15(cq15_t *d)
{
#ifdef HAVE_RVV
    const int N = 64, LOG2N = 6;
    /* bit-reversal permutation (identical to _ref) */
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { cq15_t t = d[i]; d[i] = d[j]; d[j] = t; }
    }
    const cq15_t *TW = dsp_twiddle_q15();
    int16_t *base = (int16_t *)d;                 /* interleaved re,im */
    for (int s = 1; s <= LOG2N; s++) {
        int m = 1 << s, half = m >> 1, tw_step = N / m, L = N / m;
        ptrdiff_t stride = (ptrdiff_t)m * (ptrdiff_t)sizeof(cq15_t);
        for (int jj = 0; jj < half; jj++) {
            int16_t wr = TW[jj * tw_step].re, wi = TW[jj * tw_step].im;
            size_t vl;
            for (int off = 0; off < L; off += (int)vl) {
                vl = __riscv_vsetvl_e16m1((size_t)(L - off));
                int16_t *pa = base + 2 * (jj + off * m);
                int16_t *pb = base + 2 * (jj + half + off * m);
                vint16m1x2_t va = __riscv_vlsseg2e16_v_i16m1x2(pa, stride, vl);
                vint16m1x2_t vb = __riscv_vlsseg2e16_v_i16m1x2(pb, stride, vl);
                vint16m1_t ar = __riscv_vget_v_i16m1x2_i16m1(va, 0);
                vint16m1_t ai = __riscv_vget_v_i16m1x2_i16m1(va, 1);
                vint16m1_t br = __riscv_vget_v_i16m1x2_i16m1(vb, 0);
                vint16m1_t bi = __riscv_vget_v_i16m1x2_i16m1(vb, 1);
                /* b' = cmul_q15(b, w): round(b.re*w.re - b.im*w.im, 15), etc. */
                vint32m2_t tre = __riscv_vsub_vv_i32m2(
                    __riscv_vwmul_vx_i32m2(br, wr, vl),
                    __riscv_vwmul_vx_i32m2(bi, wi, vl), vl);
                vint32m2_t tim = __riscv_vadd_vv_i32m2(
                    __riscv_vwmul_vx_i32m2(br, wi, vl),
                    __riscv_vwmul_vx_i32m2(bi, wr, vl), vl);
                vint16m1_t bpr = __riscv_vnclip_wx_i16m1(tre, 15, __RISCV_VXRM_RNU, vl);
                vint16m1_t bpi = __riscv_vnclip_wx_i16m1(tim, 15, __RISCV_VXRM_RNU, vl);
                /* up=(a+b')>>1, dn=(a-b')>>1 : widen, arith-shift-narrow (in range) */
                vint16m1_t upr = __riscv_vnsra_wx_i16m1(__riscv_vwadd_vv_i32m2(ar, bpr, vl), 1, vl);
                vint16m1_t upi = __riscv_vnsra_wx_i16m1(__riscv_vwadd_vv_i32m2(ai, bpi, vl), 1, vl);
                vint16m1_t dnr = __riscv_vnsra_wx_i16m1(__riscv_vwsub_vv_i32m2(ar, bpr, vl), 1, vl);
                vint16m1_t dni = __riscv_vnsra_wx_i16m1(__riscv_vwsub_vv_i32m2(ai, bpi, vl), 1, vl);
                __riscv_vssseg2e16_v_i16m1x2(pa, stride,
                    __riscv_vcreate_v_i16m1x2(upr, upi), vl);
                __riscv_vssseg2e16_v_i16m1x2(pb, stride,
                    __riscv_vcreate_v_i16m1x2(dnr, dni), vl);
            }
        }
    }
#else
    k_fft64_q15_ref(d);
#endif
}

/* ---- 6. requantize int32 -> int8 --------------------------------------- *
 * Fully vertical (no reduction): widen-multiply by the Q31 multiplier into
 * i64, arithmetic >>31, round >>shift, add zero-point, then a saturating
 * narrow chain i64->i32->i16->i8 (vnclip) = the reference's sat8. */
void k_requantize_s8(const int32_t *acc, size_t n, int32_t mult, int shift, int32_t zp, int8_t *out)
{
#ifdef HAVE_RVV
    size_t vl;
    for (size_t i = 0; i < n; i += vl) {
        vl = __riscv_vsetvl_e32m1(n - i);
        vint32m1_t va = __riscv_vle32_v_i32m1(acc + i, vl);
        vint64m2_t prod = __riscv_vwmul_vx_i64m2(va, mult, vl);   /* i32*i32 -> i64 */
        vint64m2_t scaled = __riscv_vsra_vx_i64m2(prod, 31, vl);
        vint64m2_t rnd;
        if (shift > 0) {
            scaled = __riscv_vadd_vx_i64m2(scaled, (int64_t)1 << (shift - 1), vl);
            rnd = __riscv_vsra_vx_i64m2(scaled, (size_t)shift, vl);
        } else {
            rnd = __riscv_vsll_vx_i64m2(scaled, (size_t)(-shift), vl);
        }
        vint64m2_t tmp = __riscv_vadd_vx_i64m2(rnd, (int64_t)zp, vl);
        /* saturating narrows (shift 0): nested clamps == a single sat8 */
        vint32m1_t n32 = __riscv_vnclip_wx_i32m1(tmp, 0, __RISCV_VXRM_RNU, vl);
        vint16mf2_t n16 = __riscv_vnclip_wx_i16mf2(n32, 0, __RISCV_VXRM_RNU, vl);
        vint8mf4_t n8 = __riscv_vnclip_wx_i8mf4(n16, 0, __RISCV_VXRM_RNU, vl);
        __riscv_vse8_v_i8mf4(out + i, n8, vl);
    }
#else
    k_requantize_s8_ref(acc, n, mult, shift, zp, out);
#endif
}

/* ---- 7. int8 GEMM ------------------------------------------------------- *
 * C[i][j] = sum_k A[i][k]*B[k][j], i32 accumulate. Per output cell: A row is
 * contiguous, B column is strided (stride N bytes) via vlse8; widening i8*i8
 * -> i16, widening reduce -> i32. Exact integer accumulate == _ref. */
void k_matmul_s8(const int8_t *A, const int8_t *B, int32_t *C, int M, int K, int N)
{
#ifdef HAVE_RVV
    for (int i = 0; i < M; i++)
        for (int j = 0; j < N; j++) {
            vint32m1_t vacc = __riscv_vmv_s_x_i32m1(0, 1);
            size_t vl;
            for (int k = 0; k < K; k += (int)vl) {
                vl = __riscv_vsetvl_e8m1((size_t)(K - k));
                vint8m1_t va = __riscv_vle8_v_i8m1(A + i * K + k, vl);
                vint8m1_t vb = __riscv_vlse8_v_i8m1(B + k * N + j, (ptrdiff_t)N, vl);
                vint16m2_t vp = __riscv_vwmul_vv_i16m2(va, vb, vl);
                vacc = __riscv_vwredsum_vs_i16m2_i32m1(vp, vacc, vl);
            }
            C[i * N + j] = __riscv_vmv_x_s_i32m1_i32(vacc);
        }
#else
    k_matmul_s8_ref(A, B, C, M, K, N);
#endif
}
