// openarch.pte — the aarch64 binding, and the register the encoding depends on.
#include <openarch/abi.h>
#include <openarch/pte_encode.h>


extern "C" arch_u64 arch_pte_make_leaf(arch_u64 phys,
                                                 int perm, int mt, int user) {
    return arch::aarch64::encode_leaf(phys, perm, mt, user != 0);
}

extern "C" int arch_pte_valid(arch_u64 bits) { return arch::aarch64::entry_valid(bits) ? 1 : 0; }
extern "C" arch_u64 arch_pte_phys(arch_u64 bits) { return arch::aarch64::entry_phys(bits); }

// The canonical MAIR_EL1 layout the encoder's AttrIndx values index into.
//
//   attr0 (byte 0)  0xFF  Normal memory, inner and outer write-back
//                         non-transient, read- and write-allocate
//   attr1 (byte 1)  0x00  Device-nGnRnE: non-gathering, non-reordering,
//                         no early write acknowledgement
//
// The remaining six attributes stay zero, which is Device-nGnRnE as well —
// chosen rather than accepted, because it means an entry carrying an index this
// layer never emits describes the strictest memory type rather than a cached
// one. A wrong index then costs performance; the other way round it would cost
// correctness on a device register.
//
// ⚠️ `isb` after the write, and it is not decoration: the barrier is what makes
// the new attributes apply to translations that follow. Without it the first
// mappings installed after boot may be walked against the reset value of the
// register, which is architecturally UNKNOWN.
extern "C" void arch_pte_install_memory_attributes(void) {
    constexpr arch_u64 kMair = 0x00FFUL;
    asm volatile("msr mair_el1, %0\n\tisb" :: "r"(kMair) : "memory");
}

