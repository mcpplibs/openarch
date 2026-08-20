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
import openarch.context;
import openarch.trap;
import openarch.cpu;

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
    arch::arch_context_switch(g_task, g_main);
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

void probe_trap() {
    arch::set_handler(&on_trap);
    machine::print("trap: raising\n");
#if defined(__riscv)
    asm volatile("ebreak");
#else
    asm volatile("brk #0");
#endif
    machine::print("trap: back, witness=");
    machine::print_int(g_trapped);
    machine::putc('\n');
}

// ── The per-CPU pointer and the barriers ───────────────────────────────────
//
// The pointer is a round trip: what a kernel stores is what it reads back, and
// nothing in between touches it. The barriers are executed rather than
// inspected — there is no architectural way to observe that a fence happened,
// so what is asserted is that all four are ACCEPTED and that the program
// continues, which is what catches a backend that emitted an instruction the
// machine does not have.
int g_percpu_area = 0;

void probe_cpu() {
    arch::set_percpu(&g_percpu_area);
    const bool same = (arch::percpu() == &g_percpu_area);

    arch::fence(arch::barrier::memory);
    arch::fence(arch::barrier::store);
    arch::fence(arch::barrier::complete);
    arch::fence(arch::barrier::fetch);

    machine::print(same ? "cpu: percpu round-trips\n" : "cpu: percpu FAILED\n");
    machine::print("cpu: four barriers accepted\n");
}

}  // namespace

extern "C" int probe_main() {
    arch::arch_context_init(g_task, &task, reinterpret_cast<void*>(42L),
                            g_stack + sizeof g_stack);

    // ⚠️ `volatile` and read after the round trip. A plain local would be
    // allowed to live in a caller-saved register or be re-materialised, and
    // then it would prove nothing about what the switch preserved.
    volatile int before = 1234;

    machine::print("main: switching to task\n");
    arch::arch_context_switch(g_main, g_task);

    machine::print("main: back, witness=");
    machine::print_int(g_witness);
    machine::print(" before=");
    machine::print_int(static_cast<int>(before));
    machine::putc('\n');

    probe_trap();
    probe_cpu();

    const bool ok = (g_witness == 7 && before == 1234 && g_trapped == 1
                     && arch::percpu() == &g_percpu_area);
    machine::print(ok ? "switch ok\n" : "switch FAILED\n");
    return ok ? 0 : 1;
}
