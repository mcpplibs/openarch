// openarch.trap — taking control when the machine takes it away.
//
// WHY THIS IS THE THIRD INTERFACE AND NOT THE FIRST
//
// Context switching asked whether an abstraction can hide a register file. The
// page-table entry asked whether it can hide a disagreement about where a fact
// lives. This one asks whether it can hide a disagreement about the SHAPE of an
// event, and the two machines disagree about that more than about anything
// else in this layer.
//
// ⭐ WHAT THE TWO MACHINES ACTUALLY DO, MEASURED AGAINST THE MANUALS RATHER
// THAN ASSUMED.
//
// riscv64 has ONE entry point. `stvec` holds an address; every trap goes there;
// `scause` says what happened, with its top bit distinguishing an interrupt
// from an exception and the rest naming the cause. The handler reads a register
// to learn what it was called for.
//
// aarch64 has SIXTEEN. `VBAR_EL1` holds the base of a 2 KiB table of sixteen
// 128-byte slots, and the hardware selects a slot from the exception's class
// (synchronous, IRQ, FIQ, SError) crossed with where it came from (the current
// level with SP0, the current level with SPx, a lower level in AArch64, a lower
// level in AArch32). The handler learns part of what it was called for from
// WHICH slot ran, and the rest from `ESR_EL1`.
//
// So the abstraction has to hide a structural difference, not a naming one:
// "install a handler" is one register write on one machine and the
// construction of a dispatch table on the other.
//
// ⚠️ THIS LAYER OWNS THE VECTOR TABLE ON aarch64, FOR THE SAME REASON IT OWNS
// `MAIR_EL1` IN `openarch.pte`. A caller that had to supply sixteen slots would
// be writing aarch64 into every kernel that uses this interface, including the
// ones that will never run on it. The table this layer installs sends all
// sixteen slots to one stub, which is what makes `handler` mean the same thing
// on both machines. A kernel that wants per-slot dispatch is asking for
// something this interface does not offer, and should install its own
// `VBAR_EL1`.

export module openarch.trap;

export namespace arch {

// What the machine stopped for.
//
// ⚠️ SIX VALUES, AND THE SET IS THE INTERSECTION RATHER THAN THE UNION. Each
// one has to mean the same thing on both machines; anything that does not is
// `other`, whose numeric cause the caller can still read. riscv distinguishes
// load faults from store faults and aarch64 does not report the direction in
// the same field, so `page_fault` covers both and `addr` carries the address.
enum class trap_kind {
    breakpoint,      // a deliberate trap instruction
    page_fault,      // a translation or permission failure
    illegal,         // an instruction the machine will not execute
    unaligned,       // an access the machine will not perform
    interrupt,       // asynchronous; `cause` names the source
    other,           // everything neither machine agrees about
};

// What the handler is told.
//
// `pc` is the instruction that trapped, not the one after it — both machines
// report it that way, and a handler that intends to resume past a `breakpoint`
// must advance it itself, because how far to advance is the instruction set's
// business rather than this layer's.
//
// `addr` is meaningful for `page_fault` and `unaligned` and is zero otherwise.
// `cause` is the architecture's own code, unmapped: a kernel that needs the
// distinction this interface deliberately does not draw can still read it, and
// a kernel that does not need it never has to.
// ⭐ `instr_len` IS HERE BECAUSE THE SECOND ARCHITECTURE PROVED IT HAD TO BE.
//
// A handler that intends to resume past a breakpoint must advance `pc`, and
// how far is not something a portable handler can know. The first version of
// this interface left it out, on the reading that both machines use four-byte
// instructions. Measured on `rv64gc`:
//
//     cause:3 desc=breakpoint          epc:0x800001f2
//     cause:2 desc=illegal_instruction epc:0x800001f6   ← forever
//
// `0x800001f2` is not four-byte aligned. The C extension is part of `rv64gc`,
// so the assembler emitted the two-byte `c.ebreak`, and a handler advancing by
// four landed inside the following instruction. aarch64 has one instruction
// width and could never have shown this.
//
// The backend knows the answer and the caller cannot derive it, which is the
// definition of something belonging in this frame.
struct trap_frame {
    unsigned long long pc;
    unsigned long long addr;
    unsigned long long cause;
    trap_kind          kind;
    unsigned           instr_len;   // bytes; 2 or 4 on riscv, always 4 on aarch64
};

// A handler runs with traps disabled and must return.
//
// ⚠️ Returning resumes the trapped context, which for a fault the handler did
// not repair means trapping again at the same instruction. That is the honest
// behaviour: this layer has no policy for an unhandled fault, and inventing one
// — halting, printing, rebooting — would be a kernel's decision made in the
// wrong place.
using trap_handler = void (*)(trap_frame&) noexcept;

// Installs `h` as the machine's trap entry and returns the previous handler, or
// null if none had been installed through this interface.
//
// On riscv64 this writes `stvec`. On aarch64 it writes `VBAR_EL1` with the
// address of a table this layer owns. Both take effect immediately; neither
// enables interrupts, which is `enable_interrupts` below.
trap_handler set_handler(trap_handler h) noexcept;

// Whether asynchronous traps are delivered.
//
// Separate from `set_handler` because the order matters and only one order is
// safe: install first, then enable. An interface that did both would make the
// unsafe order unexpressible, which sounds like a service until a kernel needs
// to install a handler while interrupts are already on.
void enable_interrupts(bool on) noexcept;
bool interrupts_enabled() noexcept;

}  // namespace arch
