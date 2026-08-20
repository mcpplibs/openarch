// openarch.pte — the aarch64 binding, and the register the encoding depends on.
module;
#include "../../encode/pte_encode.h"
module openarch.pte;

namespace arch {

pte make_leaf(unsigned long phys, perm p, memory_type mt, bool user) noexcept {
    return pte{ aarch64::encode_leaf(phys, static_cast<int>(p),
                                     static_cast<int>(mt), user) };
}

bool          is_valid(pte e) noexcept { return aarch64::entry_valid(e.bits); }
unsigned long phys_of (pte e) noexcept { return aarch64::entry_phys(e.bits);  }

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
void install_memory_attributes() noexcept {
    constexpr unsigned long kMair = 0x00FFUL;
    asm volatile("msr mair_el1, %0\n\tisb" :: "r"(kMair) : "memory");
}

}  // namespace arch
