// openarch.cpu — the Cortex-M backend.
#include <openarch/abi.h>

namespace {
// ⚠️ A VARIABLE, NOT A REGISTER, AND THE MANIFEST DECLINES THE CAPABILITY THAT
// WOULD CLAIM OTHERWISE.
//
// The other three backends put this in a register the privilege level provides
// — `TPIDR_EL1`, `mscratch`, `GS`. M-profile has no TPIDR-class register at
// all, so the only implementation available is a static, which is correct on a
// single-core part and says nothing about a multi-core one.
//
// That is why `openarch:percpu-register` is absent from this package's
// `provides`. The function exists so a portable kernel links; the capability is
// withheld so a kernel that needs a real per-CPU slot is refused at resolution
// rather than silently sharing one word across cores.
void* g_percpu = nullptr;
}  // namespace

extern "C" void* arch_cpu_percpu(void)        { return g_percpu; }
extern "C" void  arch_cpu_set_percpu(void* p) { g_percpu = p; }

extern "C" void arch_cpu_fence(int b) {
    switch (b) {
        // `dmb` orders accesses as other agents observe them; `dsb` waits for
        // them to complete. A driver that writes a device register and then
        // expects the device to have seen it needs the second.
        case ARCH_BARRIER_MEMORY:
        case ARCH_BARRIER_STORE:    asm volatile("dmb" ::: "memory"); break;
        case ARCH_BARRIER_COMPLETE: asm volatile("dsb" ::: "memory"); break;
        // ⚠️ `isb` AND NOT `dsb`, AND THE DIFFERENCE MATTERS MOST ON THIS
        // MACHINE. After writing VTOR or reprogramming the MPU, the pipeline
        // may still hold instructions fetched under the old configuration.
        // `dsb` retires the write; only `isb` discards what was fetched.
        case ARCH_BARRIER_FETCH:    asm volatile("isb" ::: "memory"); break;
        default:                    asm volatile("dmb" ::: "memory"); break;
    }
}

// ⚠️ THE TWO SLOTS ALIAS ON THIS MACHINE, AND THE PROBE MUST BE ABLE TO SAY SO.
//
// openarch distinguishes the pointer the PROCESSOR owns from the one the
// running CONTEXT owns. On aarch64 and x86_64 they are different registers; on
// riscv64 they were made distinct by moving per-CPU to `mscratch`. M-profile
// has neither register, so both are statics — and a probe asserting only that
// each round-trips would pass while measuring nothing.
namespace { void* g_tls = nullptr; }
extern "C" void* arch_cpu_tls(void)        { return g_tls; }
extern "C" void  arch_cpu_set_tls(void* p) { g_tls = p; }
