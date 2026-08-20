// openarch.trap — the aarch64 backend.

namespace {

// ESR_EL1's exception class occupies bits [31:26].
constexpr unsigned long long kEcShift = 26;
constexpr unsigned long long kEcMask  = 0x3FULL;

constexpr unsigned long long kEcUnknown       = 0x00;
constexpr unsigned long long kEcIllegalState  = 0x0E;
constexpr unsigned long long kEcPcAlignment   = 0x22;
constexpr unsigned long long kEcSpAlignment   = 0x26;
constexpr unsigned long long kEcInstrAbortLo  = 0x20;
constexpr unsigned long long kEcInstrAbortEq  = 0x21;
constexpr unsigned long long kEcDataAbortLo   = 0x24;
constexpr unsigned long long kEcDataAbortEq   = 0x25;
constexpr unsigned long long kEcBrk           = 0x3C;

inline unsigned long long read_esr() noexcept {
    unsigned long long v; asm volatile("mrs %0, esr_el1" : "=r"(v)); return v;
}
inline unsigned long long read_elr() noexcept {
    unsigned long long v; asm volatile("mrs %0, elr_el1" : "=r"(v)); return v;
}
inline void write_elr(unsigned long long v) noexcept {
    asm volatile("msr elr_el1, %0" :: "r"(v));
}
inline unsigned long long read_far() noexcept {
    unsigned long long v; asm volatile("mrs %0, far_el1" : "=r"(v)); return v;
}

}  // namespace

#include <openarch/abi.h>

extern "C" {
extern unsigned char arch_vector_table[];   // the 2 KiB table in trap.S
}

namespace {

arch_trap_handler_fn g_handler = nullptr;

// ⭐ THE CLASSIFICATION READS TWO SOURCES, AND riscv NEEDS ONLY ONE.
//
// Which slot ran says whether the exception was synchronous, an IRQ, an FIQ or
// an SError — the low two bits of the index — and that information exists
// nowhere else once the slot has branched. `ESR_EL1` then says what a
// synchronous exception actually was. A backend that read only `ESR_EL1` would
// classify every interrupt as whatever `ESR_EL1` happened to hold from the last
// synchronous trap, which is the kind of defect that works until it does not.
int classify(unsigned long long slot, unsigned long long esr) noexcept {
        switch (slot & 3ULL) {
        case 1: case 2: return 4;   // IRQ, FIQ
        case 3:         return 5;       // SError: asynchronous, and not
                                               // an interrupt a handler masks
        default:        break;                 // synchronous; ask ESR_EL1
    }
    switch ((esr >> kEcShift) & kEcMask) {
        case kEcBrk:                                        return 0;
        case kEcUnknown:
        case kEcIllegalState:                               return 2;
        case kEcPcAlignment:
        case kEcSpAlignment:                                return 3;
        case kEcInstrAbortLo: case kEcInstrAbortEq:
        case kEcDataAbortLo:  case kEcDataAbortEq:          return 1;
        default:                                            return 5;
    }
}

}  // namespace

// Called by the common path in trap.S. `slot` is the vector index the hardware
// selected, which no register records.
extern "C" void arch_trap_dispatch(arch_trap_frame* f,
                                   unsigned long long slot) {
    static_assert(sizeof(arch_trap_frame) == 32,
                  "trap.S reserves 32 bytes above the saved registers");

    const auto esr = read_esr();
    // ⚠️ `cause` carries the slot as well as ESR_EL1, because reporting only
    // ESR would discard the half of the classification that is not in it. The
    // slot goes in the high bits, where an ESR value cannot reach: ESR_EL1's
    // defined fields stop at bit 31.
    f->cause = esr | (slot << 32);
    f->pc    = read_elr();
    f->kind  = classify(slot, esr);
    f->addr  = (f->kind == 1
                || f->kind == 3) ? read_far() : 0;

    // Always four. aarch64 has one instruction width, which is why this field
    // looked unnecessary until riscv showed otherwise.
    f->instr_len = 4;

    if (g_handler) g_handler(f);

    write_elr(f->pc);
}


extern "C" arch_trap_handler_fn arch_trap_set_handler(arch_trap_handler_fn h) {
    const auto prev = g_handler;
    g_handler = h;
    const auto base = reinterpret_cast<unsigned long long>(arch_vector_table);
    // ⚠️ `isb` after the write. The barrier is what makes the new table apply
    // to exceptions that follow; without it an exception taken immediately
    // afterwards may still use the previous base, which at boot is architecturally
    // UNKNOWN. Same reasoning as the `isb` after `MAIR_EL1` in openarch.pte.
    asm volatile("msr vbar_el1, %0\n\tisb" :: "r"(base) : "memory");
    return prev;
}

extern "C" void arch_trap_enable_interrupts(int on) {
    // DAIF bit 1 (I) masks IRQ; `msr daifclr` clears mask bits, `daifset` sets
    // them. The sense is inverted relative to riscv's `mstatus.MIE`, which is
    // an enable rather than a mask — one more place the two machines say the
    // same thing in opposite words.
    if (on) asm volatile("msr daifclr, #2" ::: "memory");
    else    asm volatile("msr daifset, #2" ::: "memory");
}

extern "C" int arch_trap_interrupts_enabled(void) {
    unsigned long long v;
    asm volatile("mrs %0, daif" : "=r"(v));
    return (v & (1ULL << 7)) == 0 ? 1 : 0;   // I bit clear means enabled
}

