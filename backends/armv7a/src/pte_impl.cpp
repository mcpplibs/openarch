// openarch.pte — the armv7a backend, and the machine that answered the width
// question.
//
// THIS IS THE FIRST BACKEND WHOSE PAGE-TABLE ENTRY IS NOT EIGHT BYTES.
//
// The interface carries an entry in an `arch_u64` and, until this backend, said
// nothing about how one is stored — because it did not have to. `pte_encode.h`
// stated the assumption outright: "a page-table entry is 64 bits on every
// machine here". ARMv7-A's short-descriptor entry is 32 bits.
//
// The VALUE fits, so the carrier did not change. What was missing is a way to
// ask for the storage width: a kernel sizing a table from `sizeof(arch::pte)`
// builds one twice as large as the hardware walks, and the walker then reads
// every second word as an entry. Nothing diagnoses that — the table is
// well-formed, the entries are correct, and the machine reads the gaps.
// `arch_pte_entry_bytes()` exists because of this file.
#include <openarch/abi.h>
#include <openarch/pte_encode.h>

extern "C" arch_u64 arch_pte_make_leaf(arch_u64 phys, int perm, int mt,
                                       int user) {
    return arch::armv7a::encode_leaf(phys, perm, mt, user != 0);
}

extern "C" int arch_pte_valid(arch_u64 bits) {
    return arch::armv7a::entry_valid(bits) ? 1 : 0;
}

extern "C" arch_u64 arch_pte_phys(arch_u64 bits) {
    return arch::armv7a::entry_phys(bits);
}

// Nothing to program. The short-descriptor format carries the whole memory
// type in the entry, as riscv64 does and unlike aarch64, where the entry holds
// only an index into MAIR_EL1. An empty body here is the same statement the
// riscv64 backend makes.
extern "C" void arch_pte_install_memory_attributes(void) {}

// Four, and this is the number the interface had no way to report.
extern "C" arch_u32 arch_pte_entry_bytes(void) { return 4; }
