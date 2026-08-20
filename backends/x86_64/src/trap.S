/* The trap entry, on x86_64.
 *
 * ⭐ TWO HUNDRED AND FIFTY-SIX ENTRY POINTS, AND THE MACHINE FORCES ALL OF
 * THEM.
 *
 * riscv has one entry: `stvec` holds an address and `scause` says why. aarch64
 * has sixteen: a 2 KiB table of 128-byte slots selected by exception class. This
 * machine has 256 — the IDT — and the vector number is the ONLY place the
 * reason is recorded. There is no cause register to read, so a single shared
 * stub cannot know what it was called for; each vector must arrive at a
 * different instruction and say so.
 *
 * The stubs below are therefore generated rather than written, and a table of
 * their addresses is exported so the IDT builder does not have to depend on
 * them being a fixed size.
 *
 * ⚠️ TEN VECTORS PUSH AN ERROR CODE AND THE OTHER 246 DO NOT, AND THE FRAME
 * MUST BE THE SAME SHAPE EITHER WAY. A stub for a vector without one pushes a
 * zero in its place. Without that, `arch_trap_common` would find the interrupt
 * frame at two different offsets and would read `RIP` out of `CS` for half the
 * exception table — with no diagnostic, because both are plausible values.
 *
 * The vectors that push one: 8, 10, 11, 12, 13, 14, 17, 21, 29, 30.
 */

    .section .text.arch_trap_stubs,"ax",@progbits
    .balign 16

/* Every stub reaches here with the stack holding, from `rsp` upward:
 *
 *     0   vector          pushed by the stub
 *     8   error code      pushed by the processor or by the stub
 *    16   RIP             ─┐
 *    24   CS               │
 *    32   RFLAGS           ├ pushed by the processor
 *    40   RSP              │
 *    48   SS              ─┘
 *
 * ⚠️ `RSP` AND `SS` ARE ALWAYS PUSHED IN 64-BIT MODE, EVEN WITHOUT A PRIVILEGE
 * CHANGE. In 32-bit protected mode they are pushed only on a change of ring,
 * and a stub ported from 32-bit sources leaves the stack unbalanced on `iret`.
 */
    .globl  arch_trap_common
    .type   arch_trap_common, @function
arch_trap_common:
    /* The caller-saved registers, in reverse argument order so that the block
     * reads the same way it is restored. The callee-saved ones are not saved:
     * `arch_trap_dispatch` is an ordinary C function and preserves them. */
    pushq   %r11
    pushq   %r10
    pushq   %r9
    pushq   %r8
    pushq   %rdi
    pushq   %rsi
    pushq   %rdx
    pushq   %rcx
    pushq   %rax

    /* ⚠️ THE ALIGNMENT IS EXACT AND IS WORTH CHECKING RATHER THAN TRUSTING.
     *
     * The processor aligns `rsp` to 16 before pushing the interrupt frame. A
     * vector without an error code therefore arrives at `≡ 8 (mod 16)` after
     * five pushed quadwords, and its stub's two pushes bring it back to `≡ 8`;
     * a vector with one arrives at `≡ 0` after six, and its stub's single push
     * makes `≡ 8`. Either way this point is `≡ 8`, the nine pushes above make
     * it `≡ 0`, and the 32 reserved below keep it there — which is what the
     * System V ABI requires at a `call`. */
    subq    $32, %rsp               /* the arch_trap_frame */
    movq    %rsp, %rdi              /* arg 1: the frame to fill */
    leaq    32 + 9*8(%rsp), %rsi    /* arg 2: the raw stack, at `vector` */
    call    arch_trap_dispatch
    addq    $32, %rsp

    popq    %rax
    popq    %rcx
    popq    %rdx
    popq    %rsi
    popq    %rdi
    popq    %r8
    popq    %r9
    popq    %r10
    popq    %r11

    addq    $16, %rsp               /* the vector and the error code */
    iretq
    .size   arch_trap_common, . - arch_trap_common

/* The stubs. `.altmacro` is needed so that a macro argument can be used to
 * build a label name. */
.altmacro

.macro STUB_NOERR num
    .balign 16
arch_trap_stub_\num:
    pushq   $0
    pushq   $\num
    jmp     arch_trap_common
.endm

.macro STUB_ERR num
    .balign 16
arch_trap_stub_\num:
    pushq   $\num
    jmp     arch_trap_common
.endm

/* ⚠️ EMITTED ONE AT A TIME RATHER THAN BY `.rept`, FOR THE FIRST THIRTY-TWO.
 * The set that pushes an error code is not a range and not a pattern, so a loop
 * would need a conditional on the counter — and an off-by-one in that condition
 * produces a stub with the wrong stack shape for exactly one exception, which
 * is the hardest possible thing to notice. */
STUB_NOERR 0
STUB_NOERR 1
STUB_NOERR 2
STUB_NOERR 3
STUB_NOERR 4
STUB_NOERR 5
STUB_NOERR 6
STUB_NOERR 7
STUB_ERR   8
STUB_NOERR 9
STUB_ERR   10
STUB_ERR   11
STUB_ERR   12
STUB_ERR   13
STUB_ERR   14
STUB_NOERR 15
STUB_NOERR 16
STUB_ERR   17
STUB_NOERR 18
STUB_NOERR 19
STUB_NOERR 20
STUB_ERR   21
STUB_NOERR 22
STUB_NOERR 23
STUB_NOERR 24
STUB_NOERR 25
STUB_NOERR 26
STUB_NOERR 27
STUB_NOERR 28
STUB_ERR   29
STUB_ERR   30
STUB_NOERR 31

/* 32 upward are external interrupts, none of which carries an error code. */
    .set    i, 32
    .rept   224
    STUB_NOERR %i
    .set    i, i + 1
    .endr

/* The addresses, so that the IDT builder reads a table rather than assuming a
 * stride. `.balign 16` inside the macros makes the stride uniform today; a
 * table means it does not have to stay that way. */
    .section .rodata.arch_trap_stubs,"a",@progbits
    .balign 16
    .globl  arch_trap_stub_table
    .type   arch_trap_stub_table, @object
/* ⚠️ THE POINTER IS EMITTED FROM A MACRO, WHICH IS NOT A STYLE CHOICE.
 * `.altmacro`'s `%expr` substitution applies to a macro INVOCATION's arguments
 * and nowhere else, so `.quad arch_trap_stub_%i` inside a `.rept` passes the
 * two characters `%i` through to the label name and the assembler reports
 * `expected relocatable expression` 256 times. */
.macro STUB_PTR num
    .quad   arch_trap_stub_\num
.endm

arch_trap_stub_table:
    .set    i, 0
    .rept   256
    STUB_PTR %i
    .set    i, i + 1
    .endr
    .size   arch_trap_stub_table, . - arch_trap_stub_table

    .section .note.GNU-stack,"",@progbits
