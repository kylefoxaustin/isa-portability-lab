/*
 * main.c - Bring-up sanity check + self-test suite.
 *
 * Phase A: bring-up sanity prints (vlenb, FP, accel-stub, vsetvli probe).
 * Phase B: numbered self-test suite. Each test runs a small RVV / Zbb /
 *          FP kernel and compares against a scalar golden. Pass/fail is
 *          tallied; the final QEMU sifive_test exit code carries the
 *          number of failures (0 = all green).
 *
 * Suite contents:
 *   T1  u32 elementwise add        - vle32 / vadd.vv  / vse32           (N=17)
 *   T2  f32 elementwise add        - vle32 / vfadd.vv / vse32           (N=17)
 *   T3  f32 dot product            - vfmul.vv / vfredusum.vs / vfmv.f.s (N=8)
 *   T4  u8  memcpy at e8 SEW       - vle8  / vse8                       (N=50)
 *   T5  Zbb scalar popcount (cpop)                                      (4 cases)
 *
 * QEMU-flavored bits: 16550 UART @ 0x10000000, sifive_test @ 0x00100000.
 * Both #ifdef'd behind QEMU_VIRT_TARGET (default on) for silicon migration.
 */

#include <stdint.h>
#include <stddef.h>
#include "accelerator_intrinsics.h"

#ifndef QEMU_VIRT_TARGET
#define QEMU_VIRT_TARGET 1
#endif

#define QEMU_VIRT_UART0       ((volatile uint8_t  *)0x10000000)
#define QEMU_VIRT_SIFIVE_TEST ((volatile uint32_t *)0x00100000)

#define SIFIVE_TEST_PASS 0x5555u
#define SIFIVE_TEST_FAIL 0x3333u

/* Trap post-mortem block, written by trap_vector (trap.S).
 * Layout: [0]=mcause [1]=mepc [2]=mtval [3]=trap count. */
extern volatile uint64_t _trap_info[4];

/* Machine-mode mcause values we deliberately provoke in T13. */
#define MCAUSE_ILLEGAL_INSTR  2
#define MCAUSE_ECALL_FROM_M  11

/* ----- minimal UART ------------------------------------------------------ */
static void uart_putc(char c)        { *QEMU_VIRT_UART0 = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static void uart_put_u32(uint32_t v)
{
    if (v == 0) { uart_putc('0'); return; }
    char buf[11]; int n = 0;
    while (v) { buf[n++] = '0' + (v % 10); v /= 10; }
    while (n--) uart_putc(buf[n]);
}

static void uart_put_hex64(uint64_t v)
{
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        int nib = (v >> i) & 0xf;
        uart_putc(nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
}

/* ----- pass/fail signalling via QEMU sifive_test ------------------------- */
__attribute__((noreturn))
static void qemu_test_exit(uint32_t code)
{
#if QEMU_VIRT_TARGET
    if (code == 0) {
        *QEMU_VIRT_SIFIVE_TEST = SIFIVE_TEST_PASS;
    } else {
        *QEMU_VIRT_SIFIVE_TEST = ((code & 0xFFFFu) << 16) | SIFIVE_TEST_FAIL;
    }
#else
    (void)code;
#endif
    for (;;) asm volatile ("wfi");
}

/* =========================================================================
 * Inline-asm contract note
 * -------------------------------------------------------------------------
 * Every multi-instruction RVV block below writes vl via vsetvli and then
 * reads other input pointers in the same block. Because GCC assumes outputs
 * are written AFTER all inputs are read, a plain "=r"(vl) lets it alias the
 * vl reg with one of the pointer inputs and the load/store then faults on a
 * tiny garbage address. Use "=&r" (early clobber) on the vl output to
 * forbid that aliasing. All RVV blocks below follow that convention.
 * ========================================================================= */

/* ----- T1: u32 strip-mined elementwise add ------------------------------- */
static void vec_add_u32(const uint32_t *a, const uint32_t *b,
                        uint32_t *c, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli  %0, %4, e32, m1, ta, ma\n\t"
            "vle32.v  v0, (%1)\n\t"
            "vle32.v  v1, (%2)\n\t"
            "vadd.vv  v2, v0, v1\n\t"
            "vse32.v  v2, (%3)"
            : "=&r"(vl)
            : "r"(a + i), "r"(b + i), "r"(c + i), "r"(n - i)
            : "memory", "v0", "v1", "v2"
        );
        i += vl;
    }
}

/* ----- T2: f32 strip-mined elementwise add ------------------------------- */
static void vec_add_f32(const float *a, const float *b,
                        float *c, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli  %0, %4, e32, m1, ta, ma\n\t"
            "vle32.v  v0, (%1)\n\t"
            "vle32.v  v1, (%2)\n\t"
            "vfadd.vv v2, v0, v1\n\t"
            "vse32.v  v2, (%3)"
            : "=&r"(vl)
            : "r"(a + i), "r"(b + i), "r"(c + i), "r"(n - i)
            : "memory", "v0", "v1", "v2"
        );
        i += vl;
    }
}

/* ----- T3: f32 dot product with vector reduction -------------------------
 * Per-stripe pattern:
 *   v2 = a * b  (elementwise)
 *   v3[0] = 0.0 (seed for reduction)
 *   v4[0] = sum(v2[0..vl-1]) + v3[0]   (vfredusum.vs is unordered)
 *   partial = v4[0]
 * Scalar accumulator collects partials across stripes.
 *
 * For exact-integer-valued inputs (no rounding anywhere) the unordered
 * reduction is bit-exact equal to any other order, so we compare bit-for-
 * bit against a scalar golden.
 * ------------------------------------------------------------------------- */
static float vec_dot_f32(const float *a, const float *b, size_t n)
{
    float acc = 0.0f;
    float zero = 0.0f;
    size_t i = 0;
    while (i < n) {
        size_t vl;
        float partial;
        asm volatile (
            "vsetvli      %0, %5, e32, m1, ta, ma\n\t"
            "vle32.v      v0, (%2)\n\t"
            "vle32.v      v1, (%3)\n\t"
            "vfmul.vv     v2, v0, v1\n\t"
            "vfmv.s.f     v3, %4\n\t"
            "vfredusum.vs v4, v2, v3\n\t"
            "vfmv.f.s     %1, v4"
            : "=&r"(vl), "=f"(partial)
            : "r"(a + i), "r"(b + i), "f"(zero), "r"(n - i)
            : "memory", "v0", "v1", "v2", "v3", "v4"
        );
        acc += partial;
        i += vl;
    }
    return acc;
}

/* ----- T4: u8 vector memcpy at e8 SEW -----------------------------------
 * With e8/m1, max VL = VLEN/8 = 16 at VLEN=128, so a 50-byte buffer takes
 * three full stripes of 16 plus a 2-byte tail.
 * ------------------------------------------------------------------------- */
static void vec_memcpy_u8(uint8_t *dst, const uint8_t *src, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli  %0, %3, e8, m1, ta, ma\n\t"
            "vle8.v   v0, (%1)\n\t"
            "vse8.v   v0, (%2)"
            : "=&r"(vl)
            : "r"(src + i), "r"(dst + i), "r"(n - i)
            : "memory", "v0"
        );
        i += vl;
    }
}

/* ----- T5: Zbb popcount (cpop, RV64) ------------------------------------- */
static uint64_t scalar_cpop(uint64_t x)
{
    uint64_t r;
    asm volatile("cpop %0, %1" : "=r"(r) : "r"(x));
    return r;
}

/* ----- T17: extended scalar bit-manip (Zbb + Zbs) ------------------------ */
static uint64_t zbb_clz (uint64_t x)            { uint64_t r; asm volatile("clz  %0,%1"    :"=r"(r):"r"(x));            return r; }
static uint64_t zbb_ctz (uint64_t x)            { uint64_t r; asm volatile("ctz  %0,%1"    :"=r"(r):"r"(x));            return r; }
static uint64_t zbb_rev8(uint64_t x)            { uint64_t r; asm volatile("rev8 %0,%1"    :"=r"(r):"r"(x));            return r; }
static int64_t  zbb_max (int64_t a, int64_t b)  { int64_t  r; asm volatile("max  %0,%1,%2" :"=r"(r):"r"(a),"r"(b));     return r; }
static uint64_t zbb_minu(uint64_t a, uint64_t b){ uint64_t r; asm volatile("minu %0,%1,%2" :"=r"(r):"r"(a),"r"(b));     return r; }
static uint64_t zbs_bset(uint64_t x, uint64_t i){ uint64_t r; asm volatile("bset %0,%1,%2" :"=r"(r):"r"(x),"r"(i));     return r; }
static uint64_t zbs_bclr(uint64_t x, uint64_t i){ uint64_t r; asm volatile("bclr %0,%1,%2" :"=r"(r):"r"(x),"r"(i));     return r; }
static uint64_t zbs_bext(uint64_t x, uint64_t i){ uint64_t r; asm volatile("bext %0,%1,%2" :"=r"(r):"r"(x),"r"(i));     return r; }
static uint64_t zbs_binv(uint64_t x, uint64_t i){ uint64_t r; asm volatile("binv %0,%1,%2" :"=r"(r):"r"(x),"r"(i));     return r; }

/* ----- T26: Zba shift-add (address generation) --------------------------- */
static uint64_t zba_sh1add(uint64_t a, uint64_t b){ uint64_t r; asm volatile("sh1add %0,%1,%2":"=r"(r):"r"(a),"r"(b)); return r; }
static uint64_t zba_sh2add(uint64_t a, uint64_t b){ uint64_t r; asm volatile("sh2add %0,%1,%2":"=r"(r):"r"(a),"r"(b)); return r; }
static uint64_t zba_sh3add(uint64_t a, uint64_t b){ uint64_t r; asm volatile("sh3add %0,%1,%2":"=r"(r):"r"(a),"r"(b)); return r; }
static uint64_t zba_adduw (uint64_t a, uint64_t b){ uint64_t r; asm volatile("add.uw %0,%1,%2":"=r"(r):"r"(a),"r"(b)); return r; }

/* ----- T6: Zfh scalar half-precision add (fadd.h) ------------------------ */
static uint16_t f16_add_bits(_Float16 a, _Float16 b)
{
    _Float16 r = a + b;          /* lowers to fadd.h under -march=...zfh */
    uint16_t bits;
    __builtin_memcpy(&bits, &r, sizeof(bits));
    return bits;
}

/* ----- T7: widening int16->int32 add ------------------------------------
 * vwadd produces a 2*SEW result, so the destination is a 2-register group
 * (EMUL = 2*LMUL). We compute at e16/m1 then re-program vtype to e32/m2
 * (same element count) to store the widened result. Tests multi-register
 * groups and mid-block vtype changes.
 * ------------------------------------------------------------------------- */
static void vec_widen_add_i16(const int16_t *a, const int16_t *b,
                              int32_t *c, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli  %0, %4, e16, m1, ta, ma\n\t"
            "vle16.v  v0, (%1)\n\t"
            "vle16.v  v1, (%2)\n\t"
            "vwadd.vv v2, v0, v1\n\t"          /* v2:v3 = sext(v0)+sext(v1) */
            "vsetvli  zero, %0, e32, m2, ta, ma\n\t"
            "vse32.v  v2, (%3)"
            : "=&r"(vl)
            : "r"(a + i), "r"(b + i), "r"(c + i), "r"(n - i)
            : "memory", "v0", "v1", "v2", "v3"
        );
        i += vl;
    }
}

/* ----- T8: masked add, active lanes = even index ------------------------
 * Single-stripe (n <= VLMAX). Builds a mask in v0 from vid (even lanes
 * active), seeds the destination with a background value, then does a
 * mask-undisturbed vadd so inactive lanes keep the background. Tests the
 * v0 mask path and the mu tail/mask policy.
 * ------------------------------------------------------------------------- */
static void vec_masked_add_even(const uint32_t *a, const uint32_t *b,
                                uint32_t *c, size_t n, uint32_t bg)
{
    size_t vl;
    asm volatile (
        "vsetvli  %0, %5, e32, m1, ta, mu\n\t"
        "vle32.v  v1, (%1)\n\t"
        "vle32.v  v2, (%2)\n\t"
        "vid.v    v3\n\t"                 /* v3 = [0,1,2,3,...] */
        "vand.vi  v3, v3, 1\n\t"          /* v3 = idx & 1       */
        "vmseq.vi v0, v3, 0\n\t"          /* mask = (idx even)  */
        "vmv.v.x  v4, %4\n\t"             /* background in all lanes */
        "vadd.vv  v4, v1, v2, v0.t\n\t"   /* active lanes only       */
        "vse32.v  v4, (%3)"
        : "=&r"(vl)
        : "r"(a), "r"(b), "r"(c), "r"(bg), "r"(n)
        : "memory", "v0", "v1", "v2", "v3", "v4"
    );
}

/* ----- T9: strided gather (vlse32.v) ------------------------------------
 * Loads n elements from src with an arbitrary byte stride and packs them
 * contiguously into dst. Tests non-unit-stride memory access (matrix-row
 * / structure-of-arrays patterns).
 * ------------------------------------------------------------------------- */
static void vec_gather_stride(const uint32_t *src, uint32_t *dst,
                              size_t n, long stride_bytes)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli  %0, %4, e32, m1, ta, ma\n\t"
            "vlse32.v v0, (%1), %2\n\t"
            "vse32.v  v0, (%3)"
            : "=&r"(vl)
            : "r"(src), "r"(stride_bytes), "r"(dst + i), "r"(n - i)
            : "memory", "v0"
        );
        src = (const uint32_t *)((const uint8_t *)src + (size_t)vl * stride_bytes);
        i += vl;
    }
}

/* ----- T10: saxpy y = alpha*x + y (vfmacc.vf, fused) --------------------- */
static void saxpy_f32(float alpha, const float *x, float *y, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli   %0, %4, e32, m1, ta, ma\n\t"
            "vle32.v   v0, (%1)\n\t"           /* x */
            "vle32.v   v1, (%2)\n\t"           /* y */
            "vfmacc.vf v1, %3, v0\n\t"         /* v1 = alpha*x + y (fused) */
            "vse32.v   v1, (%2)"
            : "=&r"(vl)
            : "r"(x + i), "r"(y + i), "f"(alpha), "r"(n - i)
            : "memory", "v0", "v1"
        );
        i += vl;
    }
}

/* ----- T14: segment load (vlseg2e32) - deinterleave xy -> x[], y[] -------
 * Memory holds pairs {x0,y0,x1,y1,...}; the segment load splits field 0 into
 * v0 and field 1 into v1 in one shot. Tests structured (AoS->SoA) access.
 * ------------------------------------------------------------------------- */
static void vec_deinterleave2_u32(const uint32_t *xy,
                                  uint32_t *xs, uint32_t *ys, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli     %0, %4, e32, m1, ta, ma\n\t"
            "vlseg2e32.v v0, (%1)\n\t"     /* v0 = x field, v1 = y field */
            "vse32.v     v0, (%2)\n\t"
            "vse32.v     v1, (%3)"
            : "=&r"(vl)
            : "r"(xy), "r"(xs + i), "r"(ys + i), "r"(n - i)
            : "memory", "v0", "v1"
        );
        xy += (size_t)vl * 2;            /* consumed vl pairs */
        i  += vl;
    }
}

/* ----- T15: indexed gather (vluxei32) - dst[i] = base[idx[i]] ------------
 * Index vector holds element indices; we shift left by 2 to make byte
 * offsets, then do an unordered indexed load. Tests random-access gather.
 * ------------------------------------------------------------------------- */
static void vec_gather_index(const uint32_t *base, const uint32_t *idx,
                             uint32_t *dst, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli    %0, %4, e32, m1, ta, ma\n\t"
            "vle32.v    v1, (%2)\n\t"       /* element indices */
            "vsll.vi    v1, v1, 2\n\t"      /* * 4 -> byte offsets */
            "vluxei32.v v0, (%1), v1\n\t"   /* v0[i] = base[off v1[i]] */
            "vse32.v    v0, (%3)"
            : "=&r"(vl)
            : "r"(base), "r"(idx + i), "r"(dst + i), "r"(n - i)
            : "memory", "v0", "v1"
        );
        i += vl;
    }
}

/* ----- T16: vrgather permute - in-register reverse ----------------------
 * Single stripe, n <= VLMAX. Builds reversed indices (vl-1 .. 0) with
 * vid+vrsub, then vrgather does vd[i] = src[idx[i]]. Uses LMUL=2 so VLMAX=8
 * at VLEN=128, exercising register groups in a permute. Tests vrgather and
 * multi-register-group operand alignment.
 * ------------------------------------------------------------------------- */
static void vec_reverse8_u32(const uint32_t *src, uint32_t *dst, size_t n)
{
    size_t vl;
    asm volatile (
        "vsetvli     %0, %3, e32, m2, ta, ma\n\t"
        "vle32.v     v2, (%1)\n\t"          /* v2:v3 = src */
        "vid.v       v4\n\t"                /* v4:v5 = [0..vl-1] */
        "vrsub.vx    v4, v4, %4\n\t"        /* v4:v5 = (n-1) - idx */
        "vrgather.vv v0, v2, v4\n\t"        /* v0:v1[i] = src[idx[i]] */
        "vse32.v     v0, (%2)"
        : "=&r"(vl)
        : "r"(src), "r"(dst), "r"(n), "r"(n - 1)
        : "memory", "v0", "v1", "v2", "v3", "v4", "v5"
    );
    (void)vl;
}

/* ----- T18: vcompress - pack masked lanes (dual of gather) --------------
 * Single stripe at LMUL=2 (VLMAX=8 at VLEN=128), so n <= 8. Builds an
 * even-index mask, compacts the selected source lanes into the low end of
 * the destination, counts them with vcpop.m, then stores exactly that
 * many. Returns the packed count. NOTE: at m1 VLMAX would be only 4 -
 * m2 is required to fit n=8 in one stripe.
 * ------------------------------------------------------------------------- */
static size_t vec_compress_even(const uint32_t *src, uint32_t *dst, size_t n)
{
    size_t vl, count;
    asm volatile (
        "vsetvli      %0, %4, e32, m2, ta, ma\n\t"
        "vle32.v      v2, (%2)\n\t"       /* v2:v3 = src */
        "vid.v        v4\n\t"             /* v4:v5 = [0..vl-1] */
        "vand.vi      v4, v4, 1\n\t"
        "vmseq.vi     v0, v4, 0\n\t"      /* mask = even index (single reg) */
        "vcompress.vm v6, v2, v0\n\t"     /* pack masked lanes -> v6:v7 */
        "vcpop.m      %1, v0\n\t"         /* count of set mask bits */
        "vsetvli      zero, %1, e32, m2, ta, ma\n\t"
        "vse32.v      v6, (%3)"           /* store exactly `count` elements */
        : "=&r"(vl), "=&r"(count)
        : "r"(src), "r"(dst), "r"(n)
        : "memory", "v0", "v2", "v3", "v4", "v5", "v6", "v7"
    );
    (void)vl;
    return count;
}

/* ----- T19: vector mask-domain logic ------------------------------------
 * Single stripe at LMUL=2 (VLMAX=8). Builds two masks (data>4, data even),
 * ANDs them, then reports the population count and the index of the first
 * set lane. Exercises mask compares, mask-mask logic, vcpop.m, vfirst.m.
 * ------------------------------------------------------------------------- */
static void vec_mask_logic(const int32_t *data, size_t n,
                           uint64_t *out_count, int64_t *out_first)
{
    size_t vl;
    uint64_t cnt;
    int64_t  first;
    asm volatile (
        "vsetvli  %0, %4, e32, m2, ta, ma\n\t"
        "vle32.v  v2, (%3)\n\t"           /* v2:v3 = data */
        "vmsgt.vi v6, v2, 4\n\t"          /* maskA = data > 4 (single reg) */
        "vand.vi  v4, v2, 1\n\t"          /* v4:v5 = data & 1 */
        "vmseq.vi v7, v4, 0\n\t"          /* maskB = even (single reg) */
        "vmand.mm v0, v6, v7\n\t"         /* v0 = A & B */
        "vcpop.m  %1, v0\n\t"
        "vfirst.m %2, v0"                 /* first set lane, or -1 */
        : "=&r"(vl), "=&r"(cnt), "=&r"(first)
        : "r"(data), "r"(n)
        : "memory", "v0", "v2", "v3", "v4", "v5", "v6", "v7"
    );
    (void)vl;
    *out_count = cnt;
    *out_first = first;
}

/* ----- T20: integer max/min reductions ----------------------------------
 * Strip-mined like the dot product: each stripe reduces with vredmax/vredmin
 * seeded by the running accumulator (carried in a scalar across stripes), so
 * the result is correct for any n. Tests vredmax.vs / vredmin.vs + the
 * scalar<->element moves vmv.s.x / vmv.x.s.
 * ------------------------------------------------------------------------- */
#define I32_MIN (-2147483647 - 1)
#define I32_MAX ( 2147483647)

static int32_t vec_redmax_i32(const int32_t *a, size_t n)
{
    int32_t acc = I32_MIN;
    size_t i = 0;
    while (i < n) {
        size_t vl;
        int32_t out;
        long seed = acc;
        asm volatile (
            "vsetvli    %0, %4, e32, m1, ta, ma\n\t"
            "vle32.v    v1, (%2)\n\t"
            "vmv.s.x    v2, %3\n\t"          /* v2[0] = running max */
            "vredmax.vs v3, v1, v2\n\t"
            "vmv.x.s    %1, v3"
            : "=&r"(vl), "=r"(out)
            : "r"(a + i), "r"(seed), "r"(n - i)
            : "memory", "v1", "v2", "v3"
        );
        acc = out;
        i += vl;
    }
    return acc;
}

static int32_t vec_redmin_i32(const int32_t *a, size_t n)
{
    int32_t acc = I32_MAX;
    size_t i = 0;
    while (i < n) {
        size_t vl;
        int32_t out;
        long seed = acc;
        asm volatile (
            "vsetvli    %0, %4, e32, m1, ta, ma\n\t"
            "vle32.v    v1, (%2)\n\t"
            "vmv.s.x    v2, %3\n\t"
            "vredmin.vs v3, v1, v2\n\t"
            "vmv.x.s    %1, v3"
            : "=&r"(vl), "=r"(out)
            : "r"(a + i), "r"(seed), "r"(n - i)
            : "memory", "v1", "v2", "v3"
        );
        acc = out;
        i += vl;
    }
    return acc;
}

/* ----- T21: saturating fixed-point add (vsaddu / vsadd) ------------------
 * e8 so saturation is easy to provoke: unsigned clamps at 255, signed at
 * [-128,127], instead of wrapping like a plain vadd. Tests the fixed-point
 * saturating arithmetic path.
 * ------------------------------------------------------------------------- */
static void vec_satadd_u8(const uint8_t *a, const uint8_t *b, uint8_t *c, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli   %0, %4, e8, m1, ta, ma\n\t"
            "vle8.v    v0, (%1)\n\t"
            "vle8.v    v1, (%2)\n\t"
            "vsaddu.vv v2, v0, v1\n\t"
            "vse8.v    v2, (%3)"
            : "=&r"(vl)
            : "r"(a + i), "r"(b + i), "r"(c + i), "r"(n - i)
            : "memory", "v0", "v1", "v2"
        );
        i += vl;
    }
}

static void vec_satadd_i8(const int8_t *a, const int8_t *b, int8_t *c, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli  %0, %4, e8, m1, ta, ma\n\t"
            "vle8.v   v0, (%1)\n\t"
            "vle8.v   v1, (%2)\n\t"
            "vsadd.vv v2, v0, v1\n\t"
            "vse8.v   v2, (%3)"
            : "=&r"(vl)
            : "r"(a + i), "r"(b + i), "r"(c + i), "r"(n - i)
            : "memory", "v0", "v1", "v2"
        );
        i += vl;
    }
}

/* ----- T22: segment store (vsseg2e32) - interleave x[],y[] -> {x,y} ------
 * The dual of T14's segment load: takes two separate arrays and writes them
 * interleaved as pairs. Tests structured (SoA->AoS) store.
 * ------------------------------------------------------------------------- */
static void vec_interleave2_u32(const uint32_t *xs, const uint32_t *ys,
                                uint32_t *xy, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli     %0, %4, e32, m1, ta, ma\n\t"
            "vle32.v     v0, (%1)\n\t"      /* xs -> field 0 */
            "vle32.v     v1, (%2)\n\t"      /* ys -> field 1 */
            "vsseg2e32.v v0, (%3)"          /* store {x,y} pairs */
            : "=&r"(vl)
            : "r"(xs + i), "r"(ys + i), "r"(xy), "r"(n - i)
            : "memory", "v0", "v1"
        );
        xy += (size_t)vl * 2;
        i  += vl;
    }
}

/* ----- T23: float max/min reductions ------------------------------------
 * FP counterpart to T20. Strip-mined with a scalar accumulator seeded by
 * +/-inf (built from bit patterns, no libc). Tests vfredmax.vs/vfredmin.vs
 * and the FP scalar<->element moves vfmv.s.f / vfmv.f.s.
 * ------------------------------------------------------------------------- */
static float f32_from_bits(uint32_t b) { float f; __builtin_memcpy(&f, &b, 4); return f; }

static float vec_fredmax(const float *a, size_t n)
{
    float acc = f32_from_bits(0xFF800000u);     /* -inf */
    size_t i = 0;
    while (i < n) {
        size_t vl; float out;
        asm volatile (
            "vsetvli     %0, %4, e32, m1, ta, ma\n\t"
            "vle32.v     v1, (%2)\n\t"
            "vfmv.s.f    v2, %3\n\t"
            "vfredmax.vs v3, v1, v2\n\t"
            "vfmv.f.s    %1, v3"
            : "=&r"(vl), "=f"(out)
            : "r"(a + i), "f"(acc), "r"(n - i)
            : "memory", "v1", "v2", "v3"
        );
        acc = out;
        i += vl;
    }
    return acc;
}

static float vec_fredmin(const float *a, size_t n)
{
    float acc = f32_from_bits(0x7F800000u);     /* +inf */
    size_t i = 0;
    while (i < n) {
        size_t vl; float out;
        asm volatile (
            "vsetvli     %0, %4, e32, m1, ta, ma\n\t"
            "vle32.v     v1, (%2)\n\t"
            "vfmv.s.f    v2, %3\n\t"
            "vfredmin.vs v3, v1, v2\n\t"
            "vfmv.f.s    %1, v3"
            : "=&r"(vl), "=f"(out)
            : "r"(a + i), "f"(acc), "r"(n - i)
            : "memory", "v1", "v2", "v3"
        );
        acc = out;
        i += vl;
    }
    return acc;
}

/* ----- T24: fractional-LMUL widening zero-extend e8 -> e32 ---------------
 * Loading e8 elements while vtype SEW=32 gives EMUL = 8/32 = mf4 (fractional
 * LMUL) for the source - the canonical case where fractional LMUL is forced.
 * vzext.vf4 then zero-extends each byte to 32 bits. Tests fractional LMUL
 * and the 4x extending conversion.
 * ------------------------------------------------------------------------- */
static void vec_zext8to32(const uint8_t *src, uint32_t *dst, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli   %0, %3, e32, m1, ta, ma\n\t"  /* dest SEW=32 */
            "vle8.v    v1, (%1)\n\t"                 /* EEW=8 -> EMUL=mf4 */
            "vzext.vf4 v2, v1\n\t"                   /* zero-extend e8 -> e32 */
            "vse32.v   v2, (%2)"
            : "=&r"(vl)
            : "r"(src + i), "r"(dst + i), "r"(n - i)
            : "memory", "v1", "v2"
        );
        i += vl;
    }
}

/* ----- T25: narrowing e32 -> e16 (vnsrl.wi) ------------------------------
 * Dual of T7's widening. With vtype SEW=16, an EEW=32 load lands in a 2-reg
 * group (EMUL=m2); vnsrl.wi narrows it back to e16 (shift 0 = truncate low
 * 16 bits). Tests narrowing ops and the wide-source register grouping.
 * ------------------------------------------------------------------------- */
static void vec_narrow_32to16(const uint32_t *src, uint16_t *dst, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli  %0, %3, e16, m1, ta, ma\n\t"   /* dest SEW=16 */
            "vle32.v  v2, (%1)\n\t"                  /* EEW=32 -> EMUL=m2 (v2:v3) */
            "vnsrl.wi v4, v2, 0\n\t"                 /* narrow 32->16, truncate */
            "vse16.v  v4, (%2)"
            : "=&r"(vl)
            : "r"(src + i), "r"(dst + i), "r"(n - i)
            : "memory", "v2", "v3", "v4"
        );
        i += vl;
    }
}

/* ----- T27: vector FP <-> int conversions -------------------------------
 * vfcvt.rtz.x.f.v: float -> signed int, round toward zero (deterministic,
 * no dependence on frm). vfcvt.f.x.v: signed int -> float. Tests the
 * convert pipeline both directions.
 * ------------------------------------------------------------------------- */
static void vec_f2i_rtz(const float *src, int32_t *dst, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli         %0, %3, e32, m1, ta, ma\n\t"
            "vle32.v         v1, (%1)\n\t"
            "vfcvt.rtz.x.f.v v2, v1\n\t"
            "vse32.v         v2, (%2)"
            : "=&r"(vl)
            : "r"(src + i), "r"(dst + i), "r"(n - i)
            : "memory", "v1", "v2"
        );
        i += vl;
    }
}

static void vec_i2f(const int32_t *src, float *dst, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli     %0, %3, e32, m1, ta, ma\n\t"
            "vle32.v     v1, (%1)\n\t"
            "vfcvt.f.x.v v2, v1\n\t"
            "vse32.v     v2, (%2)"
            : "=&r"(vl)
            : "r"(src + i), "r"(dst + i), "r"(n - i)
            : "memory", "v1", "v2"
        );
        i += vl;
    }
}

/* ----- T28: multi-precision 64-bit add via carry chain ------------------
 * Each 64-bit operand is split into lo/hi 32-bit limbs across parallel
 * vectors. vmadc produces the carry-out mask of the lo add; vadc folds that
 * carry into the hi add. Tests add-with-carry / carry-out (vadc/vmadc) -
 * the building block of bignum / wide arithmetic.
 * ------------------------------------------------------------------------- */
static void vec_add64(const uint32_t *alo, const uint32_t *ahi,
                      const uint32_t *blo, const uint32_t *bhi,
                      uint32_t *rlo, uint32_t *rhi, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli  %0, %7, e32, m1, ta, ma\n\t"
            "vle32.v  v1, (%1)\n\t"          /* alo */
            "vle32.v  v2, (%3)\n\t"          /* blo */
            "vle32.v  v3, (%2)\n\t"          /* ahi */
            "vle32.v  v4, (%4)\n\t"          /* bhi */
            "vmadc.vv v0, v1, v2\n\t"        /* v0 = carry-out of lo add */
            "vadd.vv  v5, v1, v2\n\t"        /* lo sum */
            "vadc.vvm v6, v3, v4, v0\n\t"    /* hi sum + carry-in */
            "vse32.v  v5, (%5)\n\t"          /* rlo */
            "vse32.v  v6, (%6)"             /* rhi */
            : "=&r"(vl)
            : "r"(alo + i), "r"(ahi + i), "r"(blo + i), "r"(bhi + i),
              "r"(rlo + i), "r"(rhi + i), "r"(n - i)
            : "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6"
        );
        i += vl;
    }
}

/* ----- T29: slides (vslidedown / vslide1up) -----------------------------
 * Single stripe at LMUL=2 (VLMAX=8). slidedown by 2 windows the vector
 * (dst[i]=src[i+2], zero-filled past the end); slide1up shifts up by one
 * and injects a scalar at lane 0. Tests inter-lane data movement.
 * ------------------------------------------------------------------------- */
static void vec_slidedown2(const uint32_t *src, uint32_t *dst, size_t n)
{
    size_t vl;
    asm volatile (
        "vsetvli       %0, %3, e32, m2, ta, ma\n\t"
        "vle32.v       v2, (%1)\n\t"
        "vslidedown.vi v0, v2, 2\n\t"        /* v0[i] = v2[i+2] */
        "vse32.v       v0, (%2)"
        : "=&r"(vl)
        : "r"(src), "r"(dst), "r"(n)
        : "memory", "v0", "v1", "v2", "v3"
    );
    (void)vl;
}

static void vec_slide1up(const uint32_t *src, uint32_t *dst, size_t n, uint32_t head)
{
    size_t vl;
    asm volatile (
        "vsetvli     %0, %3, e32, m2, ta, ma\n\t"
        "vle32.v     v2, (%1)\n\t"
        "vslide1up.vx v0, v2, %4\n\t"        /* v0[0]=head, v0[i]=v2[i-1] */
        "vse32.v     v0, (%2)"
        : "=&r"(vl)
        : "r"(src), "r"(dst), "r"(n), "r"(head)
        : "memory", "v0", "v1", "v2", "v3"
    );
    (void)vl;
}

/* ----- T30: vector integer divide + remainder (vdivu/vremu) -------------- */
static void vec_divrem_u32(const uint32_t *a, const uint32_t *b,
                           uint32_t *q, uint32_t *r, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli  %0, %5, e32, m1, ta, ma\n\t"
            "vle32.v  v1, (%1)\n\t"
            "vle32.v  v2, (%2)\n\t"
            "vdivu.vv v3, v1, v2\n\t"
            "vremu.vv v4, v1, v2\n\t"
            "vse32.v  v3, (%3)\n\t"
            "vse32.v  v4, (%4)"
            : "=&r"(vl)
            : "r"(a + i), "r"(b + i), "r"(q + i), "r"(r + i), "r"(n - i)
            : "memory", "v1", "v2", "v3", "v4"
        );
        i += vl;
    }
}

/* ----- T31: vmerge.vvm - per-lane select between two vectors (no compute) -
 * dst[i] = mask[i] ? t[i] : f[i]. Single stripe at m2 (VLMAX=8).
 * ------------------------------------------------------------------------- */
static void vec_merge_even(const uint32_t *t, const uint32_t *f,
                           uint32_t *dst, size_t n)
{
    size_t vl;
    asm volatile (
        "vsetvli    %0, %4, e32, m2, ta, ma\n\t"
        "vle32.v    v2, (%1)\n\t"           /* t (true values)  */
        "vle32.v    v4, (%2)\n\t"           /* f (false values) */
        "vid.v      v6\n\t"
        "vand.vi    v6, v6, 1\n\t"
        "vmseq.vi   v0, v6, 0\n\t"          /* mask = even idx */
        "vmerge.vvm v8, v4, v2, v0\n\t"     /* v8 = mask ? t : f */
        "vse32.v    v8, (%3)"
        : "=&r"(vl)
        : "r"(t), "r"(f), "r"(dst), "r"(n)
        : "memory", "v0", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9"
    );
    (void)vl;
}

/* ----- T32: vrgatherei16 - gather with 16-bit indices -------------------
 * Data is e32 (m2), indices are e16 (EMUL=m1) - lets a wide-element gather
 * use a compact index vector. Single stripe, n <= VLMAX=8.
 * ------------------------------------------------------------------------- */
static void vec_gather_ei16(const uint32_t *src, const uint16_t *idx16,
                            uint32_t *dst, size_t n)
{
    size_t vl;
    asm volatile (
        "vsetvli        %0, %4, e32, m2, ta, ma\n\t"
        "vle32.v        v2, (%1)\n\t"       /* src  -> v2:v3 (e32) */
        "vle16.v        v4, (%2)\n\t"       /* idx  -> v4    (e16) */
        "vrgatherei16.vv v0, v2, v4\n\t"    /* v0:v1[i] = src[idx[i]] */
        "vse32.v        v0, (%3)"
        : "=&r"(vl)
        : "r"(src), "r"(idx16), "r"(dst), "r"(n)
        : "memory", "v0", "v1", "v2", "v3", "v4"
    );
    (void)vl;
}

/* ----- T33: fixed-point averaging add with rounding mode (vaaddu/vxrm) ---
 * vaaddu computes (a+b)>>1 in wider precision (no overflow) with the dropped
 * bit rounded per the vxrm CSR. We set vxrm=0 (round-to-nearest-up). Tests
 * the averaging unit and explicit rounding-mode control via vxrm.
 * ------------------------------------------------------------------------- */
static void vec_avg_u32_rnu(const uint32_t *a, const uint32_t *b,
                            uint32_t *c, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "csrwi     vxrm, 0\n\t"          /* round-to-nearest-up */
            "vsetvli   %0, %4, e32, m1, ta, ma\n\t"
            "vle32.v   v1, (%1)\n\t"
            "vle32.v   v2, (%2)\n\t"
            "vaaddu.vv v3, v1, v2\n\t"
            "vse32.v   v3, (%3)"
            : "=&r"(vl)
            : "r"(a + i), "r"(b + i), "r"(c + i), "r"(n - i)
            : "memory", "v1", "v2", "v3"
        );
        i += vl;
    }
}

/* ----- T34: vector square root (vfsqrt.v) -------------------------------- */
static void vec_fsqrt(const float *a, float *c, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli  %0, %3, e32, m1, ta, ma\n\t"
            "vle32.v  v1, (%1)\n\t"
            "vfsqrt.v v2, v1\n\t"
            "vse32.v  v2, (%2)"
            : "=&r"(vl)
            : "r"(a + i), "r"(c + i), "r"(n - i)
            : "memory", "v1", "v2"
        );
        i += vl;
    }
}

/* ----- T35: scalar splat + whole-vector move (vmv.v.x / vmv.v.v) --------- */
static void vec_splat(uint32_t val, uint32_t *dst, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t vl;
        asm volatile (
            "vsetvli %0, %3, e32, m1, ta, ma\n\t"
            "vmv.v.x v1, %1\n\t"             /* splat scalar to all lanes */
            "vmv.v.v v2, v1\n\t"             /* whole-vector copy */
            "vse32.v v2, (%2)"
            : "=&r"(vl)
            : "r"(val), "r"(dst + i), "r"(n - i)
            : "memory", "v1", "v2"
        );
        i += vl;
    }
}

/* ----- FP compare helpers (libc-free) ------------------------------------ */
static float f32_abs(float x)
{
    uint32_t b; __builtin_memcpy(&b, &x, 4);
    b &= 0x7FFFFFFFu;
    float r; __builtin_memcpy(&r, &b, 4);
    return r;
}
static int f32_close(float a, float b, float tol)
{
    return f32_abs(a - b) <= tol;
}

/* ----- tiny test runner --------------------------------------------------- */
static uint32_t g_fails = 0;

static void report(const char *name, int ok)
{
    uart_puts(name);
    if (ok) {
        uart_puts(" ... PASS\r\n");
    } else {
        uart_puts(" ... FAIL\r\n");
        g_fails++;
    }
}

int main(void)
{
    /* ----- Phase A: bring-up sanity ------------------------------------- */
    uart_puts("\r\n=== bare-metal RV64 + RVA23U64 subset bring-up ===\r\n");

    uint64_t vlenb;
    asm volatile("csrr %0, vlenb" : "=r"(vlenb));
    uart_puts("vlenb (bytes per V-reg): ");
    uart_put_u32((uint32_t)vlenb);
    uart_puts("  (VLEN=");
    uart_put_u32((uint32_t)(vlenb * 8));
    uart_puts(" bits)\r\n");

    float r = accel_sqrtf(2.0f);
    uint32_t bits;
    __builtin_memcpy(&bits, &r, sizeof(bits));
    uart_puts("accel_sqrtf(2.0) bits = ");
    uart_put_hex64((uint64_t)bits);
    uart_puts("   (expect 0x3fb504f3)\r\n");

    float s = accel_sinf(1.0f);
    __builtin_memcpy(&bits, &s, sizeof(bits));
    uart_puts("accel_sinf(1.0) bits  = ");
    uart_put_hex64((uint64_t)bits);
    uart_puts("   (software stub - replace with hw accel.sin)\r\n");

    uint64_t vl_probe;
    asm volatile("vsetvli %0, zero, e32, m1, ta, ma" : "=r"(vl_probe));
    uart_puts("vsetvli (e32,m1) max VL = ");
    uart_put_u32((uint32_t)vl_probe);
    uart_puts(" elements\r\n");

    uart_puts("intrinsics header: " ACCEL_INTRINSICS_VERSION "\r\n");

    /* ----- Phase B: self-test suite ------------------------------------- */
    uart_puts("\r\n=== self-test suite ===\r\n");

    /* T1: u32 vec_add (N=17 -> 4 stripes of VL=4 + tail of 1) */
    {
        enum { N = 17 };
        uint32_t a[N], b[N], c[N];
        for (size_t i = 0; i < N; i++) {
            a[i] = (uint32_t)(i * 3u + 1u);
            b[i] = (uint32_t)(i * 7u + 11u);
        }
        vec_add_u32(a, b, c, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            if (c[i] != a[i] + b[i]) { ok = 0; break; }
        }
        report("T1 u32 vec_add        (vle32/vadd.vv/vse32,   N=17)", ok);
    }

    /* T2: f32 vec_add (N=17). Operands are small exact integers cast to
     *     float so addition is exact and bit-exact compare is safe. */
    {
        enum { N = 17 };
        float a[N], b[N], c[N];
        for (size_t i = 0; i < N; i++) {
            a[i] = (float)(i + 1);
            b[i] = (float)(2 * i + 3);
        }
        vec_add_f32(a, b, c, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            float want = a[i] + b[i];
            uint32_t cb, wb;
            __builtin_memcpy(&cb, &c[i], 4);
            __builtin_memcpy(&wb, &want, 4);
            if (cb != wb) { ok = 0; break; }
        }
        report("T2 f32 vec_add        (vle32/vfadd.vv/vse32,  N=17)", ok);
    }

    /* T3: f32 dot product (N=8 -> 2 stripes of VL=4). Values chosen so
     *     all products and partial sums are exact small integers. */
    {
        enum { N = 8 };
        float a[N], b[N];
        float golden = 0.0f;
        for (size_t i = 0; i < N; i++) {
            a[i] = (float)(i + 1);
            b[i] = (float)(2 * i + 1);
            golden += a[i] * b[i];
        }
        float got = vec_dot_f32(a, b, N);
        uint32_t gb, wb;
        __builtin_memcpy(&gb, &got,    4);
        __builtin_memcpy(&wb, &golden, 4);
        report("T3 f32 dot (reduction)(vfmul/vfredusum/vfmv,  N=8)",
               gb == wb);
    }

    /* T4: u8 memcpy (N=50 -> 3 stripes of VL=16 + tail of 2). */
    {
        enum { N = 50 };
        uint8_t src[N], dst[N];
        for (size_t i = 0; i < N; i++) {
            src[i] = (uint8_t)(0xA5 ^ (i * 31u + 7u));
            dst[i] = 0;
        }
        vec_memcpy_u8(dst, src, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            if (dst[i] != src[i]) { ok = 0; break; }
        }
        report("T4 u8  vec_memcpy     (vle8/vse8,             N=50)", ok);
    }

    /* T5: Zbb cpop (popcount). Validates the bit-manip extension is alive
     *     and that the assembler emits the right encoding. */
    {
        int ok = 1;
        if (scalar_cpop(0) != 0)                          ok = 0;
        if (scalar_cpop(1) != 1)                          ok = 0;
        if (scalar_cpop(0xFFFFFFFFFFFFFFFFULL) != 64)     ok = 0;
        /* 0x123456789ABCDEF0: nibble popcounts
         *   1+1+2+1+2+2+3+1+2+2+3+2+3+3+4+0 = 32 */
        if (scalar_cpop(0x123456789ABCDEF0ULL) != 32)     ok = 0;
        report("T5 Zbb cpop           (4 cases)                    ", ok);
    }

    /* T6: Zfh scalar half-precision add. Hardcoded expected bit patterns
     *     so we actually check the hardware computed the IEEE half result. */
    {
        int ok = 1;
        if (f16_add_bits((_Float16)1.0f,  (_Float16)1.0f)  != 0x4000) ok = 0; /* 2.0  */
        if (f16_add_bits((_Float16)1.5f,  (_Float16)2.25f) != 0x4380) ok = 0; /* 3.75 */
        if (f16_add_bits((_Float16)0.5f,  (_Float16)0.25f) != 0x3A00) ok = 0; /* 0.75 */
        report("T6 Zfh scalar f16 add (fadd.h, 3 cases)            ", ok);
    }

    /* T7: widening i16->i32 add (N=10). Operands overflow 16 bits so the
     *     widening is observable: 30000 + 10000 = 40000 > INT16_MAX. */
    {
        enum { N = 10 };
        int16_t a[N], b[N];
        int32_t c[N];
        for (size_t i = 0; i < N; i++) {
            a[i] = (int16_t)(30000 - (int)i);
            b[i] = (int16_t)(10000 + (int)i * 3);
        }
        vec_widen_add_i16(a, b, c, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            if (c[i] != (int32_t)a[i] + (int32_t)b[i]) { ok = 0; break; }
        }
        report("T7 widening i16->i32  (vwadd.vv, m2 store, N=10)   ", ok);
    }

    /* T8: masked add, even lanes active (N=4, single stripe). */
    {
        enum { N = 4, BG = 99 };
        uint32_t a[N], b[N], c[N];
        for (size_t i = 0; i < N; i++) {
            a[i] = (uint32_t)(i * 10u + 1u);
            b[i] = (uint32_t)(i * 100u + 2u);
        }
        vec_masked_add_even(a, b, c, N, BG);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            uint32_t want = (i % 2 == 0) ? a[i] + b[i] : (uint32_t)BG;
            if (c[i] != want) { ok = 0; break; }
        }
        report("T8 masked add (v0.t)  (vid/vmseq/vadd mu, N=4)     ", ok);
    }

    /* T9: strided gather, every 2nd u32 (stride 8 bytes), N=8. */
    {
        enum { SRCN = 16, N = 8 };
        uint32_t src[SRCN], dst[N];
        for (size_t i = 0; i < SRCN; i++) src[i] = (uint32_t)i;
        vec_gather_stride(src, dst, N, 8 /* bytes = every other element */);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            if (dst[i] != (uint32_t)(2 * i)) { ok = 0; break; }
        }
        report("T9 strided gather     (vlse32.v stride=8, N=8)     ", ok);
    }

    /* T10: saxpy y = alpha*x + y (N=17). Exact-integer operands:
     *      alpha=2, x[i]=i+1, y[i]=3i  =>  result = 5i+2 (exact). */
    {
        enum { N = 17 };
        const float alpha = 2.0f;
        float x[N], y[N];
        for (size_t i = 0; i < N; i++) {
            x[i] = (float)(i + 1);
            y[i] = (float)(3 * i);
        }
        saxpy_f32(alpha, x, y, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            float want = (float)(5 * i + 2);
            uint32_t yb, wb;
            __builtin_memcpy(&yb, &y[i], 4);
            __builtin_memcpy(&wb, &want, 4);
            if (yb != wb) { ok = 0; break; }
        }
        report("T10 saxpy (FMA)       (vfmacc.vf alpha=2, N=17)    ", ok);
    }

    /* T11: accelerator seam. Validates every intrinsic in
     *      accelerator_intrinsics.h against references. sqrt/rsqrt are
     *      hardware (fsqrt.s) so they're exact; sin/cos are the software
     *      Taylor stub, checked within tolerance for |x|<=1 (where the
     *      README promises ~6-decimal accuracy). When the real accelerator
     *      ISA replaces these bodies, this test keeps them honest. */
    {
        int ok = 1;
        /* sqrt - exact / known IEEE bits */
        if (accel_sqrtf(4.0f) != 2.0f) ok = 0;
        if (accel_sqrtf(1.0f) != 1.0f) ok = 0;
        if (accel_sqrtf(0.0f) != 0.0f) ok = 0;
        { uint32_t b; float v = accel_sqrtf(2.0f);
          __builtin_memcpy(&b, &v, 4);
          if (b != 0x3fb504f3u) ok = 0; }
        /* rsqrt(4) = 0.5 exactly */
        if (!f32_close(accel_rsqrtf(4.0f), 0.5f, 1e-6f)) ok = 0;
        /* sin/cos within tolerance for small args */
        if (accel_sinf(0.0f) != 0.0f) ok = 0;
        if (!f32_close(accel_sinf(0.5f), 0.4794255386f, 1e-3f)) ok = 0;
        if (!f32_close(accel_sinf(1.0f), 0.8414709848f, 1e-3f)) ok = 0;
        if (!f32_close(accel_cosf(0.0f), 1.0f,          1e-3f)) ok = 0;
        if (!f32_close(accel_cosf(0.5f), 0.8775825619f, 1e-3f)) ok = 0;
        /* sincos must agree with the separate sin/cos */
        { float sc_s, sc_c; accel_sincosf(0.5f, &sc_s, &sc_c);
          if (!f32_close(sc_s, accel_sinf(0.5f), 1e-6f)) ok = 0;
          if (!f32_close(sc_c, accel_cosf(0.5f), 1e-6f)) ok = 0; }
        report("T11 accel seam        (sqrt/rsqrt/sin/cos/sincos)   ", ok);
    }

    /* T12: atan2 is a documented stub that returns 0. Pin that behavior so
     *      this test FAILS the day a real hardware accel.atan2 lands -
     *      forcing whoever wires it up to come back and add real checks. */
    report("T12 accel atan2 stub  (pinned: returns 0 by design)",
           accel_atan2f(1.0f, 1.0f) == 0.0f);

    /* T13: trap handler. Deliberately fault twice and verify trap.S
     *      captured the right mcause AND returned (we only reach the
     *      report() call if mret resumed us; the old handler hung in wfi).
     *      The "memory" clobbers force the compiler to reload _trap_info
     *      after each fault, since it can't see the handler's writes. */
    {
        int ok = 1;
        uint64_t before = _trap_info[3];

        asm volatile("ecall" ::: "memory");          /* -> mcause 11 */
        if (_trap_info[3] != before + 1)             ok = 0;
        if (_trap_info[0] != MCAUSE_ECALL_FROM_M)    ok = 0;

        asm volatile(".4byte 0xffffffff" ::: "memory"); /* guaranteed illegal */
        if (_trap_info[3] != before + 2)             ok = 0;
        if (_trap_info[0] != MCAUSE_ILLEGAL_INSTR)   ok = 0;

        report("T13 trap capture+resume(ecall->11, illegal->2)      ", ok);
    }

    /* T14: segment load deinterleave (N=6 pairs -> 2 stripes). */
    {
        enum { N = 6 };
        uint32_t xy[2 * N], xs[N], ys[N];
        for (size_t i = 0; i < N; i++) {
            xy[2 * i + 0] = (uint32_t)(10 + i);   /* x */
            xy[2 * i + 1] = (uint32_t)(20 + i);   /* y */
        }
        vec_deinterleave2_u32(xy, xs, ys, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            if (xs[i] != (uint32_t)(10 + i)) { ok = 0; break; }
            if (ys[i] != (uint32_t)(20 + i)) { ok = 0; break; }
        }
        report("T14 segment load      (vlseg2e32 deinterleave, N=6) ", ok);
    }

    /* T15: indexed gather (N=8 -> 2 stripes). */
    {
        enum { BASEN = 16, N = 8 };
        uint32_t base[BASEN], dst[N];
        const uint32_t idx[N] = { 3, 1, 4, 1, 5, 9, 2, 6 };
        for (size_t i = 0; i < BASEN; i++) base[i] = (uint32_t)(100 + i);
        vec_gather_index(base, idx, dst, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            if (dst[i] != base[idx[i]]) { ok = 0; break; }
        }
        report("T15 indexed gather    (vluxei32 base[idx], N=8)     ", ok);
    }

    /* T16: vrgather reverse (N=8, single m2 stripe). */
    {
        enum { N = 8 };
        uint32_t src[N], dst[N];
        for (size_t i = 0; i < N; i++) src[i] = (uint32_t)((i + 1) * 11u);
        vec_reverse8_u32(src, dst, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            if (dst[i] != src[N - 1 - i]) { ok = 0; break; }
        }
        report("T16 vrgather reverse  (vid/vrsub/vrgather m2, N=8)  ", ok);
    }

    /* T17: extended scalar bit-manip (Zbb + Zbs). */
    {
        int ok = 1;
        /* Zbb */
        if (zbb_clz(1)                       != 63) ok = 0;
        if (zbb_clz(0)                       != 64) ok = 0;
        if (zbb_clz(0x8000000000000000ULL)   != 0)  ok = 0;
        if (zbb_ctz(0x100)                   != 8)  ok = 0;
        if (zbb_ctz(0)                       != 64) ok = 0;
        if (zbb_rev8(0x0102030405060708ULL)  != 0x0807060504030201ULL) ok = 0;
        if (zbb_max(-1, 5)                   != 5)  ok = 0;   /* signed   */
        if (zbb_minu((uint64_t)-1, 5)        != 5)  ok = 0;   /* unsigned */
        /* Zbs */
        if (zbs_bset(0, 5)                   != 32) ok = 0;
        if (zbs_bclr(0xFF, 0)                != 0xFE) ok = 0;
        if (zbs_bext(0xA, 1)                 != 1)  ok = 0;
        if (zbs_bext(0xA, 0)                 != 0)  ok = 0;
        if (zbs_binv(0, 3)                   != 8)  ok = 0;
        report("T17 Zbb+Zbs bit-manip (clz/ctz/rev8/max/bset/bext) ", ok);
    }

    /* T18: vcompress, pack even-index lanes (N=8 -> 4 packed). */
    {
        enum { N = 8 };
        uint32_t src[N], dst[N] = {0};
        for (size_t i = 0; i < N; i++) src[i] = (uint32_t)(10 + i);
        size_t cnt = vec_compress_even(src, dst, N);
        int ok = (cnt == 4);
        for (size_t j = 0; j < cnt && ok; j++) {
            if (dst[j] != src[2 * j]) ok = 0;     /* expect 10,12,14,16 */
        }
        report("T18 vcompress         (pack even lanes, N=8->4)     ", ok);
    }

    /* T19: vector mask logic. data>4 AND even -> {idx 1,3}; cnt=2 first=1. */
    {
        enum { N = 8 };
        const int32_t data[N] = { 3, 8, 1, 6, 9, 2, 7, 4 };
        uint64_t cnt; int64_t first;
        vec_mask_logic(data, N, &cnt, &first);
        report("T19 mask logic        (vmsgt/vmand/vcpop/vfirst)    ",
               cnt == 2 && first == 1);
    }

    /* T20: integer max/min reductions (N=17, strip-mined). */
    {
        enum { N = 17 };
        int32_t a[N];
        int32_t gmax = I32_MIN, gmin = I32_MAX;
        for (size_t i = 0; i < N; i++) {
            /* spread of positives and negatives, peaks not at the ends */
            a[i] = (int32_t)(((i * 7 + 3) % 23) - 11) * (i & 1 ? -1 : 1);
            if (a[i] > gmax) gmax = a[i];
            if (a[i] < gmin) gmin = a[i];
        }
        int ok = (vec_redmax_i32(a, N) == gmax) &&
                 (vec_redmin_i32(a, N) == gmin);
        report("T20 max/min reduction (vredmax.vs/vredmin.vs, N=17) ", ok);
    }

    /* T21: saturating add, unsigned + signed (e8). */
    {
        enum { N = 8 };
        uint8_t ua[N] = {200,  10, 255, 100,  50,   0, 128, 130};
        uint8_t ub[N] = {100,  20,   1, 200,  50, 255, 128, 130};
        uint8_t uc[N];
        int8_t  ia[N] = { 100, -100,  50, -50,  10, -10, 127, -128};
        int8_t  ib[N] = {  50,  -50,  10, -10,   5,  -5,   1,   -1};
        int8_t  ic[N];
        vec_satadd_u8(ua, ub, uc, N);
        vec_satadd_i8(ia, ib, ic, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            unsigned us = (unsigned)ua[i] + (unsigned)ub[i];
            uint8_t  uw = us > 255u ? 255u : (uint8_t)us;
            int      ss = (int)ia[i] + (int)ib[i];
            int8_t   sw = ss > 127 ? 127 : (ss < -128 ? -128 : (int8_t)ss);
            if (uc[i] != uw || ic[i] != sw) { ok = 0; break; }
        }
        report("T21 saturating add    (vsaddu/vsadd e8, N=8)        ", ok);
    }

    /* T22: segment store interleave (N=6 pairs -> 2 stripes). */
    {
        enum { N = 6 };
        uint32_t xs[N], ys[N], xy[2 * N];
        for (size_t i = 0; i < N; i++) {
            xs[i] = (uint32_t)(10 + i);
            ys[i] = (uint32_t)(20 + i);
        }
        vec_interleave2_u32(xs, ys, xy, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            if (xy[2 * i + 0] != (uint32_t)(10 + i)) { ok = 0; break; }
            if (xy[2 * i + 1] != (uint32_t)(20 + i)) { ok = 0; break; }
        }
        report("T22 segment store     (vsseg2e32 interleave, N=6)   ", ok);
    }

    /* T23: float max/min reductions (N=9, strip-mined, tail of 1). */
    {
        enum { N = 9 };
        const float a[N] = { 3.0f, -7.0f, 42.0f, 8.0f, -1.0f,
                             17.0f, 42.0f, 5.0f, -3.0f };
        int ok = (vec_fredmax(a, N) == 42.0f) &&
                 (vec_fredmin(a, N) == -7.0f);
        report("T23 float max/min red (vfredmax.vs/vfredmin.vs, N=9)", ok);
    }

    /* T24: fractional-LMUL zero-extend e8->e32 (N=10 -> tail). */
    {
        enum { N = 10 };
        uint8_t  src[N] = { 0x80, 0x01, 0xFF, 0x00, 0x7F,
                            0xAA, 0x55, 0xC3, 0x10, 0xEE };
        uint32_t dst[N];
        vec_zext8to32(src, dst, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            if (dst[i] != (uint32_t)src[i]) { ok = 0; break; }   /* zero-extend */
        }
        report("T24 frac-LMUL zext     (vzext.vf4 e8->e32, N=10)    ", ok);
    }

    /* T25: narrowing e32->e16 (N=10 -> tail). */
    {
        enum { N = 10 };
        uint32_t src[N];
        uint16_t dst[N];
        for (size_t i = 0; i < N; i++) {
            src[i] = (uint32_t)((i * 0x11110000u) | (0x1000u + i));
        }
        vec_narrow_32to16(src, dst, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            if (dst[i] != (uint16_t)src[i]) { ok = 0; break; }   /* low 16 bits */
        }
        report("T25 narrowing 32->16  (vnsrl.wi truncate, N=10)    ", ok);
    }

    /* T26: Zba shift-add (was enabled in -march but previously untested). */
    {
        int ok = 1;
        if (zba_sh1add(5, 3) != ((5u << 1) + 3))  ok = 0;   /* 13 */
        if (zba_sh2add(5, 3) != ((5u << 2) + 3))  ok = 0;   /* 23 */
        if (zba_sh3add(5, 3) != ((5u << 3) + 3))  ok = 0;   /* 43 */
        /* add.uw zero-extends low 32 of rs1 then adds rs2 */
        if (zba_adduw(0xFFFFFFFF00000005ULL, 10) != 15) ok = 0;
        report("T26 Zba shift-add     (sh1/2/3add, add.uw)         ", ok);
    }

    /* T27: FP <-> int conversions (N=7). */
    {
        enum { N = 7 };
        const float   fsrc[N] = { 1.9f, -2.7f, 3.0f, -0.5f, 100.6f, -100.9f, 0.0f };
        const int32_t isrc[N] = { 1, -2, 3, 0, 100, -100, 7 };
        int32_t idst[N];
        float   fdst[N];
        vec_f2i_rtz(fsrc, idst, N);
        vec_i2f(isrc, fdst, N);
        int ok = 1;
        const int32_t iwant[N] = { 1, -2, 3, 0, 100, -100, 0 }; /* round-to-zero */
        for (size_t i = 0; i < N; i++) {
            if (idst[i] != iwant[i]) { ok = 0; break; }
            float w = (float)isrc[i];
            uint32_t db, wb;
            __builtin_memcpy(&db, &fdst[i], 4);
            __builtin_memcpy(&wb, &w, 4);
            if (db != wb) { ok = 0; break; }
        }
        report("T27 FP<->int convert  (vfcvt.rtz.x.f / f.x, N=7)   ", ok);
    }

    /* T28: multi-precision 64-bit add via carry chain (N=8). */
    {
        enum { N = 8 };
        uint64_t A[N], B[N];
        uint32_t alo[N], ahi[N], blo[N], bhi[N], rlo[N], rhi[N];
        A[0]=0x00000000FFFFFFFFULL; B[0]=0x0000000000000001ULL; /* carry across limb */
        A[1]=0x1234567880000000ULL; B[1]=0x0000000180000000ULL; /* carry + hi add  */
        A[2]=0xFFFFFFFFFFFFFFFFULL; B[2]=0x0000000000000001ULL; /* full wraparound  */
        A[3]=0x00000000DEADBEEFULL; B[3]=0x0000000000000000ULL; /* no carry         */
        A[4]=0xAAAAAAAA55555555ULL; B[4]=0x5555555655555556ULL;
        A[5]=0x0000000100000000ULL; B[5]=0x00000000FFFFFFFFULL;
        A[6]=0x7FFFFFFFFFFFFFFFULL; B[6]=0x0000000000000001ULL;
        A[7]=0x0123456789ABCDEFULL; B[7]=0xFEDCBA9876543210ULL;
        for (size_t i = 0; i < N; i++) {
            alo[i]=(uint32_t)A[i]; ahi[i]=(uint32_t)(A[i]>>32);
            blo[i]=(uint32_t)B[i]; bhi[i]=(uint32_t)(B[i]>>32);
        }
        vec_add64(alo, ahi, blo, bhi, rlo, rhi, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            uint64_t want = A[i] + B[i];
            uint64_t got  = ((uint64_t)rhi[i] << 32) | rlo[i];
            if (got != want) { ok = 0; break; }
        }
        report("T28 multiprec add64   (vmadc/vadc carry chain, N=8)", ok);
    }

    /* T29: slides (N=8 single m2 stripe). */
    {
        enum { N = 8, HEAD = 99 };
        uint32_t src[N], down[N], up[N];
        for (size_t i = 0; i < N; i++) src[i] = (uint32_t)((i + 1) * 10u);
        vec_slidedown2(src, down, N);
        vec_slide1up(src, up, N, HEAD);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            uint32_t wd = (i + 2 < N) ? src[i + 2] : 0u;     /* zero-filled tail */
            uint32_t wu = (i == 0) ? (uint32_t)HEAD : src[i - 1];
            if (down[i] != wd || up[i] != wu) { ok = 0; break; }
        }
        report("T29 slides            (vslidedown.vi/vslide1up, N=8)", ok);
    }

    /* T30: integer divide + remainder (N=8, no zero divisors). */
    {
        enum { N = 8 };
        const uint32_t a[N] = { 100, 7, 255, 1000, 0xFFFFFFFFu, 42, 1, 999999 };
        const uint32_t b[N] = {   7, 3,  16,   13,         255,  6, 7,    1000 };
        uint32_t q[N], r[N];
        vec_divrem_u32(a, b, q, r, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            if (q[i] != a[i] / b[i] || r[i] != a[i] % b[i]) { ok = 0; break; }
        }
        report("T30 int div+rem       (vdivu.vv/vremu.vv, N=8)      ", ok);
    }

    /* T31: vmerge select, even lanes from t[], odd from f[] (N=8). */
    {
        enum { N = 8 };
        uint32_t t[N], f[N], dst[N];
        for (size_t i = 0; i < N; i++) { t[i] = (uint32_t)(1000 + i); f[i] = (uint32_t)(2000 + i); }
        vec_merge_even(t, f, dst, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            uint32_t want = (i % 2 == 0) ? t[i] : f[i];
            if (dst[i] != want) { ok = 0; break; }
        }
        report("T31 vmerge select     (vmerge.vvm even?t:f, N=8)    ", ok);
    }

    /* T32: 16-bit-indexed gather (N=8). */
    {
        enum { N = 8 };
        uint32_t src[N], dst[N];
        const uint16_t idx[N] = { 7, 0, 3, 5, 1, 6, 2, 4 };
        for (size_t i = 0; i < N; i++) src[i] = (uint32_t)(100 + i);
        vec_gather_ei16(src, idx, dst, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            if (dst[i] != src[idx[i]]) { ok = 0; break; }
        }
        report("T32 ei16 gather       (vrgatherei16.vv, N=8)        ", ok);
    }

    /* T33: averaging add, round-to-nearest-up (N=8). */
    {
        enum { N = 8 };
        const uint32_t a[N] = { 5, 4, 0xFFFFFFFFu, 100, 7, 0, 0xFFFFFFFEu, 12345 };
        const uint32_t b[N] = { 2, 2, 0xFFFFFFFFu,   7, 8, 1, 0x00000001u, 54321 };
        uint32_t c[N];
        vec_avg_u32_rnu(a, b, c, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            uint32_t want = (uint32_t)(((uint64_t)a[i] + b[i] + 1) >> 1);  /* rnu */
            if (c[i] != want) { ok = 0; break; }
        }
        report("T33 avg add (vxrm rnu)(vaaddu.vv, N=8)              ", ok);
    }

    /* T34: vector sqrt - perfect squares give exact results (N=8). */
    {
        enum { N = 8 };
        const float a[N] = { 1.0f, 4.0f, 9.0f, 16.0f, 25.0f, 36.0f, 49.0f, 64.0f };
        float c[N];
        vec_fsqrt(a, c, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            if (c[i] != (float)(i + 1)) { ok = 0; break; }   /* 1,2,...,8 exactly */
        }
        report("T34 vector sqrt       (vfsqrt.v perfect sq, N=8)    ", ok);
    }

    /* T35: scalar splat + whole-vector move (N=10). */
    {
        enum { N = 10 };
        const uint32_t VAL = 0xDEADBEEFu;
        uint32_t dst[N];
        vec_splat(VAL, dst, N);
        int ok = 1;
        for (size_t i = 0; i < N; i++) {
            if (dst[i] != VAL) { ok = 0; break; }
        }
        report("T35 splat + vmv copy  (vmv.v.x/vmv.v.v, N=10)       ", ok);
    }

#ifdef SELFTEST_INJECT_FAIL
    /* Negative-path check: build with -DSELFTEST_INJECT_FAIL to verify the
     * failure exit code actually propagates out through QEMU. */
    report("TX injected failure   (negative-path check)        ", 0);
#endif

    /* ----- Summary ------------------------------------------------------ */
    uart_puts("\r\n=== summary: ");
    if (g_fails == 0) {
        uart_puts("ALL PASS ===\r\n");
        qemu_test_exit(0);
    } else {
        uart_put_u32(g_fails);
        uart_puts(" FAILED ===\r\n");
        qemu_test_exit(g_fails);
    }
}
