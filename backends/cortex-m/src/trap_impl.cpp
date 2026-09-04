// openarch.trap — the Cortex-M backend.
//
// ⚠️⚠️ THE VECTOR TABLE IS AN ARRAY INDEXED BY EXCEPTION NUMBER, NOT A SINGLE
// ENTRY POINT, AND THAT IS THE SEMANTIC CHANGE THIS MACHINE FORCES.
//
// riscv64 has `mtvec`, aarch64 `VBAR`, x86_64 a table of gates the backend
// fills with one stub. On M-profile the hardware reads a function pointer per
// exception from the table at `VTOR`, and it has already stacked r0-r3, r12,
// LR, PC and xPSR by the time the handler runs.
//
// So "install one handler" means: point the slots this layer cares about at one
// trampoline. The board owns the table itself — where it lives and what fills
// the vendor interrupt slots are board facts — which is why this file installs
// nothing and instead exports the trampoline for a board's table to name.
#include <openarch/abi.h>

namespace {
arch_trap_handler_fn g_handler = nullptr;
}

extern "C" {

arch_trap_handler_fn arch_trap_set_handler(arch_trap_handler_fn h) {
    auto prev = g_handler;
    g_handler = h;
    return prev;
}

// ⚠️ PRIMASK, NOT BASEPRI. `cpsid i` masks every configurable interrupt and is
// present on every M-profile core; `BASEPRI` exists only from v7-M up and masks
// by priority. A kernel that wants priority-based masking is doing scheduling
// policy, which this layer does not own.
void arch_trap_enable_interrupts(int on) {
    if (on) asm volatile("cpsie i" ::: "memory");
    else    asm volatile("cpsid i" ::: "memory");
}

int arch_trap_interrupts_enabled(void) {
    unsigned primask;
    asm volatile("mrs %0, primask" : "=r"(primask));
    return primask == 0;   // PRIMASK set == interrupts masked
}

// The trampoline a board's vector table names. It normalises what the machine
// reports into the frame every handler on every architecture reads.
//
// ⭐ `pc` IS THE STACKED RETURN ADDRESS, WHICH IS WHAT THE OTHER BACKENDS MEAN.
// The hardware pushed it at offset 24 of the exception frame; on a fault it is
// the faulting instruction, and on an SVC or PendSV it is the one after — the
// same fault/trap distinction x86_64 normalises, resolved the same way.
__attribute__((used)) void openarch_cm_trap_entry(unsigned* frame,
                                                  unsigned exc_number) {
    if (!g_handler) return;
    arch_trap_frame f{};
    f.pc        = frame ? frame[6] : 0;      // stacked PC
    f.addr      = 0;
    f.cause     = exc_number;
    f.instr_len = 2;                         // Thumb: `bkpt` is two bytes
    switch (exc_number) {
        case 3:  f.kind = ARCH_TRAP_ILLEGAL;   break;  // HardFault
        case 4:  f.kind = ARCH_TRAP_PAGE_FAULT; break; // MemManage (MPU)
        case 5:  f.kind = ARCH_TRAP_OTHER;     break;  // BusFault
        case 6:  f.kind = ARCH_TRAP_ILLEGAL;   break;  // UsageFault
        case 11: f.kind = ARCH_TRAP_BREAKPOINT; break; // SVCall
        default: f.kind = (exc_number >= 16) ? ARCH_TRAP_INTERRUPT
                                             : ARCH_TRAP_OTHER; break;
    }
    g_handler(&f);
}

}  // extern "C"
