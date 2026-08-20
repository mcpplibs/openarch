// The page-table encoders, asserted on the host.
//
// WHY THIS TEST EXISTS ALONGSIDE TWO EMULATOR JOBS
//
// An emulator answers "does the machine boot". It does not answer "is bit 53
// the one that denies privileged execution", because a mapping with the wrong
// permission bits still maps, still boots, and faults later in a place that
// names neither the encoder nor the entry. The bit patterns are the half of
// this layer that must be read against the architecture manuals, and reading
// them is what this file does.
//
// ⭐ IT RUNS ON THE HOST, FOR BOTH MACHINES AT ONCE. That is what the
// `probe-riscv64` and `probe-aarch64` features are for: the encoders are pure
// functions in per-architecture namespaces, so a build for x86_64 can hold both
// and compare them. The comparison is the point — several assertions below are
// about the two encoders AGREEING, and no single-target build could make them.

#include <openarch/pte_encode.h>

#include <cstdio>
#include <cstdlib>

namespace {

int g_failed = 0;

void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAILED: %s\n", what); ++g_failed; }
}

void check_eq(arch_u64 got, arch_u64 want, const char* what) {
    if (got != want) {
        std::printf("FAILED: %s\n  want 0x%016lx\n  got  0x%016lx\n",
                    what, want, got);
        ++g_failed;
    }
}

// The enumerator values, spelled as the header documents them.
constexpr int kRead = 0, kReadWrite = 1, kReadExec = 2, kReadWriteExec = 3;
constexpr int kNormal = 0, kDevice = 1;

constexpr arch_u64 kRvPage = 0x80200000UL;   // where riscv `virt` has RAM
constexpr arch_u64 kA64Page = 0x40200000UL;  // where aarch64 `virt` does
constexpr arch_u64 kX86Page = 0x00200000UL;  // where an x86 `-kernel` image sits

// ── The exact encodings, derived from the manuals ───────────────────────────
//
// Written out rather than computed from the same constants the encoder uses:
// a test that rebuilt the value the same way would agree with any mistake.
void exact_values() {
    using namespace arch;

    // riscv64, Sv39 leaf: V|R|W|A|D = 0xC7, PPN = phys>>12 placed at bit 10.
    check_eq(riscv64::encode_leaf(kRvPage, kReadWrite, kNormal, false),
             0x20080000UL | 0xC7UL,
             "riscv64 normal read-write kernel page");

    // The same, plus Svpbmt IO in bits [62:61].
    check_eq(riscv64::encode_leaf(kRvPage, kReadWrite, kDevice, false),
             0x4000000000000000UL | 0x20080000UL | 0xC7UL,
             "riscv64 device read-write kernel page");

    // aarch64 stage-1 page: valid(0b11) | AF(bit10) | AP=RW-EL1(0)
    //                     | SH=inner(0b11 at bit 8) | AttrIndx=0
    //                     | UXN(bit 54) | PXN(bit 53, because not executable)
    check_eq(aarch64::encode_leaf(kA64Page, kReadWrite, kNormal, false),
             0x0060000000000000UL | kA64Page | 0x703UL,
             "aarch64 normal read-write kernel page");

    // Device: AttrIndx = 1 (bit 2), and no shareability.
    check_eq(aarch64::encode_leaf(kA64Page, kReadWrite, kDevice, false),
             0x0060000000000000UL | kA64Page | 0x407UL,
             "aarch64 device read-write kernel page");

    // x86_64 4-level leaf: P|RW|A|D = 0x63, plus NX in bit 63 because the
    // mapping is not executable. PAT index 0, so none of PWT/PCD/PAT is set.
    check_eq(x86_64::encode_leaf(kX86Page, kReadWrite, kNormal, false),
             0x8000000000000000UL | kX86Page | 0x63UL,
             "x86_64 normal read-write kernel page");

    // Device: PAT index 1, which on this machine is the PWT bit alone.
    check_eq(x86_64::encode_leaf(kX86Page, kReadWrite, kDevice, false),
             0x8000000000000000UL | kX86Page | 0x6BUL,
             "x86_64 device read-write kernel page");
}

// ── The properties both machines must satisfy ───────────────────────────────
//
// These are the assertions that could not be written with one backend, and they
// are the ones that would have caught the mistake the aarch64 encoder was most
// at risk of making.
void agreement() {
    using namespace arch;

    struct Case { int perm; int mt; bool user; const char* name; };
    constexpr Case cases[] = {
        {kRead,          kNormal, false, "read/normal/kernel"},
        {kReadWrite,     kNormal, false, "rw/normal/kernel"},
        {kReadExec,      kNormal, false, "rx/normal/kernel"},
        {kReadWriteExec, kNormal, false, "rwx/normal/kernel"},
        {kRead,          kNormal, true,  "read/normal/user"},
        {kReadWrite,     kNormal, true,  "rw/normal/user"},
        {kReadExec,      kNormal, true,  "rx/normal/user"},
        {kReadWrite,     kDevice, false, "rw/device/kernel"},
    };

    for (const auto& c : cases) {
        const auto rv  = riscv64::encode_leaf(kRvPage,  c.perm, c.mt, c.user);
        const auto a64 = aarch64::encode_leaf(kA64Page, c.perm, c.mt, c.user);
        const auto x86 = x86_64::encode_leaf(kX86Page,  c.perm, c.mt, c.user);

        // Every entry either maps its page or is not valid; there is no third
        // outcome, and a mis-shifted address field produces one.
        check(riscv64::entry_valid(rv),  "riscv64 entry is valid");
        check(aarch64::entry_valid(a64), "aarch64 entry is valid");
        check(x86_64::entry_valid(x86),  "x86_64 entry is valid");
        check_eq(riscv64::entry_phys(rv),  kRvPage,  "riscv64 round-trips phys");
        check_eq(aarch64::entry_phys(a64), kA64Page, "aarch64 round-trips phys");
        check_eq(x86_64::entry_phys(x86),  kX86Page, "x86_64 round-trips phys");

        (void)c.name;
    }

    // ⭐ THE ASSERTION THE SECOND ARCHITECTURE EXISTS TO MAKE.
    //
    // riscv denies kernel execution of user pages structurally: its `X` bit is
    // qualified by `U`, so nothing has to be set. aarch64 does not — PXN must be
    // written, and an encoder ported by analogy would omit it and produce a
    // machine on which user memory is kernel-executable. Nothing on riscv can
    // fail this check; it is here for the machine that can.
    constexpr arch_u64 kPxn = 1ULL << 53;
    constexpr arch_u64 kUxn = 1ULL << 54;
    for (int p = kRead; p <= kReadWriteExec; ++p) {
        const auto user = aarch64::encode_leaf(kA64Page, p, kNormal, true);
        check((user & kPxn) != 0,
              "aarch64 user page is never executable by the kernel");
        const auto kern = aarch64::encode_leaf(kA64Page, p, kNormal, false);
        check((kern & kUxn) != 0,
              "aarch64 kernel page is never executable by user code");
    }

    // Execution is denied unless it was asked for, on both machines.
    constexpr arch_u64 kRvX = 1ULL << 3;
    check((riscv64::encode_leaf(kRvPage, kReadWrite, kNormal, false) & kRvX) == 0,
          "riscv64 leaves X clear for a non-executable mapping");
    check((aarch64::encode_leaf(kA64Page, kReadWrite, kNormal, false) & kPxn) != 0,
          "aarch64 sets PXN for a non-executable kernel mapping");
    check((aarch64::encode_leaf(kA64Page, kReadExec, kNormal, false) & kPxn) == 0,
          "aarch64 clears PXN for an executable kernel mapping");

    // Device memory differs from normal memory on both, and differs in the
    // field each machine keeps it in.
    check(riscv64::encode_leaf(kRvPage, kReadWrite, kDevice, false)
              != riscv64::encode_leaf(kRvPage, kReadWrite, kNormal, false),
          "riscv64 distinguishes device from normal");
    check(aarch64::encode_leaf(kA64Page, kReadWrite, kDevice, false)
              != aarch64::encode_leaf(kA64Page, kReadWrite, kNormal, false),
          "aarch64 distinguishes device from normal");

    // An entry's low bits are the entry's own: a caller passing an unaligned
    // address gets the page containing it rather than a corrupted field.
    check_eq(riscv64::entry_phys(riscv64::encode_leaf(kRvPage + 0xFFF,
                                                      kReadWrite, kNormal, false)),
             kRvPage, "riscv64 discards sub-page bits of phys");
    check_eq(aarch64::entry_phys(aarch64::encode_leaf(kA64Page + 0xFFF,
                                                      kReadWrite, kNormal, false)),
             kA64Page, "aarch64 discards sub-page bits of phys");

    // Nothing is a valid entry by accident.
    check(!riscv64::entry_valid(0),  "riscv64 zero is not a valid entry");
    check(!aarch64::entry_valid(0),  "aarch64 zero is not a valid entry");
    check(!x86_64::entry_valid(0),   "x86_64 zero is not a valid entry");
    check_eq(riscv64::entry_phys(0), 0, "riscv64 invalid entry maps nothing");
    check_eq(aarch64::entry_phys(0), 0, "aarch64 invalid entry maps nothing");
    check_eq(x86_64::entry_phys(0),  0, "x86_64 invalid entry maps nothing");

    // ⭐ THE ASSERTION THE THIRD ARCHITECTURE EXISTS TO MAKE, AND THE ONE THAT
    // RECORDS WHAT IT COULD NOT DO.
    //
    // The two checks above about kernel/user execution cannot be written for
    // x86_64: it has ONE `NX` bit covering every privilege level, so "a user
    // page is not kernel-executable" is not expressible in the entry at all.
    // The rule is enforced by `CR4.SMEP`, which the backend's
    // `install_memory_attributes` sets. What IS assertable here is that the
    // encoder does not pretend otherwise — an executable user page and an
    // executable kernel page differ only in `U/S`, and both leave `NX` clear.
    constexpr arch_u64 kNx = 1ULL << 63;
    constexpr arch_u64 kUs = 1ULL << 2;
    const auto x_user = x86_64::encode_leaf(kX86Page, kReadExec, kNormal, true);
    const auto x_kern = x86_64::encode_leaf(kX86Page, kReadExec, kNormal, false);
    check((x_user & kNx) == 0 && (x_kern & kNx) == 0,
          "x86_64 leaves NX clear for an executable mapping at either level");
    check_eq(x_user, x_kern | kUs,
          "x86_64 user and kernel executable pages differ only in U/S");
    check((x86_64::encode_leaf(kX86Page, kReadWrite, kNormal, false) & kNx) != 0,
          "x86_64 sets NX for a non-executable mapping");

    // Device memory differs from normal memory on this machine too, and in a
    // third field again: a PAT index rather than a type or an AttrIndx.
    check(x86_64::encode_leaf(kX86Page, kReadWrite, kDevice, false)
              != x86_64::encode_leaf(kX86Page, kReadWrite, kNormal, false),
          "x86_64 distinguishes device from normal");
    check_eq(x86_64::entry_phys(x86_64::encode_leaf(kX86Page + 0xFFF,
                                                    kReadWrite, kNormal, false)),
             kX86Page, "x86_64 discards sub-page bits of phys");
}

}  // namespace

int main() {
    exact_values();
    agreement();
    if (g_failed == 0) std::printf("pte encoding ok\n");
    return g_failed == 0 ? 0 : 1;
}
