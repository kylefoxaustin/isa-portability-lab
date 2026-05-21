/*
 * main.c - Cortex-M7 leg of the portability comparison.
 *
 * Platform glue only: ARM semihosting for character output and exit. The
 * computation is the shared portable_run() in ../portable/kernels.c, the same
 * source compiled for RISC-V. Output text is byte-identical across targets.
 */
#include <stdint.h>
#include "probe.h"          /* -I../portable */

/* ARM semihosting call: r0 = operation, r1 = argument block. */
static int semihost(int op, void *arg)
{
    register int   r0 asm("r0") = op;
    register void *r1 asm("r1") = arg;
    asm volatile ("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

static void sh_putc(char c)
{
    semihost(0x03, &c);                       /* SYS_WRITEC */
}

static void sh_exit(int code)
{
    uint32_t block[2] = { 0x20026u, (uint32_t)code }; /* ADP_Stopped_ApplicationExit, code */
    semihost(0x20, block);                    /* SYS_EXIT_EXTENDED */
    for (;;) { }
}

int main(void)
{
    portable_run(sh_putc);
    sh_exit(0);
    return 0;
}
