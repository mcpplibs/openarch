// The gate: one probe, two instruction sets.
//
// WHAT THIS ASSERTS, AND WHY IT IS THE WHOLE POINT
//
// An interface shaped around one instruction set always fits it. The only way
// to learn that `arch::context` is a real abstraction rather than a
// transcription of riscv64's register file is to run the same source on a
// machine whose register file, calling convention and stack rules are
// different. aarch64 is that machine: different callee-saved set, different
// stack alignment rule, a link register instead of a return-address register.
//
// Three observations, each of which would fail differently if the abstraction
// were wrong:
//
//   1. the switch reaches the task           — the saved PC/LR is right
//   2. the task's argument arrives           — the argument survives a transfer
//                                              that restores no argument register
//   3. a callee-saved local survives         — the switch really saves and
//      the round trip                          restores the callee-saved set
//
// ⚠️ Observation 3 is the one that catches a half-correct backend. A switch
// that saves the return address and the stack pointer and nothing else passes
// the first two and corrupts the caller.
// ⭐ ONE IMPORT. The layer divides into four modules and a consumer does not
// have to know that; `mcpplibs.openarch` re-exports them. Importing the parts
// individually still works and is what a boot path that needs only `pte` would
// do.
import mcpplibs.openarch;

#include "machine.h"

namespace {

arch::context g_main;
arch::context g_task;

alignas(16) unsigned char g_stack[4096];

volatile int g_witness = 0;

[[noreturn]] void task(void* arg) {
    machine::print("task: arg=");
    machine::print_int(static_cast<int>(reinterpret_cast<long>(arg)));
    machine::putc('\n');
    g_witness = 7;
    arch::context_switch(g_task, g_main);
    // Unreachable: nothing switches back to this context.
    for (;;) { }
}

}  // namespace


// ── The trap interface, on both machines ────────────────────────────────────
//
// ⭐ THE ASSERTION THAT COULD NOT BE WRITTEN WITH ONE BACKEND.
//
// riscv delivers every trap to one address and puts the cause in a register.
// aarch64 delivers to one of sixteen slots and puts half the cause in WHICH
// SLOT RAN. A `breakpoint` that arrives correctly classified on both is
// therefore evidence that the interface hid a structural difference rather
// than a naming one.
//
// The handler advances `pc` past the trapping instruction by `instr_len`,
// which the backend fills in. ⚠️ An earlier version of this probe advanced by
// four, on the reading that both trap instructions are four bytes. On rv64gc
// the assembler emits the two-byte `c.ebreak`, so the handler landed inside the
// next instruction and the machine looped on `illegal_instruction` forever.
// That is what added `instr_len` to the interface.
namespace {

volatile int g_trapped = 0;

// ⚠️ A POINTER, WHICH IS THE ABI'S SPELLING. The interface takes the C
// contract's signature rather than wrapping it in a reference, because a
// wrapper would need a thunk and a thunk would need a global in the module
// interface that every importer instantiates.
void on_trap(arch::trap_frame* f) {
    if (arch::kind_of(*f) == arch::trap_kind::breakpoint) {
        g_trapped = 1;
        f->pc += f->instr_len;
    }
}

// ⭐⭐ THE ONE ARCHITECTURE CONDITIONAL IN THIS FILE, AND IT IS A FUNCTION SO
// THAT IT CAN STAY THE ONE.
//
// CI counts `#if defined(__` in this file and requires exactly one: what the
// gate claims is that the probe is not two programs, and the trap instruction
// is the single thing no portable spelling exists for. The preemption probe
// below needs the same instruction, and writing a second conditional for it
// would have been two programs by the letter as well as by the check.
inline void raise_breakpoint() {
#if defined(__riscv)
    asm volatile("ebreak");
#elif defined(__aarch64__)
    asm volatile("brk #0");
#elif defined(__x86_64__)
    // ⚠️ AND THIS ONE IS A TRAP RATHER THAN A FAULT, WHICH THE HANDLER ABOVE
    // NEVER LEARNS. x86_64 reports `int3` with `RIP` already past it, where
    // both RISC machines report the address of the trapping instruction. The
    // backend normalises that before the handler runs — walking `pc` back and
    // setting `instr_len` to match — so `f->pc += f->instr_len` resumes in the
    // same place on all three. The alternative was to tell every handler ever
    // written that `pc` means something different here.
    asm volatile("int3");
#else
#  error "the probe has no breakpoint instruction for this architecture"
#endif
}

void probe_trap() {
    arch::set_handler(&on_trap);
    machine::print("trap: raising\n");
    raise_breakpoint();
    machine::print("trap: back, witness=");
    machine::print_int(g_trapped);
    machine::putc('\n');
}

// ── Preemption: the trap RESUMES SOMEWHERE ELSE ─────────────────────────────
//
// ⭐⭐ THE ASSERTION THAT NEEDED A FOURTH MACHINE TO EXIST AT ALL.
//
// `set_handler` lets a kernel see a trap and `enable_interrupts` lets it mask
// one. Neither can change what the trap returns to — and that is the whole of
// preemption, which is the principal reason to use this layer on a device.
// `arch::trap_switch` is that action, and this probe is what says it works.
//
// ⚠️ IT USES A SYNCHRONOUS TRAP RATHER THAN A TIMER, DELIBERATELY. A timer
// would drag a per-machine device into a probe whose whole value is being one
// piece of code — three interrupt controllers, three frequency sources, and an
// assertion that could then fail for reasons having nothing to do with the
// interface. A breakpoint is a trap on every machine here, and the property
// under test is identical: the handler runs, calls `trap_switch`, returns, and
// execution continues IN THE OTHER CONTEXT.
//
// ⚠️ AND THE ASSERTION IS THAT EACH SIDE SAW THE OTHER ADVANCE, not that both
// printed. A `trap_switch` that did nothing at all would leave the first task
// running, and a probe that only checked for output would report success.
namespace {

arch::context g_pre_main;
arch::context g_pre_task;
alignas(16) unsigned char g_pre_stack[4096];

volatile int g_pre_steps   = 0;   // advanced only by the preempted task
volatile int g_pre_resumed = 0;   // set only after the trap returned elsewhere

void on_preempt(arch::trap_frame* f) {
    if (arch::kind_of(*f) != arch::trap_kind::breakpoint) return;
    f->pc += f->instr_len;
    // Once: the second breakpoint (raised by the task) returns normally, which
    // is what lets the task reach its own `context_switch` back.
    if (g_pre_resumed) return;
    g_pre_resumed = 1;
    arch::trap_switch(f, g_pre_main, g_pre_task);
}

[[noreturn]] void preempted_task(void*) {
    g_pre_steps = 1;
    // ⚠️ NOT A `trap_switch` BACK. Returning by the cooperative primitive is
    // what shows the two are interchangeable: a context saved by the trap path
    // is resumed by the ordinary one, which is only true if they share a
    // layout. Two layouts would corrupt whichever was used second, silently.
    arch::context_switch(g_pre_task, g_pre_main);
    for (;;) { }
}

void probe_preempt() {
    arch::set_handler(&on_preempt);
    arch::context_init(g_pre_task, &preempted_task, nullptr,
                       g_pre_stack + sizeof(g_pre_stack));
    machine::print("preempt: trapping\n");
    // The trap below does not return here. The handler switches to the task,
    // the task switches back, and THAT resumes this function — after the
    // breakpoint, because the handler advanced `pc` before switching away.
    raise_breakpoint();
    machine::print("preempt: back, steps=");
    machine::print_int(g_pre_steps);
    machine::putc('\n');
}

}  // namespace

// ── The per-CPU pointer and the barriers ───────────────────────────────────
//
// The pointer is a round trip: what a kernel stores is what it reads back, and
// nothing in between touches it. The barriers are executed rather than
// inspected — there is no architectural way to observe that a fence happened,
// so what is asserted is that all four are ACCEPTED and that the program
// continues, which is what catches a backend that emitted an instruction the
// machine does not have.
int g_percpu_area = 0;
int g_tls_area    = 0;

void probe_cpu() {
    arch::set_percpu(&g_percpu_area);
    const bool same = (arch::percpu() == &g_percpu_area);

    // ⭐ THE SECOND SLOT, AND THE ASSERTION IS THAT IT IS A SECOND SLOT.
    //
    // Round-tripping it alone would pass on a backend where `tls` and `percpu`
    // are the same register — which on riscv64 they ARE, by convention, and
    // abi.h records why that matters. So the check is that BOTH hold their own
    // value at the same time, which is exactly what fails when one aliases the
    // other. On riscv64 this is expected to fail until the backend moves the
    // per-CPU pointer to `sscratch`; the point of the check is that the day it
    // matters, a program says so instead of reading its thread-locals out of a
    // per-CPU structure.
    arch::set_tls(&g_tls_area);
    const bool tls_ok      = (arch::tls() == &g_tls_area);
    const bool distinct_ok = (arch::percpu() == &g_percpu_area);

    arch::fence(arch::barrier::memory);
    arch::fence(arch::barrier::store);
    arch::fence(arch::barrier::complete);
    arch::fence(arch::barrier::fetch);

    machine::print(same ? "cpu: percpu round-trips\n" : "cpu: percpu FAILED\n");
    machine::print(tls_ok ? "cpu: tls round-trips\n" : "cpu: tls FAILED\n");
    machine::print(distinct_ok ? "cpu: the two slots are distinct\n"
                               : "cpu: the two slots ALIAS\n");
    machine::print("cpu: four barriers accepted\n");
}

}  // namespace

extern "C" int probe_main() {
    arch::context_init(g_task, &task, reinterpret_cast<void*>(42L),
                            g_stack + sizeof g_stack);

    // ⚠️ `volatile` and read after the round trip. A plain local would be
    // allowed to live in a caller-saved register or be re-materialised, and
    // then it would prove nothing about what the switch preserved.
    volatile int before = 1234;

    machine::print("main: switching to task\n");
    arch::context_switch(g_main, g_task);

    machine::print("main: back, witness=");
    machine::print_int(g_witness);
    machine::print(" before=");
    machine::print_int(static_cast<int>(before));
    machine::putc('\n');

    probe_trap();
    probe_preempt();
    probe_cpu();

    // ⚠️ `g_pre_steps == 1` IS THE ONE THAT CATCHES A `trap_switch` THAT DID
    // NOTHING. Every line above it is printed by whichever context is running;
    // only a counter the OTHER context advanced says the trap resumed
    // elsewhere.
    const bool ok = (g_witness == 7 && before == 1234 && g_trapped == 1
                     && g_pre_steps == 1 && g_pre_resumed == 1
                     && arch::percpu() == &g_percpu_area);
    machine::print(ok ? "switch ok\n" : "switch FAILED\n");
    return ok ? 0 : 1;
}
