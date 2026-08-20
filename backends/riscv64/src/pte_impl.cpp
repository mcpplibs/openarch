// openarch.pte — the riscv64 binding.
//
// The encoder is a free function reachable on any target; this unit is what
// makes it THE implementation when the target is riscv64.
#include <openarch/abi.h>
#include <openarch/pte_encode.h>


extern "C" unsigned long long arch_pte_make_leaf(unsigned long long phys,
                                                 int perm, int mt, int user) {
    return arch::riscv64::encode_leaf(phys, perm, mt, user != 0);
}

extern "C" int arch_pte_valid(unsigned long long bits) { return arch::riscv64::entry_valid(bits) ? 1 : 0; }
extern "C" unsigned long long arch_pte_phys(unsigned long long bits) { return arch::riscv64::entry_phys(bits); }

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
extern "C" void arch_pte_install_memory_attributes(void) { }

