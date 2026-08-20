/* The per-architecture entry encoders, as pure inline functions.
 *
 * ⚠️ HEADER-ONLY, AND THAT IS THE RESULT OF TWO MEASUREMENTS RATHER THAN A
 * STYLE PREFERENCE.
 *
 * These were separate translation units selected by a feature, so that a host
 * build could hold both and compare them. Two measurements ended that design:
 *
 *   1. A file named by ANY feature belongs to that feature exclusively. With
 *      the feature inactive it is not compiled even when a `cfg` block lists it
 *      explicitly — and the build still reports success, because a library
 *      missing a definition only fails at the CONSUMER'S link, in a package the
 *      reader did not write.
 *   2. Compiling both encoders unconditionally put 450 bytes of the foreign one
 *      into a riscv64 image whose whole probe is about 2000 — measured with
 *      `nm --size-sort`, not estimated. On a bare-metal target that is not a
 *      rounding error.
 *
 * `inline` satisfies both without a knob. A target build instantiates only the
 * encoder it calls, so the foreign one contributes nothing; a host build that
 * calls both gets both. There is no feature to activate, no file that can go
 * silently missing, and nothing for a consumer to remember.
 *
 * ⭐ THE HOST-TESTABILITY IS THE POINT, NOT A SIDE EFFECT. Building an entry is
 * pure bit manipulation, and it is the half of this layer most likely to be
 * wrong in a way no emulator reveals: a mis-shifted field still maps, still
 * boots, and faults later somewhere that names neither the encoder nor the
 * entry. Reachable on any host, both encoders can be asserted against the
 * architecture manuals by an ordinary unit test — including assertions that the
 * two AGREE, which no single-target build could express.
 *
 * ⚠️ The constants below are `inline constexpr` at namespace scope rather than
 * in an unnamed namespace. In a header the latter would give every translation
 * unit its own entity, and an inline function referring to one is an ODR
 * violation that no compiler is required to report.
 *
 * ⚠️ `arch_u64` THROUGHOUT, AND CROSS-PLATFORM CI IS WHAT MADE THAT NECESSARY.
 *
 * A page-table entry is 64 bits on every machine here. These constants were
 * once `unsigned long`, which is 64 bits on the systems this was written on and
 * 32 on Windows — so `1UL << 53` was a shift wider than the type: undefined,
 * and in practice silently zero rather than an error. An encoder built there
 * would have produced entries with every high field missing and no diagnostic
 * at all. The host job caught it only because `constexpr` forces the shift to
 * be evaluated at compile time, which turns the silent case into `must be
 * initialized by a constant expression`.
 *
 * `arch_u64` is defined and ASSERTED to be eight bytes in `openarch/types.h`,
 * which is the difference between a rule and a mechanism.
 *
 * The integer parameters mirror the module's enumerators in declaration order:
 *
 *   perm: 0 read, 1 read_write, 2 read_exec, 3 read_write_exec
 *   mt:   0 normal, 1 device
 *
 * An out-of-range value is the caller's error, and the encoders treat anything
 * they do not recognise as the most restrictive choice.
 */
#ifndef OPENARCH_PTE_ENCODE_H
#define OPENARCH_PTE_ENCODE_H

#include <openarch/types.h>

namespace arch {

// ── riscv64: the Sv39/Sv48 leaf, per the privileged specification ───────────
//
// The format is the same for both page-table depths; only how many levels are
// walked differs, and that is the walker's business rather than the entry's.
namespace riscv64 {

inline constexpr arch_u64 kV = 1ULL << 0;   // valid
inline constexpr arch_u64 kR = 1ULL << 1;   // readable
inline constexpr arch_u64 kW = 1ULL << 2;   // writable
inline constexpr arch_u64 kX = 1ULL << 3;   // executable
inline constexpr arch_u64 kU = 1ULL << 4;   // reachable from user mode
inline constexpr arch_u64 kA = 1ULL << 6;   // accessed
inline constexpr arch_u64 kD = 1ULL << 7;   // dirty

// ⚠️ Svpbmt, bits [62:61]: 0 = PMA (whatever the platform says the region is),
// 1 = NC (non-cacheable, idempotent), 2 = IO (non-cacheable, non-idempotent).
// Device memory is IO rather than NC: reading a device register can have an
// effect, which is exactly what "non-idempotent" names.
//
// ⭐ THE ENTIRE MEMORY TYPE IS HERE, IN THE ENTRY. That is what the aarch64
// encoder below cannot reproduce, and the reason `install_memory_attributes`
// exists in the interface at all.
inline constexpr arch_u64 kPbmtIo = 2ULL << 61;

// The physical page number occupies [53:10].
inline constexpr arch_u64 kPpnShift = 10;
inline constexpr arch_u64 kPpnMask  = ((1ULL << 44) - 1) << kPpnShift;

inline arch_u64 encode_leaf(arch_u64 phys, int perm, int mt,
                                 bool user) noexcept {
    arch_u64 e = kV | kA | kR;

    // ⚠️ `D` is set whenever the mapping is writable. A hart is permitted to
    // fault on the first write to a clean page, and nothing in this layer would
    // service that fault; a kernel that wants dirty tracking clears the bit
    // itself, which is a policy this layer does not make for it.
    switch (perm) {
        case 1: e |= kW | kD;       break;   // read_write
        case 2: e |= kX;            break;   // read_exec
        case 3: e |= kW | kD | kX;  break;   // read_write_exec
        default:                    break;   // read
    }
    if (user)    e |= kU;
    if (mt == 1) e |= kPbmtIo;

    return e | (((phys >> 12) << kPpnShift) & kPpnMask);
}

inline bool entry_valid(arch_u64 bits) noexcept { return (bits & kV) != 0; }

inline arch_u64 entry_phys(arch_u64 bits) noexcept {
    if (!entry_valid(bits)) return 0;
    return ((bits & kPpnMask) >> kPpnShift) << 12;
}

}  // namespace riscv64

// ── aarch64: the stage-1 leaf descriptor, 4KiB granule ──────────────────────
//
// ⭐ THIS IS WHERE THE INTERFACE ALMOST BROKE, AND THE SHAPE OF THE ESCAPE.
//
// A riscv64 entry says what kind of memory it maps. This one does not: bits
// [4:2] hold `AttrIndx`, a three-bit INDEX into `MAIR_EL1`, and what index one
// means is whatever a privileged write to that register last said. Two entries
// with identical bits describe different memory on two CPUs.
//
// So this layer owns the register: the layout `install_memory_attributes`
// writes is what the indices below index into.
namespace aarch64 {

// The descriptor's low two bits: 0b11 is a valid page at the last level.
inline constexpr arch_u64 kValidPage = 3ULL;

// AttrIndx, bits [4:2], indexing the layout install_memory_attributes writes:
//
//   index 0  0xFF  Normal, inner and outer write-back, read/write-allocate
//   index 1  0x00  Device-nGnRnE
//
// Two entries and not eight, because the interface distinguishes two kinds of
// memory. An unused MAIR field is zero, which reads as Device-nGnRnE — the
// strictest type — so a mis-encoded index degrades to "slow and correct" rather
// than to "cached device register".
inline constexpr arch_u64 kAttrNormal = 0ULL << 2;
inline constexpr arch_u64 kAttrDevice = 1ULL << 2;

// AP, bits [7:6]. ⚠️ aarch64 states permission as a PAIR of levels rather than
// as one set of bits per level, which is the second place the two machines
// disagree: riscv has a single `U` bit orthogonal to R/W/X.
inline constexpr arch_u64 kApRwEl1    = 0ULL << 6;   // read-write, privileged
inline constexpr arch_u64 kApRwEl0El1 = 1ULL << 6;   // read-write, both
inline constexpr arch_u64 kApRoEl1    = 2ULL << 6;   // read-only, privileged
inline constexpr arch_u64 kApRoEl0El1 = 3ULL << 6;   // read-only, both

// SH, bits [9:8]. Inner shareable for normal memory; device memory ignores the
// field, and zero is what the manual's examples leave there.
inline constexpr arch_u64 kShInner = 3ULL << 8;

inline constexpr arch_u64 kAf  = 1ULL << 10;   // access flag
inline constexpr arch_u64 kPxn = 1ULL << 53;   // privileged execute never
inline constexpr arch_u64 kUxn = 1ULL << 54;   // unprivileged execute never

// The output address occupies [47:12].
inline constexpr arch_u64 kOaMask = ((1ULL << 36) - 1) << 12;

inline arch_u64 encode_leaf(arch_u64 phys, int perm, int mt,
                                 bool user) noexcept {
    arch_u64 e = kValidPage | kAf;

    const bool writable   = (perm == 1 || perm == 3);
    const bool executable = (perm == 2 || perm == 3);

    if (user) e |= writable ? kApRwEl0El1 : kApRoEl0El1;
    else      e |= writable ? kApRwEl1    : kApRoEl1;

    // ⚠️ EXECUTION IS DENIED TO THE LEVEL THAT DOES NOT OWN THE MAPPING,
    // ALWAYS.
    //
    // A user mapping is never executable by the kernel and a kernel mapping is
    // never executable by user code, whatever `perm` says. That is not a policy
    // being chosen here; it is the only reading under which `perm` means the
    // same thing on both machines. riscv expresses it structurally — its `X`
    // bit is qualified by `U`, so a user page is not kernel-executable by
    // construction — and aarch64 needs both bits written to reach the same
    // arrangement. An encoder ported by analogy would omit them and produce a
    // machine on which user memory is kernel-executable.
    if (user) {
        e |= kPxn;
        if (!executable) e |= kUxn;
    } else {
        e |= kUxn;
        if (!executable) e |= kPxn;
    }

    if (mt == 1) e |= kAttrDevice;
    else         e |= kAttrNormal | kShInner;

    return e | (phys & kOaMask);
}

inline bool entry_valid(arch_u64 bits) noexcept { return (bits & 1ULL) != 0; }

inline arch_u64 entry_phys(arch_u64 bits) noexcept {
    if (!entry_valid(bits)) return 0;
    return bits & kOaMask;
}

}  // namespace aarch64

// ── x86_64: the 4-level paging leaf entry, 4KiB page ────────────────────────
//
// ⭐ THE THIRD MACHINE SETTLED A QUESTION THE FIRST TWO LEFT OPEN.
//
// `openarch.pte` owns `MAIR_EL1` because aarch64's entry holds an INDEX rather
// than a memory type, and riscv's holds the type itself. With two machines that
// was one-against-one, and "this layer owns the attribute register" could
// fairly be called a workaround for aarch64.
//
// x86_64 does the same thing. `PWT`, `PCD` and `PAT` are three scattered bits
// that together form a three-bit index into `IA32_PAT`, a model-specific
// register holding eight one-byte memory types. The entry says "type number
// one"; what number one means is whatever was last written to the MSR. That is
// aarch64's arrangement with different names, on a machine that shares no
// lineage with it — so the majority is now two-to-one the other way, and owning
// the register is the general case rather than the exception.
//
// ⚠️ THE THREE INDEX BITS ARE NOT ADJACENT, WHICH IS THE ONE PLACE THIS
// ENCODING IS EASY TO GET WRONG. `PWT` is bit 3, `PCD` is bit 4, and `PAT` is
// bit 7 — for a 4KiB entry. In a 2MiB or 1GiB entry the `PAT` bit moves to bit
// 12, because bit 7 is `PS` there. This encoder builds 4KiB leaves only, which
// is what the interface offers.
namespace x86_64 {

inline constexpr arch_u64 kP   = 1ULL << 0;   // present
inline constexpr arch_u64 kRw  = 1ULL << 1;   // writable
inline constexpr arch_u64 kUs  = 1ULL << 2;   // reachable from user mode
inline constexpr arch_u64 kPwt = 1ULL << 3;   // PAT index bit 0
inline constexpr arch_u64 kPcd = 1ULL << 4;   // PAT index bit 1
inline constexpr arch_u64 kA   = 1ULL << 5;   // accessed
inline constexpr arch_u64 kD   = 1ULL << 6;   // dirty
inline constexpr arch_u64 kPat = 1ULL << 7;   // PAT index bit 2 (4KiB leaf)
inline constexpr arch_u64 kG   = 1ULL << 8;   // global
inline constexpr arch_u64 kNx  = 1ULL << 63;  // no-execute

// The indices into the layout `install_memory_attributes` writes:
//
//   index 0   0x06  write-back            → PAT=0 PCD=0 PWT=0, i.e. no bits
//   index 1   0x00  uncacheable           → PAT=0 PCD=0 PWT=1
//
// ⚠️ INDEX 1 IS WRITE-THROUGH AT RESET, NOT UNCACHEABLE. The reset value of
// `IA32_PAT` is 0x0007040600070406, whose entry 1 is `WT`. An encoder relying
// on reset values would map `device` to write-through memory: writes reach
// memory eventually rather than immediately, and a device register written that
// way is written at a time the program did not choose. Nothing faults.
//
// This is exactly why the register is programmed rather than assumed — and it
// is a stronger reason than aarch64's, where an unwritten `MAIR_EL1` field is
// zero and reads as the STRICTEST type, so the failure degrades safely. Here it
// does not.
inline constexpr arch_u64 kAttrNormal = 0ULL;
inline constexpr arch_u64 kAttrDevice = kPwt;

// The physical address occupies [51:12]. Bits [62:52] are software-available
// and the MMU ignores them; masking them off keeps `entry_phys` honest.
inline constexpr arch_u64 kAddrMask = ((1ULL << 40) - 1) << 12;

inline arch_u64 encode_leaf(arch_u64 phys, int perm, int mt,
                                 bool user) noexcept {
    arch_u64 e = kP | kA;

    const bool writable   = (perm == 1 || perm == 3);
    const bool executable = (perm == 2 || perm == 3);

    if (writable) e |= kRw | kD;
    if (user)     e |= kUs;

    // ⚠️ NO PER-LEVEL EXECUTE CONTROL, WHICH IS THE THIRD DISAGREEMENT AND THE
    // ONLY ONE THIS LAYER CANNOT HIDE IN THE ENTRY.
    //
    // riscv qualifies `X` by `U`; aarch64 has separate `PXN` and `UXN` bits.
    // x86_64 has one `NX` bit that applies to every privilege level, so the
    // rule the other two encoders enforce — a user mapping is never
    // kernel-executable — is not expressible here. It is enforced instead by
    // `SMEP`, a bit in `CR4` that faults when privileged code fetches from a
    // user page, which `install_memory_attributes` sets when the processor
    // reports it.
    //
    // Stating it is the price of hiding it, the same way the `MAIR_EL1`
    // precondition was.
    if (!executable) e |= kNx;

    if (mt == 1) e |= kAttrDevice;
    else         e |= kAttrNormal;

    return e | (phys & kAddrMask);
}

inline bool entry_valid(arch_u64 bits) noexcept { return (bits & kP) != 0; }

inline arch_u64 entry_phys(arch_u64 bits) noexcept {
    if (!entry_valid(bits)) return 0;
    return bits & kAddrMask;
}

}  // namespace x86_64

}  // namespace arch

#endif
