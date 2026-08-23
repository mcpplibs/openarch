// openarch.cpu — the riscv64 backend.
#include <openarch/abi.h>


// ⭐⭐ `mscratch`, AND IT USED TO BE `tp`. THE WARNING THAT SAT HERE CAME TRUE.
//
// What this file said before, verbatim:
//
//     `tp' IS A CONVENTION HERE, NOT AN ARCHITECTURAL REGISTER. The ABI
//     reserves it for thread-local storage [...] a kernel may use it for its
//     per-CPU pointer — but a hosted program on the same ISA would find its
//     thread pointer there instead. [...] this backend must not be compiled
//     into anything that also uses a thread pointer.
//
// That last sentence is a constraint on every consumer, stated in a comment,
// enforced by nothing. It held while nothing on this architecture wanted a
// thread pointer. Measured 2026-08-23, something does: `openkal-opensbi`'s
// startup object writes `tp` so that a program's `thread_local` works, because
// on a machine with no operating system nobody else will.
//
// And the probe says so now rather than a program discovering it:
//
//     riscv64:  cpu: the two slots ALIAS
//     aarch64:  cpu: the two slots are distinct
//     x86_64:   cpu: the two slots are distinct
//
// ⇒ The per-CPU pointer moves to `mscratch`, which is the register this
// privilege level provides FOR THIS PURPOSE and which nothing else in this
// backend uses — trap.S saves and restores through the stack rather than
// swapping through it. `tp` is left to the thread pointer, whose owner it is by
// ABI. An S-mode variant of this backend would use `sscratch` for the same
// reason.
//
// ⚠️ THIS CHANGES WHICH REGISTER, NOT WHAT THE INTERFACE MEANS. A caller stores
// an opaque pointer and reads it back; that is as true after the move as
// before. What changes is that a caller may now also use `arch_cpu_tls`.
extern "C" void* arch_cpu_percpu(void) {
    void* p;
    asm volatile("csrr %0, mscratch" : "=r"(p));
    return p;
}

extern "C" void arch_cpu_set_percpu(void* p) {
    asm volatile("csrw mscratch, %0" :: "r"(p));
}

extern "C" void arch_cpu_fence(int b) {
    switch (b) {
        // ⚠️ The operands are SETS. `rw, rw` says "reads and writes before,
        // reads and writes after", and the instruction can express twelve other
        // points in that product that this interface deliberately does not.
        case 0:   asm volatile("fence rw, rw" ::: "memory"); break;
        case 1:    asm volatile("fence w, w"   ::: "memory"); break;
        // ⚠️ riscv has no separate "completed" barrier: `fence` with the device
        // bits is what orders against a device, and for ordinary memory the
        // architecture does not distinguish ordering from completion the way
        // aarch64's `dmb`/`dsb` pair does. `fence rw, rw` with the io bits is
        // the strongest statement available, and it is what `complete` means
        // here.
        case 2: asm volatile("fence iorw, iorw" ::: "memory"); break;
        case 3: asm volatile("fence.i" ::: "memory"); break;
        default: break;
    }
}


// `tp`, which the ABI reserves for exactly this and which nothing else in this
// backend now touches. The per-CPU pointer moved out of it — see above.
extern "C" void* arch_cpu_tls(void) {
    void* p;
    asm volatile("mv %0, tp" : "=r"(p));
    return p;
}

extern "C" void arch_cpu_set_tls(void* p) {
    asm volatile("mv tp, %0" :: "r"(p));
}
