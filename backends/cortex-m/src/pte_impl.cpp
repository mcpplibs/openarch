// openarch.pte — the group this machine does not have.
//
// ⭐⭐ THESE FUNCTIONS EXIST AND REFUSE. THEY DO NOT PRETEND.
//
// M-profile's MPU describes regions by base address and limit. There is no
// entry that names a physical page, no table for a walker to descend, and no
// bit pattern that `arch_pte_make_leaf` could return which any part of the
// machine would interpret. Returning zero would be a value a caller could
// store into a table that does not exist.
//
// A backend that omitted these symbols entirely would fail to link for every
// consumer, including the ones that never call them — and most kernels on this
// class of part never do. So they link and they abort, while the package's
// `provides` withholds `openarch:address-space` so that a kernel which DOES
// need an address space is refused at resolution, by name, before anything is
// compiled.
//
// ⚠️ The abort is deliberately loud rather than a silent zero. A kernel that
// reaches here has a bug in its own layering, and the useful moment to learn
// that is the first call.
#include <openarch/abi.h>

extern "C" {

// Supplied by the program (a board package or the kernel). Declared weak so a
// unit test can link this translation unit without one.
__attribute__((weak)) void openarch_panic(const char* what);

static void refuse(const char* what) {
    if (openarch_panic) openarch_panic(what);
    for (;;) { asm volatile("bkpt 0xAB"); }
}

arch_u64 arch_pte_make_leaf(arch_u64, int, int, int) {
    refuse("openarch: this machine has no address space (M-profile has an MPU, "
           "not an MMU). Require 'openarch:address-space' to be refused at "
           "resolution instead.");
    return 0;
}
int      arch_pte_valid(arch_u64)            { refuse("arch_pte_valid"); return 0; }
arch_u64 arch_pte_phys(arch_u64)             { refuse("arch_pte_phys");  return 0; }

// ⭐ THE ONE PTE-GROUP FUNCTION THAT IS MEANINGFUL HERE, AND IT IS A NO-OP FOR
// THE SAME REASON IT IS ON riscv64: there is no attribute register to program
// before a memory type becomes meaningful. On M-profile the MPU's own MAIR
// registers serve that role and are programmed by whoever configures regions,
// which is not this layer.
void arch_pte_install_memory_attributes(void) {}

}  // extern "C"
