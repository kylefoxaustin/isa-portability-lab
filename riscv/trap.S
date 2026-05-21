/*
 * trap.S - Minimal M-mode trap handler: capture + skip + return.
 *
 * Captures mcause/mepc/mtval (plus a running trap count) into _trap_info,
 * advances mepc past the faulting instruction (handling 2- vs 4-byte
 * encodings), and returns via mret so the interrupted code continues.
 *
 * This is enough to (a) survive a deliberate synchronous fault and (b) let
 * a test read back what happened. A real dispatcher would decode mcause and
 * vector to per-cause handlers + service interrupts; add that when needed.
 *
 * Register handling: synchronous, non-nested only. We bootstrap one free
 * register through mscratch (crt0 points it at _trap_save), spill the
 * temporaries we touch, then restore them and mret.
 */

    .section .text
    .align 4                        /* mtvec requires >= 4-byte alignment */
    .global trap_vector
    .type trap_vector, @function

trap_vector:
    csrrw   t0, mscratch, t0        /* t0 = &_trap_save ; mscratch = orig t0 */
    sd      t1,  0(t0)
    sd      t2,  8(t0)
    sd      t3, 16(t0)
    csrr    t1, mscratch            /* t1 = orig t0 */
    sd      t1, 24(t0)
    csrw    mscratch, t0            /* keep mscratch = &_trap_save */

    /* Capture cause/epc/tval and bump the trap count */
    la      t1, _trap_info
    csrr    t2, mcause
    sd      t2,  0(t1)
    csrr    t2, mepc
    sd      t2,  8(t1)
    csrr    t2, mtval
    sd      t2, 16(t1)
    ld      t2, 24(t1)
    addi    t2, t2, 1
    sd      t2, 24(t1)

    /* Advance mepc past the faulting instruction.
     * RISC-V: (insn[1:0] == 0b11) => 4-byte, else 2-byte (compressed). */
    csrr    t1, mepc
    lh      t2, 0(t1)
    andi    t2, t2, 3
    li      t3, 3
    bne     t2, t3, 1f
    addi    t1, t1, 4
    j       2f
1:  addi    t1, t1, 2
2:  csrw    mepc, t1

    /* Restore temporaries and return to the (advanced) mepc */
    csrr    t0, mscratch            /* t0 = &_trap_save */
    ld      t1,  0(t0)
    ld      t2,  8(t0)
    ld      t3, 16(t0)
    ld      t0, 24(t0)              /* restore orig t0 last (clobbers base) */
    mret

    .size trap_vector, . - trap_vector

    /* ---- post-mortem / test-readable scratch (zeroed BSS) ---- */
    .section .bss
    .align 3
    .global _trap_info
_trap_info:                         /* [0]=mcause [1]=mepc [2]=mtval [3]=count */
    .skip 32

    .align 3
    .global _trap_save
_trap_save:                         /* t1@0, t2@8, t3@16, orig-t0@24 */
    .skip 32
