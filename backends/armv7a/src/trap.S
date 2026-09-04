/* openarch.trap — the armv7a vector table and entry stubs.
 *
 * EIGHT ENTRIES OF ONE INSTRUCTION EACH, AND THAT IS THE STRUCTURAL DIFFERENCE
 * THIS BACKEND HAS TO HIDE.
 *
 * riscv writes one address into `mtvec` and reads a cause register. aarch64
 * has sixteen 128-byte slots. ARMv7-A has eight slots of FOUR BYTES — room for
 * a single branch and nothing else — and, unlike either, each exception is
 * taken in its OWN PROCESSOR MODE with its own banked SP and LR. So "what
 * happened" is carried by which slot ran, exactly as on aarch64, and "where to
 * return to" is carried in a register that only that mode can see.
 *
 * EACH MODE WOULD OTHERWISE NEED ITS OWN STACK, AND THIS BACKEND REFUSES TO
 * REQUIRE THAT. A board would have to allocate and install four of them before
 * the first exception, which is four more things to get wrong in the code that
 * runs before anything can report a fault. `srsdb` writes the return address
 * and SPSR onto the SVC-mode stack instead, and `cps` then switches to SVC
 * mode — so the kernel's one stack serves every exception, and a board that
 * set up SVC's stack to run C at all has already done everything needed.
 *
 * THE RETURN OFFSET DIFFERS PER EXCEPTION AND IS APPLIED BEFORE `srsdb`.
 * ARMv7-A leaves LR pointing past the faulting instruction by an amount that
 * depends on how the exception was taken: 4 for IRQ, FIQ and prefetch abort,
 * 8 for data abort, 0 for SVC and undefined. Getting one wrong resumes into
 * the middle of an instruction, which is why they are written out here rather
 * than folded into a shared macro with a parameter nobody would check.
 */
    .syntax unified
    .arm

    .section .text.arch_vectors,"ax",%progbits
    .globl  arch_vector_table
    .type   arch_vector_table, %object
    .balign 32                        /* VBAR requires 32-byte alignment */
arch_vector_table:
    b       arch_v_reset              /* 0x00 reset            */
    b       arch_v_undef              /* 0x04 undefined        */
    b       arch_v_svc                /* 0x08 supervisor call  */
    b       arch_v_pabt               /* 0x0C prefetch abort   */
    b       arch_v_dabt               /* 0x10 data abort       */
    b       arch_v_reset              /* 0x14 (reserved)       */
    b       arch_v_irq                /* 0x18 IRQ              */
    b       arch_v_fiq                /* 0x1C FIQ              */
    .size   arch_vector_table, . - arch_vector_table

/* One stub: correct LR, push the return state onto the SVC stack, switch to
 * SVC mode, save the caller-saved set, and join the common path with the slot
 * index in r0.
 *
 * `#0x13` is SVC mode. `srsdb sp!, #0x13` stores LR and SPSR to the SVC stack
 * whichever mode we are currently in, which is the whole reason this works
 * from four different modes with one stack.
 */
.macro STUB name, idx, adjust
    .globl  \name
    .type   \name, %function
    .balign 4
\name:
.if \adjust
    sub     lr, lr, #\adjust
.endif
    srsdb   sp!, #0x13
    cps     #0x13
    push    {r0-r3, r12, lr}
    mov     r0, #\idx
    bl      arch_trap_common_c
    pop     {r0-r3, r12, lr}
    rfeia   sp!
    .size   \name, . - \name
.endm

    STUB arch_v_undef, 1, 0
    STUB arch_v_svc,   2, 0
    STUB arch_v_pabt,  3, 4
    STUB arch_v_dabt,  4, 8
    STUB arch_v_irq,   6, 4
    STUB arch_v_fiq,   7, 4

/* Reset and the reserved slot have no context to preserve and nowhere to
 * return to. Spinning is the honest response, and it is the state a debugger
 * can be attached to. */
    .globl  arch_v_reset
    .type   arch_v_reset, %function
    .balign 4
arch_v_reset:
1:  b       1b
    .size   arch_v_reset, . - arch_v_reset

    .section .note.GNU-stack,"",%progbits
