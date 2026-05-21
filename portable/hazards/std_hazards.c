/*
 * portable/hazards/std_hazards.c - DELIBERATELY non-portable probe set.
 *
 * Each probe diverges between rv64 (LP64) and Cortex-M7 (ILP32). Run via
 * `make compare-hazards`; the XX rows it produces demonstrate the harness
 * catching real porting hazards. H02 is the fixed-width fix for H01 and
 * matches on both targets.
 */
#include <stdint.h>
#include <stddef.h>
#include "probe.h"

/* H01: wide product assigned to `long` - overflows 32-bit long on M7. */
static uint32_t h_long_mul(void)
{
    unsigned long p = 100000UL * 100000UL;
    uint64_t v = (uint64_t)p;
    return probe_fnv1a(&v, sizeof v);
}

/* H02: same math, fixed-width - the portable fix (matches on both). */
static uint32_t h_fixed_mul(void)
{
    uint64_t p = (uint64_t)100000u * 100000u;
    return probe_fnv1a(&p, sizeof p);
}

/* H03: unsigned long wraps at a width-dependent boundary. */
static uint32_t h_long_wrap(void)
{
    unsigned long x = 0xFFFFFFFFUL;
    x += 1UL;
    uint64_t v = (uint64_t)x;
    return probe_fnv1a(&v, sizeof v);
}

/* H04: struct size/layout driven by sizeof(long). */
static uint32_t h_struct_long(void)
{
    struct S { char c; long v; int i; };
    uint32_t f[3] = {
        (uint32_t)sizeof(struct S),
        (uint32_t)__builtin_offsetof(struct S, v),
        (uint32_t)__builtin_offsetof(struct S, i),
    };
    return probe_fnv1a(f, sizeof f);
}

/* H05: long double width (8 on ARM=double, 16 on rv64=quad). */
static uint32_t h_longdouble(void)
{
    uint32_t s = (uint32_t)sizeof(long double);
    return probe_fnv1a(&s, sizeof s);
}

REGISTER_PROBE("H01 long_mul    ", h_long_mul);
REGISTER_PROBE("H02 fixed_mul   ", h_fixed_mul);
REGISTER_PROBE("H03 long_wrap   ", h_long_wrap);
REGISTER_PROBE("H04 struct_long ", h_struct_long);
REGISTER_PROBE("H05 longdouble  ", h_longdouble);
