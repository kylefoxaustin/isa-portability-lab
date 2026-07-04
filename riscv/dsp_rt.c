/*
 * dsp_rt.c - minimal freestanding runtime for the DSP equivalence harness.
 *
 * The shared on-target harness (../harness/run_kernels.c) is written to a
 * hosted contract: it uses printf() for its PASS/FAIL lines and returns an
 * exit code from main(). On the Xtensa -M sim / xt-run legs that contract is
 * satisfied for free by newlib + SIMCALL semihosting. This RISC-V leg is
 * deliberately -nostdlib bare-metal (see crt0.S / link.ld), so we supply the
 * few libc symbols the harness needs, routed to the QEMU 'virt' UART - no
 * newlib, no semihosting, fully under our control.
 *
 * Only these are needed by run_kernels.c + the kernels:
 *   - printf  : just the conversions the harness uses (%s with '-'/width,
 *               %d, %lld) plus literal text.
 *   - mem*    : safety net in case GCC materializes a struct/array copy as a
 *               libcall despite -fno-builtin.
 *
 * The exit code is handled in crt0_dsp.S (main's return -> sifive_test).
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#define QEMU_VIRT_UART0 ((volatile uint8_t *)0x10000000)

static void rt_putc(char c) { *QEMU_VIRT_UART0 = (uint8_t)c; }
static void rt_puts(const char *s) { while (*s) rt_putc(*s++); }

/* emit a string padded to `width`; left-justified when `left`. */
static void rt_pad_str(const char *s, int width, int left)
{
    int len = 0;
    for (const char *p = s; *p; p++) len++;
    int pad = width - len;
    if (!left) while (pad-- > 0) rt_putc(' ');
    rt_puts(s);
    if (left) while (pad-- > 0) rt_putc(' ');
}

/* signed 64-bit decimal into buf (returns pointer to start within buf). */
static char *rt_i64(long long v, char *end)
{
    unsigned long long u = (v < 0) ? (unsigned long long)(-(v + 1)) + 1ull
                                   : (unsigned long long)v;
    char *p = end;
    *--p = '\0';
    char *digits = p;
    do { *--digits = (char)('0' + (int)(u % 10ull)); u /= 10ull; } while (u);
    if (v < 0) *--digits = '-';
    return digits;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char numbuf[24];
    for (const char *f = fmt; *f; f++) {
        if (*f != '%') { rt_putc(*f); continue; }
        f++;
        int left = 0, width = 0, lcount = 0;
        while (*f == '-') { left = 1; f++; }
        while (*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); f++; }
        while (*f == 'l') { lcount++; f++; }
        switch (*f) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            rt_pad_str(s ? s : "(null)", width, left);
            break;
        }
        case 'd': {
            long long v = (lcount >= 1) ? va_arg(ap, long long)
                                        : (long long)va_arg(ap, int);
            rt_pad_str(rt_i64(v, numbuf + sizeof numbuf), width, left);
            break;
        }
        case '%': rt_putc('%'); break;
        default:  rt_putc('%'); rt_putc(*f); break;   /* unknown: echo */
        }
    }
    va_end(ap);
    return 0;
}

/* --- freestanding mem* safety net (only linked if referenced) ------------ */
void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *dd = d; const uint8_t *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}
void *memset(void *d, int c, size_t n)
{
    uint8_t *dd = d;
    while (n--) *dd++ = (uint8_t)c;
    return d;
}
void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *dd = d; const uint8_t *ss = s;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}
