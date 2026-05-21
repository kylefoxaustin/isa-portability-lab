/*
 * portable/probes/p_extra.c - drop-in demo.
 *
 * This file was added with NO edits to any Makefile or central table: the
 * REGISTER_PROBE registry + linker-section walk pick it up on both legs.
 */
#include <stdint.h>
#include "probe.h"

static uint32_t k_fib(void)
{
    uint32_t f[48]; f[0] = 0; f[1] = 1;
    for (int i = 2; i < 48; i++) f[i] = f[i - 1] + f[i - 2];   /* wraps in uint32 */
    return probe_fnv1a(f, sizeof f);
}

static uint32_t k_bitrev(void)
{
    uint32_t out[32];
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t x = i * 0x9E3779B1u, r = 0;
        for (int b = 0; b < 32; b++) { r = (r << 1) | (x & 1u); x >>= 1; }
        out[i] = r;
    }
    return probe_fnv1a(out, sizeof out);
}

REGISTER_PROBE("K13 fib_u32     ", k_fib);
REGISTER_PROBE("K14 bitreverse  ", k_bitrev);
