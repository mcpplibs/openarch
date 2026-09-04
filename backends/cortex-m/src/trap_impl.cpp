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

// ── Preemption: pending the switch rather than performing it ────────────────
//
// ⭐⭐ THE ONE PLACE IN THIS LAYER WHERE TWO MACHINES DO NOT SHARE A MECHANISM.
//
// riscv64, aarch64 and x86_64 take a trap on the interrupted context's own
// stack, so each performs `arch_trap_switch` as a cooperative switch on the way
// out of its dispatcher and the exception return then finds the new context's
// frame. M-profile cannot: the handler runs on MSP while the task runs on PSP,
// so swapping the handler's registers changes the handler's stack and the
// exception return unstacks a frame belonging to nobody.
//
// Measured, before this backend had the primitive: calling
// `arch_context_switch` from PendSV builds, boots, and reports that neither of
// two tasks ever observed the other. Nothing failed — the tasks were simply
// never interleaved, and only a counter assertion could tell the difference.
//
// What the machine offers instead is exactly the interface's own sentence about
// the two primitives: PendSV pended from thread mode is taken AT ONCE, and
// pended from a handler is taken WHEN THAT HANDLER EXITS. So both functions
// pend it, and the hardware supplies the difference.
extern "C" {

// Read by `arch_context_switch` and by the PendSV stub. Two words, and the
// spelling is an array rather than a struct because the assembly indexes it.
__attribute__((used)) void* openarch_cm_pending[2] = { nullptr, nullptr };

void arch_trap_switch(arch_trap_frame* f, void* from, void* to) {
    (void)f;
    // ⚠️⚠️ THE FIRST `from` WINS AND THE LAST `to` WINS, AND THAT ASYMMETRY IS
    // A FIX RATHER THAN A CHOICE.
    //
    // PendSV is the lowest priority, so it runs only when no handler is active
    // — and TWO ticks can therefore arrive before one switch is performed. With
    // a single slot overwritten by each, the pair that PendSV eventually read
    // named the second request's `from` while the stack pointer it had already
    // taken belonged to the first request's context. It wrote one task's stack
    // pointer into the other task's context, and both were then lost.
    //
    // Measured on `mps2-an385`: `pendsv=2998` switches performed, `p1=0` — the
    // second task never ran at all — and the addresses said why: the two
    // contexts held stack pointers 32 bytes apart, on one stack.
    //
    // The interrupted context is the one the FIRST call in this window saw as
    // current, so that is the `from` to honour; the context to resume is
    // whatever the LAST call asked for. A scheduler that ticked twice is then
    // consistent either way — with two tasks and two ticks the net effect is
    // no switch, which is exactly what it asked for.
    if (!openarch_cm_pending[0]) openarch_cm_pending[0] = from;
    openarch_cm_pending[1] = to;
    // ICSR.PENDSVSET. Taken when this handler returns — tail-chained, so the
    // hardware does not unstack and re-stack the task's frame in between.
    *reinterpret_cast<volatile unsigned*>(0xE000ED04u) = 1u << 28;
}

// Called by the PendSV stub with the outgoing context's saved stack pointer,
// and answering with the incoming one.
//
// ⚠️ AN UNREQUESTED PendSV IS A NO-OP RATHER THAN AN ERROR. A board may pend it
// for its own reasons, and returning the outgoing pointer unchanged saves and
// restores the same context — which is what "nothing was requested" means.
__attribute__((used)) void* openarch_cm_pendsv_pick(void* outgoing) {
    void* from = openarch_cm_pending[0];
    void* to   = openarch_cm_pending[1];
    if (!to) return outgoing;
    openarch_cm_pending[0] = openarch_cm_pending[1] = nullptr;
    if (from) *static_cast<void**>(from) = outgoing;
    return *static_cast<void**>(to);
}

}  // extern "C"
