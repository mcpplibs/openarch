/* openarch.context — the armv7a backend's switch and trampoline.
 *
 * AAPCS names r4-r11 as callee-saved, plus SP (r13) and LR (r14). Ten words,
 * forty bytes — the smallest saved context of any backend here, and the reason
 * is the register file rather than any choice this file makes.
 *
 * d8-d15 ARE ALSO CALLEE-SAVED AND ARE DELIBERATELY NOT SAVED, which is the
 * interface's contract and not this backend's shortcut: riscv64 saves none of
 * fs0-fs11 and aarch64 none of d8-d15 for the same reason. A saved context in
 * openarch is INTEGER state; a kernel that lets its tasks use floating point
 * saves that state itself or compiles with floating point disabled.
 *
 * r9 IS SAVED AND RESTORED LIKE ANY OTHER. Some ARM ABIs reserve it as a
 * static base or a thread register, and a kernel built with those conventions
 * would want it left alone — but this backend cannot know which convention its
 * consumer used, and saving a register that did not need saving is harmless
 * where failing to save one is not.
 */
    .syntax unified
    .arm

    .section .text.arch_context_switch,"ax",%progbits
    .globl  arch_context_switch
    .type   arch_context_switch, %function
    .balign 4
arch_context_switch:                    /* r0 = &from, r1 = &to */
    stm     r0, {r4-r11}                /* [0 .. 31]  callee-saved            */
    str     sp, [r0, #32]
    str     lr, [r0, #36]

    ldm     r1, {r4-r11}
    ldr     sp, [r1, #32]
    ldr     lr, [r1, #36]
    bx      lr
    .size   arch_context_switch, . - arch_context_switch

/* The initial return address of a context built by arch_context_init.
 *
 * Reached by `bx lr`, not by a call, so it has no caller and must not return.
 * r4 and r5 carry the entry point and its argument: they are callee-saved,
 * therefore restored by the switch above, which is exactly the property that
 * lets an argument survive a transfer restoring no argument register.
 */
    .section .text.arch_context_entry,"ax",%progbits
    .globl  arch_context_entry
    .type   arch_context_entry, %function
    .balign 4
arch_context_entry:
    mov     r0, r5                      /* arg   */
    blx     r4                          /* entry */
    /* `entry` returning is a contract violation with nowhere to go. Spinning
     * is the honest response: it neither corrupts state nor pretends to
     * recover. */
1:  b       1b
    .size   arch_context_entry, . - arch_context_entry

    .section .note.GNU-stack,"",%progbits
