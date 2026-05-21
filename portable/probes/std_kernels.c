/*
 * portable/probes/std_kernels.c - the standard, should-be-portable probe set.
 *
 * Pure portable C (fixed-width types, no inline asm). Each kernel registers
 * itself via REGISTER_PROBE, so this file is auto-discovered by both target
 * builds. Drop another .c next to this one to add probes - no table edits.
 */
#include <stdint.h>
#include <stddef.h>
#include "probe.h"

static uint32_t k_int_arith(void)
{
    uint32_t acc[64];
    for (uint32_t i = 0; i < 64; i++)
        acc[i] = (i * 7u + 3u) ^ (i << 3) ^ (i * i);
    return probe_fnv1a(acc, sizeof acc);
}

static uint32_t k_int_divmod(void)
{
    uint32_t acc[64];
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t x = i * 2654435761u + 1u;
        acc[i] = (x / 97u) + (x % 101u) * 7u;
    }
    return probe_fnv1a(acc, sizeof acc);
}

static uint32_t k_crc32(void)
{
    uint8_t buf[120];
    for (int i = 0; i < 120; i++) buf[i] = (uint8_t)(i * 31 + 7);
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < 120; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
    }
    return ~crc;
}

static uint32_t k_fnv_hash(void)
{
    static const char *const msgs[] =
        { "cortex-m7", "riscv", "portability", "FNV-1a", "baremetal" };
    uint32_t h = 2166136261u;
    for (int m = 0; m < 5; m++) {
        const char *s = msgs[m];
        while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    }
    return h;
}

static uint32_t k_float_saxpy(void)
{
    float x[48], y[48];
    for (int i = 0; i < 48; i++) { x[i] = (float)i * 0.5f + 1.0f; y[i] = (float)(48 - i) * 0.25f; }
    const float a = 3.0f;
    for (int i = 0; i < 48; i++) y[i] = a * x[i] + y[i];
    return probe_fnv1a(y, sizeof y);
}

static uint32_t k_float_dot(void)
{
    float x[64], y[64];
    for (int i = 0; i < 64; i++) { x[i] = (float)i * 0.125f; y[i] = (float)(i % 7) * 0.5f - 1.0f; }
    float s = 0.0f;
    for (int i = 0; i < 64; i++) s += x[i] * y[i];
    return probe_fnv1a(&s, sizeof s);
}

static uint32_t k_double_poly(void)
{
    double acc[32];
    for (int i = 0; i < 32; i++) {
        double t = (double)i * 0.1;
        acc[i] = ((1.0 * t + 2.0) * t + 3.0) * t + 4.0;
    }
    return probe_fnv1a(acc, sizeof acc);
}

static uint32_t k_char_sign(void)
{
    char buf[16];
    for (int i = 0; i < 16; i++) buf[i] = (char)(i * 17);
    int32_t acc = 0;
    for (int i = 0; i < 16; i++) acc += (int)buf[i];
    return (uint32_t)acc;
}

static uint32_t k_shifts(void)
{
    uint32_t acc[32];
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t x = 0x80000001u + i * 0x01010101u;
        uint32_t rot = (x << 5) | (x >> 27);
        acc[i] = rot ^ (x >> 3);
    }
    return probe_fnv1a(acc, sizeof acc);
}

static uint32_t k_memops(void)
{
    uint8_t a[100], b[100];
    for (int i = 0; i < 100; i++) a[i] = (uint8_t)(i * 13 + 1);
    for (int i = 0; i < 100; i++) b[i] = a[i];
    for (int i = 0; i < 50; i++) { uint8_t t = b[i]; b[i] = b[99 - i]; b[99 - i] = t; }
    return probe_fnv1a(b, sizeof b);
}

static uint32_t k_sort(void)
{
    int32_t arr[40];
    uint32_t seed = 12345u;
    for (int i = 0; i < 40; i++) { seed = seed * 1103515245u + 12345u; arr[i] = (int32_t)(seed >> 16); }
    for (int i = 1; i < 40; i++) {
        int32_t key = arr[i]; int j = i - 1;
        while (j >= 0 && arr[j] > key) { arr[j + 1] = arr[j]; j--; }
        arr[j + 1] = key;
    }
    return probe_fnv1a(arr, sizeof arr);
}

static uint32_t k_matmul(void)
{
    int32_t A[6][6], B[6][6], C[6][6];
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 6; j++) { A[i][j] = (i * 6 + j) % 7 - 3; B[i][j] = (j * 6 + i) % 5 - 2; }
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 6; j++) {
            int32_t s = 0;
            for (int k = 0; k < 6; k++) s += A[i][k] * B[k][j];
            C[i][j] = s;
        }
    return probe_fnv1a(C, sizeof C);
}

REGISTER_PROBE("K01 int_arith   ", k_int_arith);
REGISTER_PROBE("K02 int_divmod  ", k_int_divmod);
REGISTER_PROBE("K03 crc32       ", k_crc32);
REGISTER_PROBE("K04 fnv1a_hash  ", k_fnv_hash);
REGISTER_PROBE("K05 float_saxpy ", k_float_saxpy);
REGISTER_PROBE("K06 float_dot   ", k_float_dot);
REGISTER_PROBE("K07 double_poly ", k_double_poly);
REGISTER_PROBE("K08 char_sign   ", k_char_sign);
REGISTER_PROBE("K09 shifts_rot  ", k_shifts);
REGISTER_PROBE("K10 memops_rev  ", k_memops);
REGISTER_PROBE("K11 insertsort  ", k_sort);
REGISTER_PROBE("K12 matmul6x6   ", k_matmul);
