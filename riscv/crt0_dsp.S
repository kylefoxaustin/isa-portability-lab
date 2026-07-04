/*
 * crt0_dsp.S - startup for the DSP equivalence harness (run_kernels.c).
 *
 * Identical boot sequence to crt0.S (park secondary harts, stack, enable
 * FS/VS so FP+vector don't trap, zero BSS, install trap vector), but the
 * TAIL differs: run_kernels.c's main() returns an exit code (0 = all kernels
 * bit-exact vs golden), and we forward that to QEMU's sifive_test device so
 * `qemu ... ; echo $?` is the CI gate. crt0.S (used by the hello/self-test/
 * portable images) halts on return instead; keeping this separate leaves the
 * existing green legs untouched.
 */
    .section .text.boot, "ax"
    .global _start
    .type _start, @function

_start:
    csrr    t0, mhartid
    bnez    t0, _park

    csrw    mie, zero
    csrw    mip, zero

    la      sp, _stack_top

    /* FS=11, VS=11 so FP + vector instructions are live (not illegal-instr) */
    li      t0, (3 << 13) | (3 << 9)
    csrs    mstatus, t0

    /* Zero BSS [_bss_start, _bss_end) */
    la      t0, _bss_start
    la      t1, _bss_end
1:  bgeu    t0, t1, 2f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       1b
2:

    la      t0, _trap_save
    csrw    mscratch, t0
    la      t0, trap_vector
    csrw    mtvec, t0

    call    main

    /* main() returned its exit code in a0. Drive sifive_test:
     *   0      -> 0x5555            (FINISHER_PASS  -> qemu exit 0)
     *   rc!=0  -> (rc<<16)|0x3333   (FINISHER_FAIL  -> qemu exit rc)      */
    li      t1, 0x100000            /* sifive_test base */
    bnez    a0, 3f
    li      t0, 0x5555
    sw      t0, 0(t1)
    j       _halt
3:
    slli    t0, a0, 16
    li      t2, 0x3333
    or      t0, t0, t2
    sw      t0, 0(t1)

_halt:
    wfi
    j       _halt

_park:
    wfi
    j       _park

    .size _start, . - _start
