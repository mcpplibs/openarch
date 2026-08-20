// The A0 probe: two contexts, switched between, and control returning.
//
// ⚠️ WHAT THIS PROVES AND WHAT IT DOES NOT.
//
// It proves the riscv64 backend saves and restores the callee-saved set
// correctly, that a context which has never run starts at its entry point with
// its argument, and that switching back resumes the caller where it left off.
//
// It does not prove the ABSTRACTION holds, and no single-architecture test can.
// The gate for this layer is that one interface survives a second, genuinely
// different machine; an interface shaped around one instruction set always fits
// that instruction set.
import mcpplibs.riscv_virt_rt;
import openarch.context;

namespace {

arch::context g_main;
arch::context g_task;
alignas(16) unsigned char g_stack[4096];

// Values held in callee-saved registers across the switch. If the backend
// dropped or reordered a register, these would come back wrong — which is the
// half of the contract that a "does it run" test would miss.
volatile int g_witness = 0;

[[noreturn]] void task(void* arg) {
    board::printf("task: arg=%d\n", static_cast<int>(reinterpret_cast<long>(arg)));
    g_witness = 7;
    // Back to whoever started us. This never returns, which is the contract:
    // the trampoline that entered this function has no caller.
    arch::arch_context_switch(g_task, g_main);
    for (;;) {}
}

}  // namespace

extern "C" int main() {
    arch::arch_context_init(g_task, &task, reinterpret_cast<void*>(42L),
                            g_stack + sizeof g_stack);

    // Live values in callee-saved registers, so that a backend which failed to
    // restore them is caught rather than merely surviving.
    volatile int before = 1234;
    board::println("main: switching to task");
    arch::arch_context_switch(g_main, g_task);
    board::printf("main: back, witness=%d before=%d\n", g_witness,
                  static_cast<int>(before));

    return (g_witness == 7 && before == 1234) ? 0 : 1;
}
