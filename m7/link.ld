/*
 * link.ld - Cortex-M7 memory layout for the QEMU mps2-an500 machine.
 *
 * MPS2 boots from the code SRAM at 0x00000000 (the vector table must live
 * there: the CPU loads the initial SP from 0x0 and the reset PC from 0x4).
 * Data/BSS/stack live in the SRAM at 0x20000000.
 */
ENTRY(Reset_Handler)

MEMORY
{
    CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 2M
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 1M
}

_estack = ORIGIN(RAM) + LENGTH(RAM);   /* initial SP: top of RAM */

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > CODE

    .text :
    {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > CODE

    /* Drop-in probe registry (see ../portable/probe.h). KEEP so --gc-sections
     * can't discard it; bracketed by __start_/__stop_probes for iteration. */
    .probes :
    {
        . = ALIGN(4);
        PROVIDE(__start_probes = .);
        KEEP(*(probes))
        PROVIDE(__stop_probes = .);
        . = ALIGN(4);
    } > CODE

    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data .data.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > CODE

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /DISCARD/ : { *(.ARM.exidx*) *(.comment) *(.note.*) }
}
