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

#include "../src/encode/pte_encode.h"

#include <cstdio>
#include <cstdlib>

namespace {

int g_failed = 0;

void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAILED: %s\n", what); ++g_failed; }
}

void check_eq(unsigned long long got, unsigned long long want, const char* what) {
    if (got != want) {
        std::printf("FAILED: %s\n  want 0x%016lx\n  got  0x%016lx\n",
                    what, want, got);
        ++g_failed;
    }
}

// The enumerator values, spelled as the header documents them.
constexpr int kRead = 0, kReadWrite = 1, kReadExec = 2, kReadWriteExec = 3;
constexpr int kNormal = 0, kDevice = 1;

constexpr unsigned long long kRvPage = 0x80200000UL;   // where riscv `virt` has RAM
constexpr unsigned long long kA64Page = 0x40200000UL;  // where aarch64 `virt` does

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

        // Every entry either maps its page or is not valid; there is no third
        // outcome, and a mis-shifted address field produces one.
        check(riscv64::entry_valid(rv),  "riscv64 entry is valid");
        check(aarch64::entry_valid(a64), "aarch64 entry is valid");
        check_eq(riscv64::entry_phys(rv),  kRvPage,  "riscv64 round-trips phys");
        check_eq(aarch64::entry_phys(a64), kA64Page, "aarch64 round-trips phys");

        (void)c.name;
    }

    // ⭐ THE ASSERTION THE SECOND ARCHITECTURE EXISTS TO MAKE.
    //
    // riscv denies kernel execution of user pages structurally: its `X` bit is
    // qualified by `U`, so nothing has to be set. aarch64 does not — PXN must be
    // written, and an encoder ported by analogy would omit it and produce a
    // machine on which user memory is kernel-executable. Nothing on riscv can
    // fail this check; it is here for the machine that can.
    constexpr unsigned long long kPxn = 1ULL << 53;
    constexpr unsigned long long kUxn = 1ULL << 54;
    for (int p = kRead; p <= kReadWriteExec; ++p) {
        const auto user = aarch64::encode_leaf(kA64Page, p, kNormal, true);
        check((user & kPxn) != 0,
              "aarch64 user page is never executable by the kernel");
        const auto kern = aarch64::encode_leaf(kA64Page, p, kNormal, false);
        check((kern & kUxn) != 0,
              "aarch64 kernel page is never executable by user code");
    }

    // Execution is denied unless it was asked for, on both machines.
    constexpr unsigned long long kRvX = 1ULL << 3;
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
    check_eq(riscv64::entry_phys(0), 0, "riscv64 invalid entry maps nothing");
    check_eq(aarch64::entry_phys(0), 0, "aarch64 invalid entry maps nothing");
}

}  // namespace

int main() {
    exact_values();
    agreement();
    if (g_failed == 0) std::printf("pte encoding ok\n");
    return g_failed == 0 ? 0 : 1;
}
