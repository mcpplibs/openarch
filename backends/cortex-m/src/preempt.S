/* openarch.trap — the M-profile switch, which is an EXCEPTION rather than a
 * function call.
 *
 * ⭐⭐ THIS IS THE ONE PLACE IN THE LAYER WHERE TWO MACHINES DO NOT SHARE A
 * MECHANISM, AND IT IS WHY `arch_trap_switch` IS AN INTERFACE FUNCTION RATHER
 * THAN A LINE OF KERNEL CODE.
 *
 * riscv64, aarch64 and x86_64 take a trap on the interrupted context's own
 * stack, so a handler can perform an ordinary cooperative switch on its way out
 * and the exception return then finds the new context's frame. M-profile
 * cannot: the handler runs on MSP while the task runs on PSP, the hardware has
 * already stacked r0-r3, r12, LR, PC and xPSR onto the TASK's stack, and
 * swapping the handler's registers changes the handler's stack — the exception
 * return then unstacks a frame belonging to nobody.
 *
 * Measured, before this backend had the primitive: calling
 * `arch_context_switch` from PendSV builds, boots, and reports that neither of
 * two tasks ever observed the other. Nothing failed. The tasks were simply
 * never interleaved, and only a counter assertion could tell the difference.
 *
 * ⭐ THE MECHANISM THE MACHINE DOES OFFER IS EXACTLY THE INTERFACE'S SHAPE.
 * PendSV is an exception whose whole purpose is "do this when we are about to
 * leave handler mode". So:
 *
 *     arch_trap_switch  — pend it from a handler: taken at exception exit
 *     arch_context_switch — pend it from thread mode: taken immediately
 *
 * One instruction sequence, two moments. That is the interface's own sentence
 * about the two primitives, provided by the hardware rather than emulated.
 *
 * ⚠️ THE BOARD MUST NAME `openarch_cm_pendsv` IN VECTOR SLOT 14. There is no
 * way for this package to install it: the table's location is a board fact and
 * the hardware reads it by address. A board that does not is not broken at
 * link time — the switch simply never happens, which is why the example that
 * exercises this asserts preemption rather than progress.
 */
    .syntax unified
    .thumb

/* ⚠️ EVERY IMMEDIATE BELOW USES THE TWO-OPERAND FORM (`adds r0, #32`), WHICH IS
 * NOT A STYLE CHOICE. On v6-M the three-operand `adds Rd, Rn, #imm` encodes
 * only imm3 — 0 to 7 — while `adds Rdn, #imm8` reaches 255. Writing the
 * three-operand form assembles for a Cortex-M4 and fails for a Cortex-M0, which
 * is the same hazard the `stmia`/high-register dance below exists for.
 *
 * The saved layout, at the address a context block holds:
 *
 *   +0   r4  r5  r6  r7          the low callee-saved four
 *   +16  r8  r9 r10 r11          the high four, moved through the low ones
 *   +32  r0  r1  r2  r3 r12 LR PC xPSR    the hardware's own frame
 */

    .section .text.openarch_cm_pendsv,"ax",%progbits
    .globl  openarch_cm_pendsv
    .type   openarch_cm_pendsv, %function
    .thumb_func
    .balign 4
openarch_cm_pendsv:
    /* ⚠️⚠️ MASKED FOR THE WHOLE SWITCH, AND THIS IS THE FIX FOR A DEFECT THAT
     * PRESENTED AS A FLAKE.
     *
     * PendSV is the LOWEST priority, which is what makes it run after every
     * other handler — and it is also what lets the tick that REQUESTED the
     * switch preempt the switch itself. The tick handler then calls
     * `arch_trap_switch` again, overwriting the pending pair, and this stub
     * resumes holding an outgoing pointer for one context and a `from` naming
     * another. It writes the first into the second, and both contexts are lost.
     *
     * Measured on `mps2-an385`: six of twelve runs of an identical image
     * reported `no task observed the other` — the same output a backend with no
     * `arch_trap_switch` at all produces. The counters said what the message
     * could not: `pendsv=2997` switches performed, and both tasks stopped at a
     * handful of iterations, spinning in the task-exit trap because their saved
     * stack pointers no longer described a frame.
     *
     * ⭐ SAFE TO UNMASK UNCONDITIONALLY AT THE END. PRIMASK masks every
     * configurable-priority exception, so a context that had interrupts masked
     * could not have been interrupted into PendSV in the first place: on entry
     * here PRIMASK is necessarily clear.
     *
     * ⚠️ It is NOT enough to make the pair atomic. The outgoing pointer is read
     * from PSP at entry and the pair from memory after the call; making only
     * the second indivisible leaves them describing different moments. What has
     * to be indivisible is the switch. */
    cpsid   i
    /* r4-r11 still hold the OUTGOING context's values: the hardware stacked
     * only the caller-saved half, and any handler between then and now restored
     * the rest in its own epilogue. */
    mrs     r0, psp
    subs    r0, #32
    stmia   r0!, {r4-r7}
    mov     r4, r8
    mov     r5, r9
    mov     r6, r10
    mov     r7, r11
    stmia   r0!, {r4-r7}
    subs    r0, #32                     /* r0 = the outgoing saved SP */

    push    {lr}                        /* the EXC_RETURN we were entered with */
    bl      openarch_cm_pendsv_pick     /* r0 in = outgoing, r0 out = incoming */
    pop     {r1}

    adds    r0, #16
    ldmia   r0!, {r4-r7}                /* the high four, via the low ones */
    mov     r8, r4
    mov     r9, r5
    mov     r10, r6
    mov     r11, r7
    subs    r0, #32
    ldmia   r0!, {r4-r7}                /* the low four */
    adds    r0, #16                     /* past both blocks: the hardware frame */
    msr     psp, r0
    cpsie   i
    bx      r1
    .size   openarch_cm_pendsv, . - openarch_cm_pendsv

/* Entering the FIRST task. There is no outgoing context, so this cannot go
 * through the switch above.
 *
 * ⚠️ IT IS ONLY VALID ON A CONTEXT THAT HAS NEVER RUN, and that is what lets it
 * be this short: a fresh frame from `arch_context_init` holds zero in r1-r3 and
 * r12, so only the argument and the entry point have to be delivered. A running
 * context carries live values in all of them and must be resumed by an
 * exception return, which is what PendSV above does.
 *
 * ⚠️ AND IT SWITCHES THE THREAD STACK POINTER MID-FUNCTION, so nothing after
 * the `msr control` may touch this function's own stack. Everything below it is
 * register-to-register for that reason.
 *
 * ⚠️⚠️ IT UNMASKS INTERRUPTS ITSELF, AND THAT IS A FIX RATHER THAN A SERVICE.
 *
 * CALL IT WITH INTERRUPTS MASKED. Between arming a timer and arriving here
 * there is no valid context to preempt: PSP still holds whatever reset left,
 * and a tick in that window pends PendSV, which saves eight registers below
 * that address and writes the result into the outgoing context — destroying the
 * one this function is about to enter.
 *
 * Measured. With the example unmasking before calling this, the program did not
 * fault: it ran one task forever and reported `no task observed the other`,
 * about one run in three. A pass rate is not a result, and the fix is to close
 * the window rather than to widen the timer — so the unmask is the last
 * instruction before the branch, where no window remains.
 *
 * ⭐ An interrupt taken between the `cpsie` and the `bx` is harmless: PSP is
 * valid by then, so the hardware stacks this function's own r0-r3 and PC onto
 * the task's stack and PendSV resumes it correctly.
 *
 * It does not return. */
    .section .text.openarch_cm_enter,"ax",%progbits
    .globl  openarch_cm_enter
    .type   openarch_cm_enter, %function
    .thumb_func
    .balign 4
openarch_cm_enter:                      /* r0 = the context block */
    ldr     r0, [r0, #0]                /* the saved SP it holds */
    adds    r0, #16
    ldmia   r0!, {r4-r7}
    mov     r8, r4
    mov     r9, r5
    mov     r10, r6
    mov     r11, r7
    subs    r0, #32
    ldmia   r0!, {r4-r7}
    adds    r0, #16                     /* r0 = the hardware frame */
    msr     psp, r0
    movs    r1, #2
    msr     control, r1                 /* Thread mode uses PSP from here */
    isb
    ldr     r1, [r0, #24]               /* the frame's PC */
    ldr     r2, [r0, #0]                /* the frame's r0 — the task argument */
    adds    r0, #32                     /* drop the frame we just consumed */
    msr     psp, r0
    mov     r0, r2
    cpsie   i                           /* see the note above: HERE, not sooner */
    bx      r1
    .size   openarch_cm_enter, . - openarch_cm_enter
