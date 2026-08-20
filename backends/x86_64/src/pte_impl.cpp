// The page-table entry, on x86_64.
//
// The encoding is in `openarch/pte_encode.h`, header-only and testable on any
// host. What is here is the part that cannot be: the writes to the registers
// that give the encoding its meaning.
#include <openarch/abi.h>
#include <openarch/pte_encode.h>

extern "C" arch_u64 arch_pte_make_leaf(arch_u64 phys,
                                                 int perm, int mt, int user) {
    return arch::x86_64::encode_leaf(phys, perm, mt, user != 0);
}

extern "C" int arch_pte_valid(arch_u64 bits) {
    return arch::x86_64::entry_valid(bits) ? 1 : 0;
}

extern "C" arch_u64 arch_pte_phys(arch_u64 bits) {
    return arch::x86_64::entry_phys(bits);
}

namespace {

constexpr unsigned kIa32Pat = 0x277u;

// ⭐ THE LAYOUT THE ENCODER'S INDICES INDEX INTO.
//
//   entry 0  0x06  write-back      — `memory_type::normal`
//   entry 1  0x00  uncacheable     — `memory_type::device`
//   entry 2  0x01  write-combining — not offered by the interface; present so
//                                    that a kernel needing it has an index
//                                    rather than having to reprogram the
//                                    register underneath this layer
//   entry 3  0x04  write-through
//
// Entries 4-7 repeat 0-3. Eight distinct types are available and four are
// enough; repeating rather than leaving them zero means an entry built with a
// stray `PAT` bit set lands on the same memory type as the one intended, rather
// than on uncacheable memory a page at a time.
//
// ⚠️ THIS MUST RUN BEFORE ANY MAPPING BUILT BY `make_leaf` IS USED, and the
// requirement is sharper than aarch64's. There, an unprogrammed `MAIR_EL1`
// field is zero and reads as Device-nGnRnE — the strictest type — so a
// too-early mapping is slow and correct. Here, `IA32_PAT`'s reset value has
// write-through at index 1, so a device mapping used before this call is
// CACHED-ish rather than uncached: writes reach the device eventually, at a
// time the program did not choose, and nothing faults.
constexpr arch_u64 kPatLayout =
    (0x06ULL <<  0) | (0x00ULL <<  8) | (0x01ULL << 16) | (0x04ULL << 24) |
    (0x06ULL << 32) | (0x00ULL << 40) | (0x01ULL << 48) | (0x04ULL << 56);

constexpr arch_u64 kCr4Smep = 1ULL << 20;
constexpr arch_u64 kCr4Smap = 1ULL << 21;

inline void wrmsr(unsigned msr, arch_u64 v) noexcept {
    asm volatile("wrmsr" ::
                 "c"(msr),
                 "a"(static_cast<unsigned>(v)),
                 "d"(static_cast<unsigned>(v >> 32)));
}

}  // namespace

extern "C" void arch_pte_install_memory_attributes(void) {
    wrmsr(kIa32Pat, kPatLayout);

    // ⚠️ SMEP IS PART OF THIS CALL BECAUSE THE ENCODER CANNOT EXPRESS WHAT THE
    // OTHER TWO MACHINES EXPRESS IN THE ENTRY.
    //
    // riscv qualifies its `X` bit by `U`, and aarch64 has separate `PXN` and
    // `UXN` bits, so on both of them "a user mapping is not kernel-executable"
    // is a property of the entry. x86_64 has one `NX` bit covering every
    // privilege level, so the same rule has to come from `CR4.SMEP`, which
    // faults when privileged code fetches an instruction from a user page.
    //
    // Putting it here rather than leaving it to the kernel is the same decision
    // `MAIR_EL1` was: the interface promises that `perm` means the same thing
    // on every machine, and on this one it does not unless this bit is set.
    //
    // ⚠️ SET IF SUPPORTED, RATHER THAN SET UNCONDITIONALLY. Writing a reserved
    // `CR4` bit raises #GP, so a processor without SMEP would fault inside a
    // function whose contract is that it always succeeds. `cpuid` leaf 7,
    // sub-leaf 0 reports SMEP in `ebx` bit 7 and SMAP in bit 20.
    unsigned max_leaf;
    {
        unsigned b, c, d;
        asm volatile("cpuid" : "=a"(max_leaf), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    }
    if (max_leaf < 7) return;

    unsigned features;
    {
        unsigned a, c, d;
        asm volatile("cpuid"
                     : "=a"(a), "=b"(features), "=c"(c), "=d"(d)
                     : "a"(7), "c"(0));
    }

    arch_u64 cr4;
    asm volatile("movq %%cr4, %0" : "=r"(cr4));
    if (features & (1u << 7))  cr4 |= kCr4Smep;
    // ⚠️ SMAP IS NOT SET, AND LEAVING IT OFF IS DELIBERATE. It faults when
    // privileged code READS a user page, which a kernel does on purpose — every
    // system call that copies an argument. Enabling it here would break callers
    // that have not been written to bracket those accesses with `stac`/`clac`,
    // and this layer has no way to know whether they have. The bit is named so
    // that a kernel enabling it does not have to rediscover which one it is.
    (void)kCr4Smap;
    asm volatile("movq %0, %%cr4" :: "r"(cr4) : "memory");
}
