/* openarch.context — the Cortex-M backend's switch and trampoline.
 *
 * AAPCS names r4-r11 as callee-saved, plus SP and LR. Ten words, 40 bytes,
 * well inside the 128 the interface reserves.
 *
 * ⚠️ NO FLOATING-POINT REGISTERS, WHICH IS THE INTERFACE'S CONTRACT RATHER
 * THAN THIS BACKEND'S SHORTCUT. aarch64 saves none of d8-d15 and riscv64 none
 * of fs0-fs11 for the same reason: a saved context in openarch is INTEGER
 * state. On M-profile the hardware's lazy FP stacking already handles the
 * exception path, and a cooperative switch between tasks that use the FPU is
 * the kernel's business — which is what `s16-s31` would cost 64 more bytes to
 * express here.
 *
 * ⭐ THIS IS THE HALF OF THE INTERFACE M-PROFILE CAN IMPLEMENT. The other
 * primitive the layer's viability was decided on — the page-table entry — has
 * nothing to construct on a machine whose MPU describes regions rather than
 * pages. See this backend's manifest for why that is declared rather than
 * faked.
 */
    .syntax unified
    .thumb

    .section .text.arch_context_switch,"ax",%progbits
    .globl  arch_context_switch
    .type   arch_context_switch, %function
    .thumb_func
    .balign 4
arch_context_switch:                    /* r0 = &from, r1 = &to */
    /* ⚠️ `stmia` cannot encode r8-r11 on every M-profile core, so the high
     * registers move through r2-r5 first. Writing the wide form would build on
     * a Cortex-M4 and fail to assemble for a Cortex-M0. */
    stmia   r0!, {r4-r7}
    mov     r2, r8
    mov     r3, r9
    mov     r4, r10
    mov     r5, r11
    stmia   r0!, {r2-r5}
    mov     r2, sp
    mov     r3, lr
    stmia   r0!, {r2-r3}

    adds    r1, r1, #16                 /* skip to the high-register block */
    ldmia   r1!, {r2-r5}
    mov     r8, r2
    mov     r9, r3
    mov     r10, r4
    mov     r11, r5
    ldmia   r1!, {r2-r3}
    mov     sp, r2
    mov     r12, r3                     /* the new LR, parked until r4-r7 land */
    subs    r1, r1, #40                 /* back to the start of `to` */
    ldmia   r1!, {r4-r7}
    bx      r12

    .size   arch_context_switch, . - arch_context_switch

/* The trampoline a fresh context returns into: the entry point is in r4 and its
 * argument in r5, placed there by arch_context_init.
 *
 * ⚠️ IT DOES NOT RETURN. A task function that runs off its end has nowhere to
 * go — there is no caller — so falling into a spin is the honest behaviour and
 * a `bx lr` here would branch to whatever the initial LR happened to be. */
    .section .text.openarch_cm_trampoline,"ax",%progbits
    .globl  openarch_cm_trampoline
    .type   openarch_cm_trampoline, %function
    .thumb_func
    .balign 4
openarch_cm_trampoline:
    mov     r0, r5
    blx     r4
1:  b       1b
    .size   openarch_cm_trampoline, . - openarch_cm_trampoline
