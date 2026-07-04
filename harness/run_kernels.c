/* run_kernels.c - on-target equivalence + metrics harness.
 *
 * Runs the UNDER-TEST public k_* kernels (baseline / HiFi / RVV depending
 * on which impl file this leg links) against golden vectors compiled in
 * from golden_data.h. Prints one PASS/FAIL line per kernel and returns
 * nonzero if any kernel diverges - so "qemu ... ; echo $?" is the CI gate.
 *
 * No filesystem needed: runs cleanly under qemu-system-xtensa -M sim,
 * xt-run, and the cortex-m7 semihosting path alike. Inputs come from the
 * shared LCG (test_vectors.h) - identical to how golden was generated.
 *
 * Metrics: cycle counts are target-specific. Hook them in cyc_begin()/
 * cyc_end() below (CCOUNT on Xtensa, rdcycle on RISC-V, DWT->CYCCNT on
 * Cortex-M). Left as 0 on hosts that lack a counter; the equivalence
 * result does not depend on them.
 */
#include <stdio.h>
#include "../kernels/dsp_kernels.h"
#include "../kernels/test_vectors.h"
#include "golden_data.h"

/* keep sizes in lockstep with gen_golden.c */
#define FIR_N 32
#define FIR_T 8
#define BIQ_N 32
#define DOT_N 32
#define CDOT_N 16
#define REQ_N 32
#define MM_M 8
#define MM_K 8
#define MM_N 8
static const int16_t BIQUAD_COEFF[5] = { 4096, 8192, 4096, -8192, 4096 };
#define REQ_MULT 0x40000000
#define REQ_SHIFT 4
#define REQ_ZP 0

static int g_fail = 0;

static void check(const char *name, const int64_t *got, const int64_t *gold, int n) {
    int bad = -1;
    for (int i = 0; i < n; i++) if (got[i] != gold[i]) { bad = i; break; }
    if (bad < 0) {
        printf("PASS  %-16s  (%d values)\n", name, n);
    } else {
        printf("FAIL  %-16s  idx %d: got %lld want %lld\n",
               name, bad, (long long)got[bad], (long long)gold[bad]);
        g_fail = 1;
    }
}

/* optional cycle counter hooks (fill per target) */
static inline unsigned long cyc_begin(void) { return 0; }
static inline unsigned long cyc_end(void)   { return 0; }

int main(void) {
    lcg_t g;
    int64_t got[128];

    printf("== isa-portability-lab :: DSP kernel equivalence ==\n");

    /* FIR */
    {
        int16_t h[FIR_T], x[FIR_N], y[FIR_N];
        lcg_seed(&g, SEED_FIR); fill_q15(&g, h, FIR_T); fill_q15(&g, x, FIR_N);
        (void)cyc_begin(); k_fir_q15(x, FIR_N, h, FIR_T, y); (void)cyc_end();
        for (int i = 0; i < FIR_N; i++) got[i] = y[i];
        check("fir_q15", got, golden_fir_q15, FIR_N);
    }
    /* Biquad */
    {
        int16_t x[BIQ_N], y[BIQ_N], st[4] = {0,0,0,0};
        lcg_seed(&g, SEED_BIQUAD); fill_q15(&g, x, BIQ_N);
        k_biquad_q14(x, BIQ_N, BIQUAD_COEFF, st, y);
        for (int i = 0; i < BIQ_N; i++) got[i] = y[i];
        check("biquad_q14", got, golden_biquad_q14, BIQ_N);
    }
    /* Dot (raw 64-bit) */
    {
        int16_t a[DOT_N], b[DOT_N], o;
        lcg_seed(&g, SEED_DOT); fill_q15(&g, a, DOT_N); fill_q15(&g, b, DOT_N);
        got[0] = k_dotprod_q15(a, b, DOT_N, &o);
        check("dotprod_q15_raw", got, golden_dotprod_q15_raw, 1);
    }
    /* Complex dot */
    {
        cq15_t a[CDOT_N], b[CDOT_N], o;
        lcg_seed(&g, SEED_CDOT); fill_cq15(&g, a, CDOT_N); fill_cq15(&g, b, CDOT_N);
        k_cdotprod_q15(a, b, CDOT_N, &o);
        got[0] = o.re; got[1] = o.im;
        check("cdotprod_q15", got, golden_cdotprod_q15, 2);
    }
    /* FFT */
    {
        cq15_t d[64];
        lcg_seed(&g, SEED_FFT); fill_cq15(&g, d, 64);
        k_fft64_q15(d);
        for (int i = 0; i < 64; i++) { got[2*i] = d[i].re; got[2*i+1] = d[i].im; }
        check("fft64_q15", got, golden_fft64_q15, 128);
    }
    /* Requantize */
    {
        int32_t acc[REQ_N]; int8_t out[REQ_N];
        lcg_seed(&g, SEED_REQUANT); fill_s32(&g, acc, REQ_N);
        k_requantize_s8(acc, REQ_N, REQ_MULT, REQ_SHIFT, REQ_ZP, out);
        for (int i = 0; i < REQ_N; i++) got[i] = out[i];
        check("requantize_s8", got, golden_requantize_s8, REQ_N);
    }
    /* Matmul */
    {
        int8_t A[MM_M*MM_K], B[MM_K*MM_N]; int32_t C[MM_M*MM_N];
        lcg_seed(&g, SEED_MATMUL); fill_s8(&g, A, MM_M*MM_K); fill_s8(&g, B, MM_K*MM_N);
        k_matmul_s8(A, B, C, MM_M, MM_K, MM_N);
        for (int i = 0; i < MM_M*MM_N; i++) got[i] = C[i];
        check("matmul_s8", got, golden_matmul_s8, MM_M*MM_N);
    }

    printf(g_fail ? "== RESULT: FAIL ==\n" : "== RESULT: PASS ==\n");
    return g_fail;
}
