/* gen_golden.c - generate golden output vectors from the scalar reference.
 *
 * Run once (on any host) to (re)generate kernels/golden/ files. Inputs are
 * produced by the shared LCG in test_vectors.h, so they are identical on
 * every target; only outputs are stored. The runtime harness (run_kernels.c)
 * uses the SAME input generation and compares against these files.
 *
 * Output format: one integer per line, preceded by a "# <name> <count>"
 * header. Human-diffable on purpose.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../kernels/dsp_kernels.h"
#include "../kernels/test_vectors.h"

/* fixed problem sizes - keep in sync with run_kernels.c */
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
#define REQ_MULT 0x40000000  /* Q31 0.5 */
#define REQ_SHIFT 4
#define REQ_ZP 0

static void wr_i16(const char *path, const char *name, const int16_t *v, int n) {
    FILE *f = fopen(path, "w");
    fprintf(f, "# %s %d\n", name, n);
    for (int i = 0; i < n; i++) fprintf(f, "%d\n", (int)v[i]);
    fclose(f);
}
static void wr_i32(const char *path, const char *name, const int32_t *v, int n) {
    FILE *f = fopen(path, "w");
    fprintf(f, "# %s %d\n", name, n);
    for (int i = 0; i < n; i++) fprintf(f, "%d\n", (int)v[i]);
    fclose(f);
}
static void wr_i8(const char *path, const char *name, const int8_t *v, int n) {
    FILE *f = fopen(path, "w");
    fprintf(f, "# %s %d\n", name, n);
    for (int i = 0; i < n; i++) fprintf(f, "%d\n", (int)v[i]);
    fclose(f);
}
static void wr_i64(const char *path, const char *name, int64_t v) {
    FILE *f = fopen(path, "w");
    fprintf(f, "# %s 1\n%lld\n", name, (long long)v);
    fclose(f);
}

int main(void) {
    lcg_t g;

    /* FIR */
    {
        int16_t h[FIR_T], x[FIR_N], y[FIR_N];
        lcg_seed(&g, SEED_FIR);
        fill_q15(&g, h, FIR_T);
        fill_q15(&g, x, FIR_N);
        k_fir_q15_ref(x, FIR_N, h, FIR_T, y);
        wr_i16("kernels/golden/fir_q15.txt", "fir_q15", y, FIR_N);
    }
    /* Biquad */
    {
        int16_t x[BIQ_N], y[BIQ_N], state[4] = {0,0,0,0};
        lcg_seed(&g, SEED_BIQUAD);
        fill_q15(&g, x, BIQ_N);
        k_biquad_q14_ref(x, BIQ_N, BIQUAD_COEFF, state, y);
        wr_i16("kernels/golden/biquad_q14.txt", "biquad_q14", y, BIQ_N);
    }
    /* Dot */
    {
        int16_t a[DOT_N], b[DOT_N], out;
        lcg_seed(&g, SEED_DOT);
        fill_q15(&g, a, DOT_N);
        fill_q15(&g, b, DOT_N);
        int64_t raw = k_dotprod_q15_ref(a, b, DOT_N, &out);
        wr_i64("kernels/golden/dotprod_q15.txt", "dotprod_q15_raw", raw);
    }
    /* Complex dot */
    {
        cq15_t a[CDOT_N], b[CDOT_N], out;
        lcg_seed(&g, SEED_CDOT);
        fill_cq15(&g, a, CDOT_N);
        fill_cq15(&g, b, CDOT_N);
        k_cdotprod_q15_ref(a, b, CDOT_N, &out);
        int16_t pair[2] = { out.re, out.im };
        wr_i16("kernels/golden/cdotprod_q15.txt", "cdotprod_q15", pair, 2);
    }
    /* FFT */
    {
        cq15_t d[64];
        lcg_seed(&g, SEED_FFT);
        fill_cq15(&g, d, 64);
        k_fft64_q15_ref(d);
        int16_t flat[128];
        for (int i = 0; i < 64; i++) { flat[2*i] = d[i].re; flat[2*i+1] = d[i].im; }
        wr_i16("kernels/golden/fft64_q15.txt", "fft64_q15", flat, 128);
    }
    /* Requantize */
    {
        int32_t acc[REQ_N]; int8_t out[REQ_N];
        lcg_seed(&g, SEED_REQUANT);
        fill_s32(&g, acc, REQ_N);
        k_requantize_s8_ref(acc, REQ_N, REQ_MULT, REQ_SHIFT, REQ_ZP, out);
        wr_i8("kernels/golden/requantize_s8.txt", "requantize_s8", out, REQ_N);
    }
    /* Matmul */
    {
        int8_t A[MM_M*MM_K], B[MM_K*MM_N]; int32_t C[MM_M*MM_N];
        lcg_seed(&g, SEED_MATMUL);
        fill_s8(&g, A, MM_M*MM_K);
        fill_s8(&g, B, MM_K*MM_N);
        k_matmul_s8_ref(A, B, C, MM_M, MM_K, MM_N);
        wr_i32("kernels/golden/matmul_s8.txt", "matmul_s8", C, MM_M*MM_N);
    }

    printf("golden vectors generated.\n");
    return 0;
}
