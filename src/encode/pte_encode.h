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

namespace arch {

// ── riscv64: the Sv39/Sv48 leaf, per the privileged specification ───────────
//
// The format is the same for both page-table depths; only how many levels are
// walked differs, and that is the walker's business rather than the entry's.
namespace riscv64 {

inline constexpr unsigned long kV = 1UL << 0;   // valid
inline constexpr unsigned long kR = 1UL << 1;   // readable
inline constexpr unsigned long kW = 1UL << 2;   // writable
inline constexpr unsigned long kX = 1UL << 3;   // executable
inline constexpr unsigned long kU = 1UL << 4;   // reachable from user mode
inline constexpr unsigned long kA = 1UL << 6;   // accessed
inline constexpr unsigned long kD = 1UL << 7;   // dirty

// ⚠️ Svpbmt, bits [62:61]: 0 = PMA (whatever the platform says the region is),
// 1 = NC (non-cacheable, idempotent), 2 = IO (non-cacheable, non-idempotent).
// Device memory is IO rather than NC: reading a device register can have an
// effect, which is exactly what "non-idempotent" names.
//
// ⭐ THE ENTIRE MEMORY TYPE IS HERE, IN THE ENTRY. That is what the aarch64
// encoder below cannot reproduce, and the reason `install_memory_attributes`
// exists in the interface at all.
inline constexpr unsigned long kPbmtIo = 2UL << 61;

// The physical page number occupies [53:10].
inline constexpr unsigned long kPpnShift = 10;
inline constexpr unsigned long kPpnMask  = ((1UL << 44) - 1) << kPpnShift;

inline unsigned long encode_leaf(unsigned long phys, int perm, int mt,
                                 bool user) noexcept {
    unsigned long e = kV | kA | kR;

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

inline bool entry_valid(unsigned long bits) noexcept { return (bits & kV) != 0; }

inline unsigned long entry_phys(unsigned long bits) noexcept {
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
inline constexpr unsigned long kValidPage = 3UL;

// AttrIndx, bits [4:2], indexing the layout install_memory_attributes writes:
//
//   index 0  0xFF  Normal, inner and outer write-back, read/write-allocate
//   index 1  0x00  Device-nGnRnE
//
// Two entries and not eight, because the interface distinguishes two kinds of
// memory. An unused MAIR field is zero, which reads as Device-nGnRnE — the
// strictest type — so a mis-encoded index degrades to "slow and correct" rather
// than to "cached device register".
inline constexpr unsigned long kAttrNormal = 0UL << 2;
inline constexpr unsigned long kAttrDevice = 1UL << 2;

// AP, bits [7:6]. ⚠️ aarch64 states permission as a PAIR of levels rather than
// as one set of bits per level, which is the second place the two machines
// disagree: riscv has a single `U` bit orthogonal to R/W/X.
inline constexpr unsigned long kApRwEl1    = 0UL << 6;   // read-write, privileged
inline constexpr unsigned long kApRwEl0El1 = 1UL << 6;   // read-write, both
inline constexpr unsigned long kApRoEl1    = 2UL << 6;   // read-only, privileged
inline constexpr unsigned long kApRoEl0El1 = 3UL << 6;   // read-only, both

// SH, bits [9:8]. Inner shareable for normal memory; device memory ignores the
// field, and zero is what the manual's examples leave there.
inline constexpr unsigned long kShInner = 3UL << 8;

inline constexpr unsigned long kAf  = 1UL << 10;   // access flag
inline constexpr unsigned long kPxn = 1UL << 53;   // privileged execute never
inline constexpr unsigned long kUxn = 1UL << 54;   // unprivileged execute never

// The output address occupies [47:12].
inline constexpr unsigned long kOaMask = ((1UL << 36) - 1) << 12;

inline unsigned long encode_leaf(unsigned long phys, int perm, int mt,
                                 bool user) noexcept {
    unsigned long e = kValidPage | kAf;

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

inline bool entry_valid(unsigned long bits) noexcept { return (bits & 1UL) != 0; }

inline unsigned long entry_phys(unsigned long bits) noexcept {
    if (!entry_valid(bits)) return 0;
    return bits & kOaMask;
}

}  // namespace aarch64

}  // namespace arch

#endif
