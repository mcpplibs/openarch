// The machine: vector table, reset, semihosting console, SysTick.
//
// Board facts, kept out of main.cpp so that what main.cpp shows is the
// scheduler and not the machine.
#include <openarch/abi.h>

extern "C" unsigned __stack_top;
extern "C" void Reset_Handler();
extern "C" void openarch_cm_trap_entry(unsigned* frame, unsigned exc);
// ⚠️ THE BOARD NAMES openarch'S HANDLER IN SLOT 14, AND NOTHING BUT THE BOARD
// CAN. The table's location is a board fact and the hardware reads it by
// address, so the package that implements the switch cannot install itself. A
// board that forgets is not broken at link time — the switch simply never
// happens, which is why this example asserts preemption rather than progress.
extern "C" void openarch_cm_pendsv();
extern "C" void scheduler_tick();

namespace {
void semihost(int op, const void* arg) {
    register int         r0 __asm__("r0") = op;
    register const void* r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xAB" :: "r"(r0), "r"(r1) : "memory");
}
}  // namespace

extern "C" void board_print(const char* s) { semihost(0x04, s); }
// ⚠️⚠️ `SYS_EXIT_EXTENDED` (0x20), NOT `SYS_EXIT` (0x18), AND THE DIFFERENCE IS
// AN EXIT STATUS THAT LOOKS RIGHT AND IS NOT.
//
// 0x18 takes the reason code in r1 DIRECTLY; the `{reason, code}` block is the
// EXTENDED call, which exists because a 32-bit r1 cannot carry both a reason and
// a status. Passing the block to 0x18 prints everything correctly and then
// reports the WRONG status.
//
// Measured: this example printed `both tasks observed preemption` and
// `mcpp run` exited 1. Every assertion on the OUTPUT passed; only the exit code
// disagreed, and only a check that reads it could tell.
extern "C" [[noreturn]] void board_exit(int code) {
    struct { unsigned reason, code; } b{0x20026u, static_cast<unsigned>(code)};
    semihost(0x20, &b);
    for (;;) {}
}

extern "C" void openarch_panic(const char* what) {
    board_print("PANIC: "); board_print(what); board_print("\n");
    board_exit(1);
}

// ⭐ SysTick drives preemption, and it does no switching of its own: it calls
// the scheduler, which calls `arch_trap_switch`, which pends PendSV. The switch
// therefore happens at the LOWEST exception priority, after every other handler
// has finished — switching stacks inside a high-priority handler is the classic
// way to corrupt an unrelated interrupt.
extern "C" void SysTick_Handler() { scheduler_tick(); }

// Faults route into openarch's trampoline, which normalises them into the frame
// every handler on every architecture reads.
extern "C" __attribute__((naked)) void Fault_Handler() {
    __asm__ volatile(
        "mrs r0, msp\n"
        "mrs r1, ipsr\n"
        "b   openarch_cm_trap_entry\n");
}

extern "C" __attribute__((section(".vectors"), used))
void* const vectors[] = {
    (void*)&__stack_top, (void*)Reset_Handler,
    (void*)Fault_Handler,  // NMI
    (void*)Fault_Handler,  // HardFault
    (void*)Fault_Handler, (void*)Fault_Handler, (void*)Fault_Handler,
    nullptr, nullptr, nullptr, nullptr,
    (void*)Fault_Handler,  // SVCall
    nullptr, nullptr,
    (void*)openarch_cm_pendsv,
    (void*)SysTick_Handler,
};

extern "C" void board_start_tick(unsigned reload) {
    // PendSV at the lowest priority so it runs after everything else.
    *reinterpret_cast<volatile unsigned*>(0xE000ED20) |= (0xFFu << 16);
    auto* syst = reinterpret_cast<volatile unsigned*>(0xE000E010);
    syst[1] = reload;   // RVR
    syst[2] = 0;        // CVR
    syst[0] = 0x7;      // CSR: enable | tickint | processor clock
}
