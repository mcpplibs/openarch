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

    const bool ok = (g_witness == 7 && before == 1234);
    machine::print(ok ? "switch ok\n" : "switch FAILED\n");
    return ok ? 0 : 1;
}
