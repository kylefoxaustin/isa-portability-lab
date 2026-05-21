/*
 * crt0.S - Minimal bare-metal startup for RV64 in M-mode.
 *
 * QEMU 'virt' starts all harts at 0x80000000 in M-mode after its built-in
 * bootrom. We park all harts except hart 0, set up stack/BSS, install a
 * trap vector, then call main().
 */

    .section .text.boot, "ax"
    .global _start
    .type _start, @function

_start:
    /* Park all secondary harts */
    csrr    t0, mhartid
    bnez    t0, _park

    /* Disable all interrupts at machine level until we're ready */
    csrw    mie, zero
    csrw    mip, zero

    /* Set stack pointer to top of SRAM (defined by linker) */
    la      sp, _stack_top

    /* Enable the F/D and V extensions in mstatus.FS=11, mstatus.VS=11
     * Without this, FP and vector instructions trap with illegal-instr.
     *   mstatus.FS bits [14:13], mstatus.VS bits [10:9]  (RV64)
     *   Set both to 0b11 = "Dirty" so they're known-live.
     */
    li      t0, (3 << 13) | (3 << 9)   /* FS=11, VS=11 */
    csrs    mstatus, t0

    /* Zero out BSS: [_bss_start, _bss_end) */
    la      t0, _bss_start
    la      t1, _bss_end
1:  bgeu    t0, t1, 2f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       1b
2:

    /* Point mscratch at the trap handler's register save area. Must happen
     * before any trap can fire, since trap_vector bootstraps through it. */
    la      t0, _trap_save
    csrw    mscratch, t0

    /* Install trap vector (direct mode, all traps go to one handler) */
    la      t0, trap_vector
    csrw    mtvec, t0

    /* Off to C */
    call    main

    /* If main returns, halt forever */
_halt:
    wfi
    j       _halt

_park:
    wfi
    j       _park

    .size _start, . - _start
