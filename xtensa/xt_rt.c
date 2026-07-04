/*
 * xt_rt.c - Xtensa runtime glue for the shared DSP harness under QEMU -M sim.
 *
 * The shared on-target harness (../harness/run_kernels.c) is written to a
 * hosted contract: printf() for its PASS/FAIL lines and an exit code from
 * main(). This leg satisfies that with picolibc + QEMU xtensa semihosting
 * (the `simcall` instruction), no Cadence LSP needed:
 *
 *   - stdout : picolibc's tinystdio needs the app to define `stdout`; we back
 *              it with a semihosting SYS_write putc.
 *   - exit   : picolibc's crt0-semihost _cstart does `main(); spin` here - it
 *              DROPS main()'s return. So run_kernels.c is compiled with
 *              -Dmain=dsp_main and we provide the real main() that runs it and
 *              forwards its exit code to QEMU via semihosting SYS_exit. That
 *              exit code (0 == all kernels bit-exact vs golden) is the CI gate.
 *
 * QEMU xtensa semihosting simcall ABI: a2=syscall, a3..a5=args, result in a2.
 *   SYS_write = 4 (a3=fd, a4=buf, a5=len);  SYS_exit = 1 (a3=code).
 */
#include <stdio.h>

extern int dsp_main(void);            /* run_kernels.c's main(), renamed */

static int sh_putc(char c, FILE *f)
{
    (void)f;
    char b = c;
    register int a2 asm("a2") = 4;    /* SYS_write */
    register int a3 asm("a3") = 1;    /* fd = stdout */
    register void *a4 asm("a4") = &b;
    register int a5 asm("a5") = 1;    /* len */
    asm volatile("simcall" : "+r"(a2), "+r"(a3), "+r"(a4), "+r"(a5) :: "memory");
    return c;
}

static FILE __stdio = FDEV_SETUP_STREAM(sh_putc, NULL, NULL, _FDEV_SETUP_WRITE);
FILE *const stdout = &__stdio;

int main(void)
{
    int rc = dsp_main();
    register int a2 asm("a2") = 1;    /* SYS_exit */
    register int a3 asm("a3") = rc;
    asm volatile("simcall" : "+r"(a2), "+r"(a3) :: "memory");
    for (;;) { }
}
