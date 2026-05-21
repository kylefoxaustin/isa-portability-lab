/*
 * link.ld - Bare-metal RISC-V memory layout
 *
 * Target: 256KB SRAM at 0x80000000 (QEMU 'virt' machine compatible).
 * When you target real silicon, edit MEMORY{} and any peripheral bases
 * referenced by main.c. Everything else should be portable.
 */

OUTPUT_ARCH("riscv")
OUTPUT_FORMAT("elf64-littleriscv")
ENTRY(_start)

MEMORY
{
    SRAM (rwx) : ORIGIN = 0x80000000, LENGTH = 256K
}

/* Stack size carved out of the top of SRAM */
PROVIDE(_stack_size = 8K);

SECTIONS
{
    .text :
    {
        /* _start must be at the very beginning - this is the reset vector */
        KEEP(*(.text.boot))
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(8);
    } > SRAM

    /* Drop-in probe registry (see portable/probe.h). KEEP so --gc-sections
     * can't discard it; bracketed by __start_/__stop_probes for iteration. */
    .probes :
    {
        . = ALIGN(8);
        PROVIDE(__start_probes = .);
        KEEP(*(probes))
        PROVIDE(__stop_probes = .);
        . = ALIGN(8);
    } > SRAM

    .data :
    {
        _data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(8);
        _data_end = .;
    } > SRAM

    .bss (NOLOAD) :
    {
        . = ALIGN(8);
        _bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    } > SRAM

    /* Stack lives at the top of SRAM, grows down */
    . = ALIGN(16);
    _stack_bottom = .;
    . = . + _stack_size;
    _stack_top = .;

    /* Heap is whatever's left between stack top and end of SRAM.
     * No malloc by default; uncomment in your runtime if needed. */
    _heap_start = .;
    _heap_end   = ORIGIN(SRAM) + LENGTH(SRAM);

    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
        *(.riscv.attributes)
    }
}
