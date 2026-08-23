// The per-CPU pointer and the barriers, on x86_64.
//
// ⭐ THIS FILE IS WHERE THE THIRD MACHINE PAYS FOR THE BARRIER INTERFACE, AND
// WHAT IT PAYS IS ALMOST NOTHING — WHICH IS THE INTERESTING RESULT.
//
// riscv has one `fence` instruction over a cross-product of sets; aarch64 has
// three instructions over a shareability domain. Both are weakly ordered, so
// every one of `openarch.cpu`'s four barriers costs an instruction on both.
//
// x86_64 is total-store-order: loads are not reordered with loads, stores are
// not reordered with stores, and a store is not reordered with an older load.
// The only reordering the model permits is a load moving ahead of an older
// store to a different address. So three of the four barriers need no
// instruction at all — a compiler barrier is the whole of `memory` and `store`
// — and only the `complete` case, which must also order across device memory
// and serialise, needs `mfence`.
//
// ⚠️ THAT MAKES THE INTERFACE'S NAMES LOAD-BEARING RATHER THAN DECORATIVE. If
// `barrier` had been spelled the way either RISC machine spells it — as an
// instruction to emit — this backend would either emit instructions it does not
// need or would quietly ignore requests. Because the names say what is
// GUARANTEED, "guaranteed by the memory model" is a legitimate implementation
// and the interface survives a machine that was not consulted when it was
// designed. That is the only evidence available that a two-machine abstraction
// was an abstraction.
#include <openarch/abi.h>

namespace {

// `IA32_GS_BASE`. The processor keeps the `gs` segment's 64-bit base here, and
// nothing in the architecture reads it: it is the register x86_64 has that
// corresponds to riscv's `tp` and aarch64's `TPIDR_EL1`.
//
// ⚠️ `IA32_KERNEL_GS_BASE` (0xC0000102) IS A DIFFERENT REGISTER AND IS THE
// WRONG ONE HERE. It holds the value `swapgs` will exchange in, which is the
// mechanism for a kernel entered from user mode; a kernel that has not executed
// `swapgs` reads its own pointer from `IA32_GS_BASE`. Using the other one
// produces a per-CPU pointer that is correct only after a syscall.
constexpr unsigned kIa32GsBase = 0xC0000101u;
// ⚠️ `FS` AND NOT `GS`, AND ON THIS MACHINE THAT IS THE WHOLE DIFFERENCE
// BETWEEN THE TWO SLOTS. The System V ABI puts a program's thread-local storage
// at `%fs`; a kernel's per-CPU structure conventionally sits at `%gs`, which is
// also what `swapgs` exists to exchange on entry. Two registers, so unlike
// riscv64 the two slots here do not compete.
constexpr unsigned kIa32FsBase = 0xC0000100u;

inline arch_u64 rdmsr(unsigned msr) noexcept {
    unsigned lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (static_cast<arch_u64>(hi) << 32) | lo;
}

inline void wrmsr(unsigned msr, arch_u64 v) noexcept {
    asm volatile("wrmsr" ::
                 "c"(msr),
                 "a"(static_cast<unsigned>(v)),
                 "d"(static_cast<unsigned>(v >> 32)));
}

}  // namespace

extern "C" void* arch_cpu_percpu(void) {
    return reinterpret_cast<void*>(rdmsr(kIa32GsBase));
}

extern "C" void arch_cpu_set_percpu(void* p) {
    wrmsr(kIa32GsBase, reinterpret_cast<arch_u64>(p));
}

extern "C" void arch_cpu_fence(int b) {
    switch (b) {
        // ⚠️ A COMPILER BARRIER IS NOT "NOTHING", AND OMITTING IT WOULD MAKE
        // THIS THE ONE BACKEND THAT DOES NOT WORK. The processor will not
        // reorder these; the compiler will, and the `"memory"` clobber is what
        // stops it. A backend that emitted no instruction AND no clobber would
        // satisfy the architecture and break the program.
        case ARCH_BARRIER_MEMORY: asm volatile("" ::: "memory"); break;
        case ARCH_BARRIER_STORE:  asm volatile("" ::: "memory"); break;

        // The one case the memory model does not cover. `mfence` orders every
        // load and store before it against every one after, including accesses
        // to write-combining and uncacheable memory, which is what a caller
        // touching a device register or changing a translation needs.
        case ARCH_BARRIER_COMPLETE: asm volatile("mfence" ::: "memory"); break;

        // ⚠️ `cpuid` AND NOT `lfence`, AND THE DIFFERENCE IS THE WHOLE QUESTION
        // THIS BARRIER ASKS.
        //
        // `lfence` orders loads. It is not architecturally a serialising
        // instruction, so it does not guarantee that instruction FETCH sees
        // stores that preceded it — which is exactly what `fetch` promises, and
        // what riscv spells `fence.i` and aarch64 spells `isb`.
        //
        // `cpuid` is serialising by definition and is the sequence Intel's own
        // manual gives for self-modifying code across processors.
        //
        // ⚠️ IT IS WRITTEN WITH OPERANDS RATHER THAN AS A BARE CLOBBER LIST.
        // `cpuid` READS `eax` to choose which leaf to report, so an `asm` that
        // only declares it clobbered executes the instruction with whatever
        // happened to be in the register — a different leaf on every call, and
        // on some processors one that faults. Leaf 0 is defined on every
        // x86_64. The four outputs are named so the compiler knows the values
        // are gone rather than being told to preserve registers it cannot.
        case ARCH_BARRIER_FETCH: {
            unsigned a, b_, c, d;
            asm volatile("cpuid"
                         : "=a"(a), "=b"(b_), "=c"(c), "=d"(d)
                         : "a"(0)
                         : "memory");
            break;
        }

        default: break;
    }
}

extern "C" void* arch_cpu_tls(void) {
    return reinterpret_cast<void*>(rdmsr(kIa32FsBase));
}

extern "C" void arch_cpu_set_tls(void* p) {
    wrmsr(kIa32FsBase, reinterpret_cast<arch_u64>(p));
}
