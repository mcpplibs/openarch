// openarch.cpu — the armv7a backend.
#include <openarch/abi.h>

// TPIDRPRW (c13, c0, 4) is the privileged-only thread ID register: readable and
// writable at PL1, invisible to a program at PL0. It is this machine's peer of
// aarch64's TPIDR_EL1 and, like it, cannot collide with a program's own thread
// pointer.
extern "C" void* arch_cpu_percpu(void) {
    void* p;
    asm volatile("mrc p15, 0, %0, c13, c0, 4" : "=r"(p));
    return p;
}

extern "C" void arch_cpu_set_percpu(void* p) {
    asm volatile("mcr p15, 0, %0, c13, c0, 4" :: "r"(p));
}

extern "C" void arch_cpu_fence(int b) {
    switch (b) {
        // The same four intents the other backends implement, spelled in this
        // machine's instructions. `dmb` orders; `dsb` waits; `isb` discards
        // what the fetch path has already decided.
        //
        // `ish` — inner shareable — rather than `sy`: a kernel's own data
        // structures are shared with the other cores in its inner domain, and
        // the full-system domain would also order against agents this layer
        // never speaks for. `dsb` keeps `sy` because its callers are waiting
        // for a device to have observed a write.
        case 0: asm volatile("dmb ish"   ::: "memory"); break;
        case 1: asm volatile("dmb ishst" ::: "memory"); break;
        case 2: asm volatile("dsb sy"    ::: "memory"); break;
        case 3: asm volatile("isb"       ::: "memory"); break;
        default: break;
    }
}

// TPIDRURW (c13, c0, 2) is the one a program reaches, and it is a DIFFERENT
// register from TPIDRPRW above — so on this machine, as on aarch64 and unlike
// riscv, the per-CPU slot and the thread pointer do not compete for one
// register.
extern "C" void* arch_cpu_tls(void) {
    void* p;
    asm volatile("mrc p15, 0, %0, c13, c0, 2" : "=r"(p));
    return p;
}

extern "C" void arch_cpu_set_tls(void* p) {
    asm volatile("mcr p15, 0, %0, c13, c0, 2" :: "r"(p));
}
