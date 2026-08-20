/* openarch.context — the aarch64 backend's switch and trampoline.
 *
 * AAPCS64 names x19-x28 as callee-saved, plus x29 (the frame pointer), x30
 * (the link register) and SP. Thirteen doublewords, 104 bytes.
 *
 * ⚠️ d8-d15 ARE ALSO CALLEE-SAVED AND ARE DELIBERATELY NOT SAVED HERE.
 *
 * This is the interface's contract rather than this backend's shortcut: the
 * riscv64 backend likewise saves none of fs0-fs11. A saved context in openarch
 * is INTEGER state, and a kernel that lets its tasks use floating point must
 * save that state itself, or compile with floating point disabled — which is
 * what a bare-metal kernel ordinarily does.
 *
 * The contract was implicit while one architecture existed, because a
 * single backend cannot distinguish "the interface says so" from "this
 * machine happens to work that way". Writing the second one is what made it
 * a statement. Note the arithmetic: saving d8-d15 as well would need 168
 * bytes, which does not fit the 128 the interface reserves — so the omission
 * is not merely conventional, it is what the reserved size already assumes.
 */
    .section .text.arch_context_switch,"ax",@progbits
    .globl  arch_context_switch
    .type   arch_context_switch, @function
    .balign 4
arch_context_switch:                    /* x0 = &from, x1 = &to */
    stp     x19, x20, [x0, #0]
    stp     x21, x22, [x0, #16]
    stp     x23, x24, [x0, #32]
    stp     x25, x26, [x0, #48]
    stp     x27, x28, [x0, #64]
    stp     x29, x30, [x0, #80]
    mov     x2, sp
    str     x2,       [x0, #96]

    ldp     x19, x20, [x1, #0]
    ldp     x21, x22, [x1, #16]
    ldp     x23, x24, [x1, #32]
    ldp     x25, x26, [x1, #48]
    ldp     x27, x28, [x1, #64]
    ldp     x29, x30, [x1, #80]
    ldr     x2,       [x1, #96]
    mov     sp, x2
    ret
    .size   arch_context_switch, . - arch_context_switch

/* The initial return address of a context built by arch_context_init.
 *
 * ⚠️ It is reached by `ret`, not by a call, so it has no caller and must not
 * return. x19 and x20 carry the entry point and its argument: they are
 * callee-saved, therefore restored by the switch above, which is exactly the
 * property that lets an argument survive a transfer that restores no argument
 * registers.
 */
    .section .text.arch_context_entry,"ax",@progbits
    .globl  arch_context_entry
    .type   arch_context_entry, @function
    .balign 4
arch_context_entry:
    mov     x0, x20                     /* arg   */
    blr     x19                         /* entry */
    /* `entry` returning is a contract violation with nowhere to go. Spinning
     * is the honest response: it neither corrupts state nor pretends to
     * recover. */
1:  b       1b
    .size   arch_context_entry, . - arch_context_entry

    .section .note.GNU-stack,"",@progbits
