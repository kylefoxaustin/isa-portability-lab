/*
 * portable_riscv.c - RISC-V leg of the cross-target comparison harness.
 *
 * Platform glue only: UART output + sifive_test exit. The actual computation
 * is the shared, portable portable_run() in ../portable/, identical to the
 * code compiled for Cortex-M7. Build with `make portable`, run with
 * `make portable-run`.
 */
#include <stdint.h>
#include "probe.h"          /* -I../portable */

#define QEMU_VIRT_UART0       ((volatile uint8_t  *)0x10000000)
#define QEMU_VIRT_SIFIVE_TEST ((volatile uint32_t *)0x00100000)

static void rv_putc(char c) { *QEMU_VIRT_UART0 = (uint8_t)c; }

int main(void)
{
    portable_run(rv_putc);
    *QEMU_VIRT_SIFIVE_TEST = 0x5555u;          /* PASS -> QEMU exit 0 */
    for (;;) asm volatile("wfi");
}
