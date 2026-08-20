// openarch.cpu — the riscv64 backend.
#include <openarch/abi.h>


// ⚠️ `tp` IS A CONVENTION HERE, NOT AN ARCHITECTURAL REGISTER. The ABI reserves
// it for thread-local storage and the hardware assigns it no meaning, so a
// kernel may use it for its per-CPU pointer — but a hosted program on the same
// ISA would find its thread pointer there instead. aarch64's `TPIDR_EL1` is
// architectural and privileged, so no such collision exists.
//
// The consequence for a caller is nil, which is the point; the consequence for
// this backend is that it must not be compiled into anything that also uses a
// thread pointer.
extern "C" void* arch_cpu_percpu(void) {
    void* p;
    asm volatile("mv %0, tp" : "=r"(p));
    return p;
}

extern "C" void arch_cpu_set_percpu(void* p) {
    asm volatile("mv tp, %0" :: "r"(p));
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

