// openarch.trap — the armv7a dispatcher.
//
// The stubs in trap.S hand this the slot index; the fault registers say the
// rest. DFSR and IFSR carry the status of a data and a prefetch abort, DFAR
// and IFAR the address each faulted on.
#include <openarch/abi.h>

namespace {

arch_trap_handler_fn g_handler = nullptr;

inline arch_u32 read_dfsr() noexcept {
    arch_u32 v; asm volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(v)); return v;
}
inline arch_u32 read_ifsr() noexcept {
    arch_u32 v; asm volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(v)); return v;
}
inline arch_u32 read_dfar() noexcept {
    arch_u32 v; asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(v)); return v;
}
inline arch_u32 read_ifar() noexcept {
    arch_u32 v; asm volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(v)); return v;
}

// The slot indices trap.S passes, and the `arch_trap_kind` each maps onto.
// Kind 0 is a breakpoint, 1 a page fault, 2 an illegal instruction, 3 a
// misaligned access, 4 an interrupt, 5 anything else.
int classify(arch_u32 slot, arch_u32 fsr) noexcept {
    switch (slot) {
        case 1: return 2;               // undefined instruction
        case 2: return 0;               // supervisor call — a deliberate trap
        case 6: case 7: return 4;       // IRQ, FIQ
        case 3: case 4: break;          // abort: ask the fault status
        default: return 5;
    }
    // The status field is bits [3:0] with bit 10 as its high bit. Alignment
    // faults are 0b00001; translation faults are 0b00101 and 0b00111. Anything
    // else is reported as itself rather than guessed at.
    const arch_u32 status = (fsr & 0xF) | ((fsr >> 6) & 0x10);
    if (status == 0x01) return 3;                       // alignment
    if (status == 0x05 || status == 0x07) return 1;     // translation
    if (status == 0x03 || status == 0x06) return 1;     // access flag
    if (status == 0x0D || status == 0x0F) return 1;     // permission
    return 5;
}

}  // namespace

extern "C" {

// THIS BACKEND DOES NOT PROVIDE `openarch:preemption`, AND THE MANIFEST SAYS SO
// RATHER THAN THIS FILE PRETENDING.
//
// `arch_trap_switch` asks the trap to resume a DIFFERENT context. On riscv and
// aarch64 the resumption address lives in a register the dispatcher can save
// and restore around the switch; on Cortex-M a dedicated exception performs it.
// Here it lives on the SVC stack, written by `srsdb` — so switching stacks
// mid-trap changes which return frame `rfeia` will pop, and the context being
// resumed must have been suspended through this same path for that to be
// well-defined. That is a real design and it is not one this backend has
// measured, so the capability is withheld at resolution instead of being
// offered and being wrong.
//
// A consumer that requires `openarch:preemption` is refused by name, before
// anything is compiled. That is the same mechanism by which the Cortex-M
// backend withholds `openarch:address-space`.

void arch_trap_common_c(arch_u32 slot);

void arch_trap_common_c(arch_u32 slot) {
    arch_trap_frame f{};
    const arch_u32 fsr = (slot == 3) ? read_ifsr() : read_dfsr();

    f.kind      = classify(slot, fsr);
    f.cause     = static_cast<arch_u64>(fsr) | (static_cast<arch_u64>(slot) << 32);
    f.addr      = (f.kind == 1 || f.kind == 3)
                    ? ((slot == 3) ? read_ifar() : read_dfar())
                    : 0;
    // THE RESUMPTION ADDRESS IS ON THE STACK HERE, NOT IN A REGISTER, so it is
    // reported and not writable: a handler that changes `f.pc` changes nothing
    // on this machine. Saying so is better than accepting the write and
    // discarding it, which is what a field the dispatcher never reads back
    // would do.
    f.pc        = 0;
    f.instr_len = 4;

    if (g_handler) g_handler(&f);
}

arch_trap_handler_fn arch_trap_set_handler(arch_trap_handler_fn h) {
    const auto prev = g_handler;
    g_handler = h;

    extern unsigned char arch_vector_table[];
    const auto base = reinterpret_cast<arch_u32>(arch_vector_table);
    // VBAR, and SCTLR.V cleared so that VBAR is consulted at all: with V set
    // the vectors are at the architectural high address and this register is
    // ignored, which would look exactly like a handler that never fires.
    asm volatile("mcr p15, 0, %0, c12, c0, 0" :: "r"(base) : "memory");
    arch_u32 sctlr;
    asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr &= ~(1U << 13);
    asm volatile("mcr p15, 0, %0, c1, c0, 0\n\tisb" :: "r"(sctlr) : "memory");
    return prev;
}

void arch_trap_enable_interrupts(int on) {
    if (on) asm volatile("cpsie i" ::: "memory");
    else    asm volatile("cpsid i" ::: "memory");
}

int arch_trap_interrupts_enabled(void) {
    arch_u32 cpsr;
    asm volatile("mrs %0, cpsr" : "=r"(cpsr));
    return (cpsr & (1U << 7)) == 0 ? 1 : 0;   // I bit set means masked
}

}  // extern "C"
