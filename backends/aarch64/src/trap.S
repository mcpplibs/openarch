/* openarch.trap — the aarch64 vector table and entry stub.
 *
 * ⭐ SIXTEEN ENTRY POINTS, AND THAT IS THE STRUCTURAL DIFFERENCE THE INTERFACE
 * HAS TO HIDE.
 *
 * riscv writes one address into `mtvec` and every trap arrives there with a
 * cause register to read. aarch64 requires a 2 KiB table of sixteen 128-byte
 * slots, and the hardware picks a slot from the exception class crossed with
 * where it came from. Part of "what happened" is therefore carried by WHICH
 * SLOT RAN and cannot be read back from any register afterwards.
 *
 * So each slot has to record its own index before joining the common path.
 *
 * ⚠️ AND IT HAS TO DO SO WITHOUT DESTROYING THE TRAPPED CONTEXT. The obvious
 * spelling — `mov x9, #N; b common` — clobbers x9, whose value belongs to the
 * interrupted code. A trap that silently corrupts one register is the worst
 * kind of defect this layer can ship: everything works until something reads
 * that register after an interrupt. Each slot therefore opens the frame and
 * saves x0 FIRST, then uses x0 for the index.
 *
 * ⚠️ THE TABLE IS THIS LAYER'S, NOT THE CALLER'S, for the same reason
 * `MAIR_EL1` is in openarch.pte: a caller made to supply sixteen slots would be
 * writing aarch64 into every kernel that uses this interface, including the
 * ones that will never run on it.
 *
 * Caller-saved state only — x0-x18 and x30. A handler is an ordinary C++
 * function and the compiler already preserves the rest. Floating-point state is
 * not saved, which is the contract `openarch.context` states.
 */

    .section .text.arch_vectors,"ax",@progbits

/* One slot: open the frame, save x0, put the index in x0, join the common
 * path. Four instructions of the 32 a slot has room for. */
.macro SLOT idx
    sub     sp, sp, #192
    str     x0, [sp, #0]
    mov     x0, #\idx
    b       arch_trap_common
    .balign 0x80
.endm

    .globl  arch_vector_table
    .type   arch_vector_table, @object
    .balign 0x800                     /* VBAR_EL1 requires 2 KiB alignment */
arch_vector_table:
    SLOT 0      /* Current EL with SP0    : Synchronous */
    SLOT 1      /* Current EL with SP0    : IRQ         */
    SLOT 2      /* Current EL with SP0    : FIQ         */
    SLOT 3      /* Current EL with SP0    : SError      */
    SLOT 4      /* Current EL with SPx    : Synchronous */
    SLOT 5      /* Current EL with SPx    : IRQ         */
    SLOT 6      /* Current EL with SPx    : FIQ         */
    SLOT 7      /* Current EL with SPx    : SError      */
    SLOT 8      /* Lower EL, AArch64      : Synchronous */
    SLOT 9      /* Lower EL, AArch64      : IRQ         */
    SLOT 10     /* Lower EL, AArch64      : FIQ         */
    SLOT 11     /* Lower EL, AArch64      : SError      */
    SLOT 12     /* Lower EL, AArch32      : Synchronous */
    SLOT 13     /* Lower EL, AArch32      : IRQ         */
    SLOT 14     /* Lower EL, AArch32      : FIQ         */
    SLOT 15     /* Lower EL, AArch32      : SError      */
    .size   arch_vector_table, . - arch_vector_table

    .section .text.arch_trap_common,"ax",@progbits
    .type   arch_trap_common, @function
    .balign 4
arch_trap_common:
    /* x0 holds the slot index and the original x0 is already at [sp, #0]. */
    stp     x1,  x2,  [sp, #8]
    stp     x3,  x4,  [sp, #24]
    stp     x5,  x6,  [sp, #40]
    stp     x7,  x8,  [sp, #56]
    stp     x9,  x10, [sp, #72]
    stp     x11, x12, [sp, #88]
    stp     x13, x14, [sp, #104]
    stp     x15, x16, [sp, #120]
    stp     x17, x18, [sp, #136]
    str     x30,      [sp, #152]

    mov     x1, x0                    /* second argument: the slot index      */
    add     x0, sp, #160              /* first argument:  the frame           */
    bl      arch_trap_dispatch

    ldp     x1,  x2,  [sp, #8]
    ldp     x3,  x4,  [sp, #24]
    ldp     x5,  x6,  [sp, #40]
    ldp     x7,  x8,  [sp, #56]
    ldp     x9,  x10, [sp, #72]
    ldp     x11, x12, [sp, #88]
    ldp     x13, x14, [sp, #104]
    ldp     x15, x16, [sp, #120]
    ldp     x17, x18, [sp, #136]
    ldr     x30,      [sp, #152]
    ldr     x0,       [sp, #0]

    add     sp, sp, #192
    eret
    .size   arch_trap_common, . - arch_trap_common

    .section .note.GNU-stack,"",@progbits
