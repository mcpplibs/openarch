// openarch.trap — the riscv64 backend.
//
// The stub in trap.S saves registers and calls `arch_trap_dispatch`; everything
// that can be C++ is here, so the classification is readable once rather than
// encoded twice.

// ⚠️ THE FRAME LAYOUT IS SHARED WITH ASSEMBLY AND IS THEREFORE ASSERTED.
// trap.S hands a pointer to 32 bytes above the saved registers. A silent
// disagreement about the size would corrupt the saved `a0` immediately below
// it, which is the register the handler's argument arrives in — a fault whose
// symptom is arbitrarily far from its cause.
// ⚠️ `openarch/abi.h` AT THE TOP, NOT BELOW THE HELPERS. The register readers
// under it are written in `arch_u64`, and until 0.4.0 they were written in a
// builtin type that needed no header — so the include sat wherever it was first
// needed, which was after them.
#include <openarch/abi.h>

namespace {

constexpr arch_u64 kCauseInterrupt = 1ULL << 63;

// The exception codes this backend maps. Everything else reaches `other` with
// its numeric cause intact, which is the honest answer for a machine-specific
// event rather than a guess at the nearest portable name.
constexpr arch_u64 kInstrMisaligned = 0;
constexpr arch_u64 kIllegalInstr    = 2;
constexpr arch_u64 kBreakpoint      = 3;
constexpr arch_u64 kLoadMisaligned  = 4;
constexpr arch_u64 kStoreMisaligned = 6;
constexpr arch_u64 kInstrPageFault  = 12;
constexpr arch_u64 kLoadPageFault   = 13;
constexpr arch_u64 kStorePageFault  = 15;

inline arch_u64 read_mcause() noexcept {
    arch_u64 v; asm volatile("csrr %0, mcause" : "=r"(v)); return v;
}
inline arch_u64 read_mepc() noexcept {
    arch_u64 v; asm volatile("csrr %0, mepc" : "=r"(v)); return v;
}
inline void write_mepc(arch_u64 v) noexcept {
    asm volatile("csrw mepc, %0" :: "r"(v));
}
inline arch_u64 read_mtval() noexcept {
    arch_u64 v; asm volatile("csrr %0, mtval" : "=r"(v)); return v;
}

}  // namespace


extern "C" void arch_trap_entry();   // the stub in trap.S

namespace {

arch_trap_handler_fn g_handler = nullptr;

int classify(arch_u64 cause) noexcept {
        if (cause & kCauseInterrupt) return 4;
    switch (cause) {
        case kBreakpoint:                                   return 0;
        case kIllegalInstr:                                 return 2;
        case kInstrMisaligned:
        case kLoadMisaligned:
        case kStoreMisaligned:                              return 3;
        case kInstrPageFault:
        case kLoadPageFault:
        case kStorePageFault:                               return 1;
        default:                                            return 5;
    }
}

}  // namespace

// Called by trap.S with a pointer to 32 bytes of stack for the frame.
extern "C" void arch_trap_dispatch(arch_trap_frame* f) {
    static_assert(sizeof(arch_trap_frame) == 32,
                  "trap.S reserves 32 bytes above the saved registers");

    const auto cause = read_mcause();
    f->cause = cause;
    f->pc    = read_mepc();
    f->kind  = classify(cause);
    // ⚠️ `mtval` is only meaningful for the faults that produce an address.
    // Reporting it unconditionally would hand a caller the residue of an
    // earlier trap and let it look like an address.
    f->addr  = (f->kind == 1
                || f->kind == 3) ? read_mtval() : 0;

    // ⚠️ The length is read from the instruction itself, not assumed. riscv
    // encodes it in the low bits: anything other than 0b11 there is a two-byte
    // compressed instruction, and `rv64gc` includes the C extension, so the
    // assembler emits `c.ebreak` rather than `ebreak` unless told otherwise.
    // Reading the halfword is safe here — the machine has just executed it.
    {
        const auto half = *reinterpret_cast<const unsigned short*>(f->pc);
        f->instr_len = ((half & 0x3u) == 0x3u) ? 4u : 2u;
    }

    if (g_handler) g_handler(f);

    // The handler may have advanced `pc` — past a breakpoint, typically — and
    // resuming has to honour that. Writing it back unconditionally is simpler
    // than asking whether it changed, and identical when it did not.
    write_mepc(f->pc);
}


extern "C" arch_trap_handler_fn arch_trap_set_handler(arch_trap_handler_fn h) {
    const auto prev = g_handler;
    g_handler = h;
    // ⚠️ Direct mode, not vectored. `mtvec`'s low two bits select the mode, and
    // vectored mode would scale interrupt causes into separate entries — which
    // is the aarch64 arrangement, and is exactly what this interface hides. One
    // entry point on both machines is what makes `handler` mean one thing.
    const auto vec = reinterpret_cast<arch_u64>(&arch_trap_entry);
    asm volatile("csrw mtvec, %0" :: "r"(vec & ~3ULL) : "memory");
    return prev;
}

extern "C" void arch_trap_enable_interrupts(int on) {
    constexpr arch_u64 kMie = 1ULL << 3;   // mstatus.MIE
    if (on) asm volatile("csrs mstatus, %0" :: "r"(kMie) : "memory");
    else    asm volatile("csrc mstatus, %0" :: "r"(kMie) : "memory");
}

extern "C" int arch_trap_interrupts_enabled(void) {
    arch_u64 v;
    asm volatile("csrr %0, mstatus" : "=r"(v));
    return (v & (1ULL << 3)) != 0 ? 1 : 0;
}

