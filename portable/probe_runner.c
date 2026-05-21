/*
 * portable/probe_runner.c - shared driver for the probe registry.
 *
 * Walks the `probes` linker section (filled by REGISTER_PROBE), runs every
 * probe in a deterministic name-sorted order, and emits one
 * `<name> = 0x<checksum>` line each plus a combined hash. Output is
 * byte-identical across targets so the dispatcher can diff it.
 */
#include <stdint.h>
#include <stddef.h>
#include "probe.h"

/* Section bounds (the linker scripts bracket the `probes` section). */
extern const struct probe __start_probes[];
extern const struct probe __stop_probes[];

static void (*g_emit)(char);
static void p_putc(char c)        { g_emit(c); }
static void p_puts(const char *s) { while (*s) p_putc(*s++); }
static void p_u32(uint32_t v)
{
    if (!v) { p_putc('0'); return; }
    char b[10]; int n = 0;
    while (v) { b[n++] = (char)('0' + v % 10); v /= 10; }
    while (n--) p_putc(b[n]);
}
static void p_hex32(uint32_t v)
{
    p_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        int nib = (int)((v >> i) & 0xF);
        p_putc(nib < 10 ? (char)('0' + nib) : (char)('a' + nib - 10));
    }
}
static int p_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

void portable_run(void (*emit)(char))
{
    g_emit = emit;

    p_puts("=== portable probe (M7 <-> RISC-V) ===\r\n");

    /* ABI / type facts - informative for porting, reported every run. */
    /* char signedness is a compile-time property; GCC exposes it directly. */
#ifdef __CHAR_UNSIGNED__
    unsigned char_signed = 0;
#else
    unsigned char_signed = 1;
#endif
    p_puts("info char_signed = "); p_u32(char_signed);             p_puts("\r\n");
    p_puts("info sizeof_int  = "); p_u32((uint32_t)sizeof(int));    p_puts("\r\n");
    p_puts("info sizeof_long = "); p_u32((uint32_t)sizeof(long));   p_puts("\r\n");
    p_puts("info sizeof_ptr  = "); p_u32((uint32_t)sizeof(void *)); p_puts("\r\n");

    /* Collect + name-sort the registered probes so output order is stable
     * regardless of link order (keeps the two targets' lines aligned). */
    const struct probe *ord[256];
    size_t n = 0;
    for (const struct probe *p = __start_probes; p < __stop_probes && n < 256; ++p)
        ord[n++] = p;
    for (size_t i = 1; i < n; i++) {
        const struct probe *key = ord[i];
        size_t j = i;
        while (j > 0 && p_strcmp(ord[j - 1]->name, key->name) > 0) { ord[j] = ord[j - 1]; j--; }
        ord[j] = key;
    }

    uint32_t combined = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        uint32_t h = ord[i]->fn();
        p_puts(ord[i]->name); p_puts("= "); p_hex32(h); p_puts("\r\n");
        combined ^= h; combined *= 16777619u;
    }
    p_puts("=== combined = "); p_hex32(combined); p_puts(" ===\r\n");
}
