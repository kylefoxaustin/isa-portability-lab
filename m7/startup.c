/*
 * startup.c - Minimal Cortex-M7 startup.
 *
 * Vector table (initial SP + reset + a couple of fault catchers), .data copy,
 * .bss zero, FPU enable, then main(). No interrupts are used by the probe.
 */
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);

void Reset_Handler(void)
{
    /* Copy initialized data from its load address (in CODE) to RAM */
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;

    /* Zero BSS */
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;

    /* Enable the FPU: CPACR (0xE000ED88), grant full access to CP10 & CP11 */
    *(volatile uint32_t *)0xE000ED88u |= (0xFu << 20);
    __asm volatile ("dsb \n isb");

    main();
    for (;;) { }
}

void Default_Handler(void) { for (;;) { } }

__attribute__((section(".isr_vector"), used))
void (*const g_vectors[])(void) = {
    (void (*)(void))&_estack,   /* 0x00: initial stack pointer */
    Reset_Handler,              /* 0x04: reset                 */
    Default_Handler,            /* 0x08: NMI                   */
    Default_Handler,            /* 0x0C: HardFault             */
};
