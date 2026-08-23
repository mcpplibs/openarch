// openarch.cpu — the aarch64 backend.
#include <openarch/abi.h>


// `TPIDR_EL1` is architectural and readable only at EL1 or above, which is
// where a kernel is. Unlike riscv's `tp` it cannot collide with a hosted
// program's thread pointer, because a hosted program cannot reach it.
extern "C" void* arch_cpu_percpu(void) {
    void* p;
    asm volatile("mrs %0, tpidr_el1" : "=r"(p));
    return p;
}

extern "C" void arch_cpu_set_percpu(void* p) {
    asm volatile("msr tpidr_el1, %0" :: "r"(p));
}

extern "C" void arch_cpu_fence(int b) {
    switch (b) {
        // ⚠️ THREE INSTRUCTIONS, NOT ONE WITH OPERANDS, AND THE DIFFERENCE
        // BETWEEN THE FIRST TWO IS THE REASON `complete` EXISTS IN THE
        // INTERFACE.
        //
        // `dmb` orders accesses as other agents observe them; it does not wait
        // for them. `dsb` waits. A kernel that writes a device register and
        // then expects the device to have seen it needs the second, and a
        // caller that had only `memory` would silently get the first.
        case 0:   asm volatile("dmb sy" ::: "memory"); break;
        case 1:    asm volatile("dmb st" ::: "memory"); break;
        case 2: asm volatile("dsb sy" ::: "memory"); break;
        // `isb` is not a memory barrier at all: it discards what the fetch path
        // has already decided. riscv spells the same intent `fence.i`, which
        // sits in the fence family there and does not here.
        case 3:    asm volatile("isb" ::: "memory"); break;
        default: break;
    }
}


// `TPIDR_EL0` is the one a program reaches, and it is a DIFFERENT register from
// the `TPIDR_EL1` above — so on this machine the two slots do not compete.
// Readable and writable at EL1; readable by a program at EL0, which is what
// makes it the thread pointer.
extern "C" void* arch_cpu_tls(void) {
    void* p;
    asm volatile("mrs %0, tpidr_el0" : "=r"(p));
    return p;
}

extern "C" void arch_cpu_set_tls(void* p) {
    asm volatile("msr tpidr_el0, %0" :: "r"(p));
}
