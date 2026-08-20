/* openarch.context — the riscv64 backend.
 *
 * ⚠️ ASSEMBLY, AND NOT AS A PREFERENCE.
 *
 * A compiler-generated prologue saves registers relative to the CURRENT stack
 * pointer, and the matching epilogue restores them after this function has
 * replaced it. The frame it would read belongs to a different context. There is
 * no attribute and no flag that makes a C++ function safe here; the operation
 * is defined by what it does to the stack pointer, so it is written where the
 * stack pointer is under the author's control.
 *
 * WHAT IS SAVED, AND WHY THAT IS THE WHOLE LIST
 *
 * A switch occurs at an ordinary call site, so the caller has already spilled
 * everything the RISC-V calling convention makes caller-saved (a0-a7, t0-t6).
 * What remains is ra, sp and s0-s11: thirteen registers, 104 bytes.
 *
 * ⚠️ The floating-point callee-saved registers (fs0-fs11) are NOT saved. Every
 * translation unit in an mcpp freestanding build is compiled with the target's
 * ABI, and a kernel that switches contexts while floating-point state is live
 * in a task must save them itself. Saving them unconditionally would double the
 * cost of every switch for the majority of kernels that never use them, and
 * doing it correctly requires knowing whether the FPU is enabled at all —
 * which is a policy question this layer does not answer.
 */

    .section .text.arch_context_switch,"ax",@progbits
    .globl  arch_context_switch
    .type   arch_context_switch, @function
    .balign 4

/* void arch_context_switch(context& from, context& to)
 *   a0 = &from, a1 = &to
 */
arch_context_switch:
    sd      ra,   0*8(a0)
    sd      sp,   1*8(a0)
    sd      s0,   2*8(a0)
    sd      s1,   3*8(a0)
    sd      s2,   4*8(a0)
    sd      s3,   5*8(a0)
    sd      s4,   6*8(a0)
    sd      s5,   7*8(a0)
    sd      s6,   8*8(a0)
    sd      s7,   9*8(a0)
    sd      s8,  10*8(a0)
    sd      s9,  11*8(a0)
    sd      s10, 12*8(a0)
    sd      s11, 13*8(a0)

    ld      ra,   0*8(a1)
    ld      sp,   1*8(a1)
    ld      s0,   2*8(a1)
    ld      s1,   3*8(a1)
    ld      s2,   4*8(a1)
    ld      s3,   5*8(a1)
    ld      s4,   6*8(a1)
    ld      s5,   7*8(a1)
    ld      s6,   8*8(a1)
    ld      s7,   9*8(a1)
    ld      s8,  10*8(a1)
    ld      s9,  11*8(a1)
    ld      s10, 12*8(a1)
    ld      s11, 13*8(a1)

    /* Returns through the resumed context's `ra`, which is where that context
     * called this function from — or, for a context that has never run, the
     * trampoline below. */
    ret
    .size   arch_context_switch, . - arch_context_switch

/* The initial return address of a context that has not run yet.
 *
 * `arch_context_init` leaves the entry point in s1 and its argument in s2,
 * because those survive the switch above while a0 does not: a0 holds `&from`
 * on the way in and is never restored.
 */
    .section .text.arch_context_entry,"ax",@progbits
    .type   arch_context_entry, @function
    .balign 4
    .globl  arch_context_entry
arch_context_entry:
    mv      a0, s2          /* arg */
    jr      s1              /* entry(arg) — a tail call, so there is no frame
                             * to return to, which matches the contract that
                             * an entry must not return. */
    .size   arch_context_entry, . - arch_context_entry
