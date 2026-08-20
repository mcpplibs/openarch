/* openarch.trap — the riscv64 entry stub.
 *
 * ⚠️ MACHINE MODE, AND THE CHOICE IS THE INTERFACE'S FIRST ASYMMETRY WITH
 * aarch64.
 *
 * riscv has three privilege levels and a program must pick one to trap into.
 * A bare-metal image booted with `-bios none` starts in M-mode and traps to
 * `mtvec`; a kernel running under SBI lives in S-mode and traps to `stvec`.
 * The two are the same mechanism with a different prefix on every register, and
 * NEITHER is more correct — which level a program occupies is a property of how
 * it was booted.
 *
 * aarch64 has no corresponding choice here: a kernel is at EL1, and `VBAR_EL1`
 * is where its traps go. There is one obvious answer, so the aarch64 backend
 * does not have to make one.
 *
 * This backend takes M-mode, because that is where an image this layer's probe
 * can boot actually runs. An S-mode variant is a mechanical substitution
 * (`m` -> `s` throughout) but it is a DIFFERENT backend, not a runtime switch:
 * the CSR names are encoded in the instructions.
 *
 * ⚠️ WHAT THIS STUB SAVES, AND WHY IT IS NOT EVERYTHING.
 *
 * The sixteen caller-saved integer registers. A handler is an ordinary C++
 * function, so the callee-saved ones are its own responsibility and the
 * compiler already knows that. Saving all thirty-two would be a third of a
 * kilobyte of stack per trap to preserve registers nothing can have changed.
 *
 * Floating-point state is NOT saved, which is the same contract
 * `openarch.context` states: a saved context in this layer is integer state.
 */
    .section .text.arch_trap_entry,"ax",@progbits
    .globl  arch_trap_entry
    .type   arch_trap_entry, @function
    .balign 4
arch_trap_entry:
    addi    sp, sp, -160

    sd      ra,  0*8(sp)
    sd      t0,  1*8(sp)
    sd      t1,  2*8(sp)
    sd      t2,  3*8(sp)
    sd      t3,  4*8(sp)
    sd      t4,  5*8(sp)
    sd      t5,  6*8(sp)
    sd      t6,  7*8(sp)
    sd      a0,  8*8(sp)
    sd      a1,  9*8(sp)
    sd      a2, 10*8(sp)
    sd      a3, 11*8(sp)
    sd      a4, 12*8(sp)
    sd      a5, 13*8(sp)
    sd      a6, 14*8(sp)
    sd      a7, 15*8(sp)

    /* The frame the dispatcher fills sits above the saved registers. Handing
     * its address rather than letting the dispatcher allocate keeps the trap
     * path free of any call that could itself fault. */
    addi    a0, sp, 128
    call    arch_trap_dispatch

    ld      ra,  0*8(sp)
    ld      t0,  1*8(sp)
    ld      t1,  2*8(sp)
    ld      t2,  3*8(sp)
    ld      t3,  4*8(sp)
    ld      t4,  5*8(sp)
    ld      t5,  6*8(sp)
    ld      t6,  7*8(sp)
    ld      a0,  8*8(sp)
    ld      a1,  9*8(sp)
    ld      a2, 10*8(sp)
    ld      a3, 11*8(sp)
    ld      a4, 12*8(sp)
    ld      a5, 13*8(sp)
    ld      a6, 14*8(sp)
    ld      a7, 15*8(sp)

    addi    sp, sp, 160
    mret
    .size   arch_trap_entry, . - arch_trap_entry

    .section .note.GNU-stack,"",@progbits
