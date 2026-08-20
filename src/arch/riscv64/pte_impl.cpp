// openarch.pte — the riscv64 binding.
//
// The encoder is a free function reachable on any target; this unit is what
// makes it THE implementation when the target is riscv64.
module;
#include "../../encode/pte_encode.h"
module openarch.pte;

namespace arch {

pte make_leaf(unsigned long long phys, perm p, memory_type mt, bool user) noexcept {
    return pte{ riscv64::encode_leaf(phys, static_cast<int>(p),
                                     static_cast<int>(mt), user) };
}

bool          is_valid(pte e) noexcept { return riscv64::entry_valid(e.bits); }
unsigned long long phys_of (pte e) noexcept { return riscv64::entry_phys(e.bits);  }

// ⚠️ EMPTY, AND CORRECTLY SO RATHER THAN AS A STUB.
//
// A riscv64 entry carries its own memory type in bits [62:61]. There is no
// register that must agree with it, so there is nothing to install, and the
// interface's promise — that `device` means the same thing on both machines —
// is already kept here by the encoding alone.
//
// The function exists on this machine so that a kernel calls it unconditionally
// during boot. An interface whose members appear only on the machines that need
// them would push the machine's identity back into every caller, which is the
// coupling this layer exists to remove.
void install_memory_attributes() noexcept { }

}  // namespace arch
