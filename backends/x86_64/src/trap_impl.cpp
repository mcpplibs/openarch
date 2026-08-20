// Traps, on x86_64: building the table and reading what the machine reports.
//
// ⭐ THE THIRD MACHINE CONTRADICTED SOMETHING THE FIRST TWO AGREED ON, AND THE
// INTERFACE HAD TO ABSORB IT RATHER THAN RESTATE IT.
//
// `openarch.trap` documents that `pc` is "the instruction that trapped, not the
// one after it — both machines report it that way". That was true of riscv64
// and aarch64 and it is not true here. x86_64 divides its exceptions into
// FAULTS, which report the address of the instruction that failed, and TRAPS,
// which report the address of the NEXT one. `int3` — the breakpoint the
// interface's `instr_len` exists to step over — is a trap, so the processor
// reports `RIP` already past it.
//
// Two readings were available. Change the interface to say "pc means whatever
// the machine reports", which pushes the difference into every handler that
// will ever be written including the ones that only run on RISC machines; or
// normalise here, so `pc` means one thing everywhere. The second is what a
// machine-abstraction layer is for, and it is what this file does: for a
// trap-class vector the reported `RIP` is walked back over the instruction, and
// `instr_len` is set so that `pc + instr_len` resumes where the processor would
// have.
//
// ⚠️ ONE ARCHITECTURE CANNOT REVEAL THIS. It is invisible with riscv alone and
// invisible with riscv and aarch64 together, because both are RISC machines
// that report faults and traps identically. It is the same shape of finding as
// `instr_len` itself, which `rv64gc`'s two-byte `c.ebreak` forced and which
// aarch64 could never have shown.
#include <openarch/abi.h>

extern "C" const arch_u64 arch_trap_stub_table[256];

namespace {

arch_trap_handler_fn g_handler = nullptr;

// ── The interrupt descriptor table ─────────────────────────────────────────
//
// ⚠️ SIXTEEN BYTES PER GATE, WITH THE HANDLER'S ADDRESS SPLIT ACROSS THREE
// NON-ADJACENT FIELDS. The layout is historical: a 32-bit gate had two
// 16-bit halves, and 64-bit mode appended the high 32 bits after the fields
// that used to follow them. Writing it as one 64-bit member would assemble
// cleanly and produce a table the processor reads as garbage.
struct [[gnu::packed]] Gate {
    arch_u32 offset_low  : 16;
    arch_u32 selector    : 16;
    arch_u32 ist         : 3;    // 0 = use the current stack
    arch_u32 reserved0   : 5;
    arch_u32 type_attr   : 8;
    arch_u32 offset_mid  : 16;
    arch_u32 offset_high;
    arch_u32 reserved1;
};

static_assert(sizeof(Gate) == 16, "an x86_64 IDT gate is sixteen bytes");

struct [[gnu::packed]] Idtr {
    arch_u32 limit : 16;
    arch_u64 base;
};

static_assert(sizeof(Idtr) == 10, "lidt reads a 16-bit limit and a 64-bit base");

// ⚠️ `alignas(16)` IS NOT REQUIRED BY THE ARCHITECTURE AND IS KEPT ANYWAY.
// The manual recommends it so that no gate straddles a cache line, which
// matters for the exception path's latency rather than its correctness.
alignas(16) Gate g_idt[256];

// 0x8E: present, descriptor privilege level 0, type 0xE — a 64-bit INTERRUPT
// gate.
//
// ⚠️ AN INTERRUPT GATE AND NOT A TRAP GATE, AND THE DIFFERENCE IS THE ONE
// PROMISE `openarch.trap` MAKES ABOUT HANDLERS. A trap gate (type 0xF) leaves
// `RFLAGS.IF` as it was, so a handler installed through one can be interrupted
// by the very source it is servicing. An interrupt gate clears `IF` on entry,
// which is what "a handler runs with traps disabled" means. The two differ by
// one bit and nothing reports the choice.
constexpr arch_u32 kInterruptGate64 = 0x8Eu;

arch_u32 current_cs() noexcept {
    // ⚠️ READ RATHER THAN ASSUMED. A gate names a code segment by selector, and
    // 0x08 is only the conventional first GDT entry. This backend does not own
    // the GDT — the loader that reached long mode does — so it asks the
    // processor which selector is in use instead of asserting one. A wrong
    // selector produces #GP on the first exception, from inside the exception
    // path, which is the least diagnosable failure this file could have.
    arch_u32 cs;
    asm volatile("movl %%cs, %0" : "=r"(cs));
    return cs;
}

void set_gate(int vector, arch_u64 handler, arch_u32 cs) noexcept {
    Gate& g       = g_idt[vector];
    g.offset_low  = static_cast<arch_u32>(handler)         & 0xFFFFu;
    g.selector    = cs;
    g.ist         = 0;
    g.reserved0   = 0;
    g.type_attr   = kInterruptGate64;
    g.offset_mid  = static_cast<arch_u32>(handler >> 16)   & 0xFFFFu;
    g.offset_high = static_cast<arch_u32>(handler >> 32);
    g.reserved1   = 0;
}

// ── What the machine stopped for ───────────────────────────────────────────
//
// ⚠️ THE VECTOR IS THE ONLY CAUSE THIS MACHINE REPORTS. riscv has `scause` and
// aarch64 has `ESR_EL1`, both of which encode a class and a reason; here the
// entry point IS the reason, which is why there are 256 of them.
constexpr int kVecDebug         = 1;
constexpr int kVecBreakpoint    = 3;
constexpr int kVecOverflow      = 4;
constexpr int kVecInvalidOpcode = 6;
constexpr int kVecAlignCheck    = 17;
constexpr int kVecPageFault     = 14;
constexpr int kVecFirstExternal = 32;

int classify(arch_u64 vector) noexcept {
    if (vector >= kVecFirstExternal) return ARCH_TRAP_INTERRUPT;
    switch (vector) {
        case kVecBreakpoint:
        case kVecDebug:          return ARCH_TRAP_BREAKPOINT;
        case kVecInvalidOpcode:  return ARCH_TRAP_ILLEGAL;
        case kVecAlignCheck:     return ARCH_TRAP_UNALIGNED;
        case kVecPageFault:      return ARCH_TRAP_PAGE_FAULT;
        default:                 return ARCH_TRAP_OTHER;
    }
}

// The vectors whose reported `RIP` is the instruction AFTER the one that
// trapped. Everything else is a fault or an external interrupt, both of which
// report the instruction itself.
//
// ⚠️ VECTOR 1 IS IN BOTH SETS DEPENDING ON WHY IT WAS RAISED — an instruction
// breakpoint is a fault and a single-step is a trap — and this backend treats
// it as a fault, which is the safe direction. Walking `pc` back for a fault
// would point it into the middle of the preceding instruction; not walking it
// back for a trap makes a handler that resumes re-execute one instruction. The
// interface's own contract covers neither, and `cause` carries the vector so a
// kernel doing single-step debugging can read it and decide.
bool reports_next_instruction(arch_u64 vector) noexcept {
    return vector == kVecBreakpoint || vector == kVecOverflow;
}

arch_u64 read_cr2() noexcept {
    arch_u64 v;
    asm volatile("movq %%cr2, %0" : "=r"(v));
    return v;
}

// The raw stack `arch_trap_common` hands over, in the order the stubs and the
// processor pushed it.
struct Raw {
    arch_u64 vector;
    arch_u64 error_code;
    arch_u64 rip;
    arch_u64 cs;
    arch_u64 rflags;
    arch_u64 rsp;
    arch_u64 ss;
};

}  // namespace

extern "C" void arch_trap_dispatch(arch_trap_frame* f, Raw* raw) {
    static_assert(sizeof(arch_trap_frame) == 32,
                  "trap.S reserves 32 bytes below the saved registers");

    f->cause = raw->vector;
    f->kind  = classify(raw->vector);
    f->addr  = (raw->vector == kVecPageFault) ? read_cr2() : 0;

    if (reports_next_instruction(raw->vector)) {
        // ⚠️ ONE BYTE, BECAUSE THE ONLY BREAKPOINT THIS CAN BE IS `0xCC`.
        //
        // `int3` has two encodings: the one-byte `CC`, which every compiler and
        // debugger emits, and the two-byte `CD 03` (`int $3`), which is
        // essentially only written by hand. Distinguishing them means reading
        // the bytes before `RIP` and deciding which of two overlapping
        // encodings ended there — a decode, and a decode that can be fooled by
        // data preceding the instruction.
        //
        // So the common encoding is assumed and the assumption is stated. A
        // program that raises a breakpoint with `CD 03` gets a `pc` one byte
        // into its own instruction; that is a defect, and it is a defect in a
        // place a reader can find rather than an ambiguity spread over the
        // handler.
        f->instr_len = 1;
        f->pc        = raw->rip - f->instr_len;
    } else {
        // ⚠️ ZERO, AND IT IS A STATEMENT RATHER THAN A DEFAULT. On riscv the
        // backend reads two bytes at `pc` and knows the length from the low
        // bits, because RISC-V encodes it there. x86_64 instructions are one to
        // fifteen bytes and their length is only knowable by decoding, which
        // needs a disassembler this layer will not carry. A handler that means
        // to step past a fault on this machine has to decode it itself, and
        // reporting zero says so rather than offering a number that is wrong.
        f->instr_len = 0;
        f->pc        = raw->rip;
    }

    if (g_handler) g_handler(f);

    // What the handler left in `pc` is where execution resumes, which is the
    // same contract the other two backends have — they write `mepc` and
    // `ELR_EL1`, and this one writes the `iret` frame.
    raw->rip = f->pc;
}

extern "C" arch_trap_handler_fn arch_trap_set_handler(arch_trap_handler_fn h) {
    const auto prev = g_handler;
    g_handler = h;

    const arch_u32 cs = current_cs();
    for (int v = 0; v < 256; ++v) set_gate(v, arch_trap_stub_table[v], cs);

    const Idtr idtr{ sizeof(g_idt) - 1, reinterpret_cast<arch_u64>(&g_idt[0]) };
    asm volatile("lidt %0" :: "m"(idtr) : "memory");

    return prev;
}

extern "C" void arch_trap_enable_interrupts(int on) {
    if (on) asm volatile("sti" ::: "memory");
    else    asm volatile("cli" ::: "memory");
}

extern "C" int arch_trap_interrupts_enabled(void) {
    arch_u64 flags;
    asm volatile("pushfq\n\tpopq %0" : "=r"(flags));
    return (flags & (1ULL << 9)) != 0 ? 1 : 0;   // RFLAGS.IF
}
