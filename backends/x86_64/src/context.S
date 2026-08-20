/* arch_context_switch — System V AMD64.
 *
 * ⚠️ THE CALLEE-SAVED SET IS SIX REGISTERS AND A STACK POINTER, WHICH IS THE
 * SMALLEST OF THE THREE MACHINES BY A WIDE MARGIN.
 *
 * riscv64 saves fourteen doublewords, aarch64 thirteen, x86_64 seven. That is
 * not this backend being clever: the System V AMD64 ABI names only `rbx`,
 * `rbp` and `r12`-`r15` callee-saved, because the architecture has sixteen
 * general registers rather than thirty-two and the convention spends most of
 * them on arguments and scratch.
 *
 * The consequence for the interface is that `arch::context`'s 128 bytes are
 * generous here and tight on riscv — which is the right way round, and is
 * checked by a static assertion in context_init.cpp rather than assumed.
 *
 * Arguments: `from` in `rdi`, `to` in `rsi`.
 *
 * ⚠️ THE RETURN ADDRESS IS ON THE STACK, NOT IN A REGISTER, AND THAT CHANGES
 * WHAT A CONTEXT IS. On both other machines the saved `ra`/`x30` holds where to
 * resume; here `call` has already pushed it, so saving `rsp` saves the
 * resumption point with it. Switching therefore restores one fewer field and
 * ends in `ret` rather than a jump — and `arch_context_init` must PLANT a
 * return address on the new stack rather than store one in the context.
 */
    .section .text.arch_context_switch,"ax",@progbits
    .globl  arch_context_switch
    .type   arch_context_switch, @function
    .balign 16

arch_context_switch:
    movq    %rbx,  0*8(%rdi)
    movq    %rbp,  1*8(%rdi)
    movq    %r12,  2*8(%rdi)
    movq    %r13,  3*8(%rdi)
    movq    %r14,  4*8(%rdi)
    movq    %r15,  5*8(%rdi)
    movq    %rsp,  6*8(%rdi)

    movq    0*8(%rsi), %rbx
    movq    1*8(%rsi), %rbp
    movq    2*8(%rsi), %r12
    movq    3*8(%rsi), %r13
    movq    4*8(%rsi), %r14
    movq    5*8(%rsi), %r15
    movq    6*8(%rsi), %rsp

    /* Returns through the resumed context's stack, which is where that context
     * was when it called here — or, for a context that has never run, the
     * trampoline address `arch_context_init` planted. */
    ret
    .size   arch_context_switch, . - arch_context_switch

/* The trampoline a freshly initialised context returns into.
 *
 * `arch_context_init` leaves `entry` in the slot restored to `r12` and `arg` in
 * the slot restored to `r13`, so by the time this runs both are in registers
 * the switch has just loaded.
 *
 * ⚠️ THE STACK MUST BE 16-BYTE ALIGNED AT THE `call`, NOT AT THE ENTRY. The
 * System V ABI requires `rsp + 8` to be 16-aligned on entry to a function,
 * because the `call` has pushed eight bytes. `arch_context_init` arranges the
 * initial stack so that this holds here; a `jmp` rather than a `call` preserves
 * it, and also matches the other two backends' tail call. */
    .section .text.arch_context_entry,"ax",@progbits
    .globl  arch_context_entry
    .type   arch_context_entry, @function
    .balign 16
arch_context_entry:
    movq    %r13, %rdi      /* arg */
    jmpq    *%r12           /* entry(arg) — a tail call, so there is no frame
                             * and no return address for `entry` to use. The
                             * interface says `entry` must not return. */
    .size   arch_context_entry, . - arch_context_entry

/* ⚠️ Marks the object as not requiring an executable stack. Without it lld
 * emits `missing .note.GNU-stack section implies executable stack` for every
 * hand-written assembler file, which on a bare-metal link is noise that hides
 * real diagnostics. The other two backends carry the same note. */
    .section .note.GNU-stack,"",@progbits
