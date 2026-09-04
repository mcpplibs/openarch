/* openarch.context — the Cortex-M backend's cooperative switch.
 *
 * ⭐⭐ IT IS THE SAME MECHANISM AS `arch_trap_switch`, AND THAT IS THE WHOLE
 * DESIGN RATHER THAN AN IMPLEMENTATION CONVENIENCE.
 *
 * The interface says the two primitives have one shape and differ only in when
 * they take effect: one switches now, the other switches when the trap
 * returns. On M-profile the hardware provides exactly that distinction —
 * PendSV pended from thread mode is taken at once, and PendSV pended from a
 * handler is taken when that handler exits. So both functions do the same
 * thing, and the machine supplies the difference.
 *
 * ⚠️ THIS REPLACED A PURE REGISTER SWAP, AND THE REASON IS THAT THE TWO COULD
 * NOT SHARE A CONTEXT LAYOUT. A cooperative swap stores r4-r11, SP and LR in
 * the caller's block; a preempted context lives as r4-r11 plus a hardware
 * exception frame on its own stack. Two layouts means a context saved one way
 * cannot be resumed the other, and a kernel that mixed a `yield` with a timer
 * would corrupt whichever it used second — silently, because both are just
 * words. One mechanism, one layout, and `arch_context_init` produces the shape
 * both read.
 *
 * ⚠️ THE PRICE, STATED: a cooperative switch here needs PendSV enabled and the
 * caller running on PSP. Both hold for a task started through
 * `openarch_cm_enter`; neither holds for code that has never entered a task.
 * The other three backends have no such requirement, and this one has it
 * because its exception mechanism is the only way to change the stack an
 * exception return will use.
 */
    .syntax unified
    .thumb

    .section .text.arch_context_switch,"ax",%progbits
    .globl  arch_context_switch
    .type   arch_context_switch, %function
    .thumb_func
    .balign 4
arch_context_switch:                    /* r0 = &from, r1 = &to */
    ldr     r2, =openarch_cm_pending
    str     r0, [r2, #0]
    str     r1, [r2, #4]
    /* ICSR.PENDSVSET — 0xE000ED04, bit 28. */
    ldr     r2, =0xE000ED04
    movs    r3, #1
    lsls    r3, r3, #28
    str     r3, [r2, #0]
    dsb
    isb                                 /* PendSV is taken here */
    bx      lr
    .size   arch_context_switch, . - arch_context_switch

/* ⭐ THERE IS NO TRAMPOLINE HERE, AND ITS ABSENCE IS THE LAYOUT'S DOING.
 *
 * The other three backends need one: their switch restores callee-saved
 * registers and returns, so a context that has never run has to land somewhere
 * that moves the entry point and argument into the right registers. Here a
 * context is resumed by an EXCEPTION RETURN, and the hardware frame carries r0
 * and the PC — so `arch_context_init` simply writes the argument into the r0
 * slot and the entry point into the PC slot, and the hardware does the rest.
 *
 * ⚠️ A task that returns therefore returns to the frame's LR, which
 * `arch_context_init` sets to an address that spins. The contract is the same
 * one every backend states — an entry must not return — and this is what
 * happens if it does, rather than a branch to whatever the stack held.
 */
    .section .text.openarch_cm_task_exit,"ax",%progbits
    .globl  openarch_cm_task_exit
    .type   openarch_cm_task_exit, %function
    .thumb_func
    .balign 4
openarch_cm_task_exit:
1:  b       1b
    .size   openarch_cm_task_exit, . - openarch_cm_task_exit
