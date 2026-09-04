// Laying out a context so that the first `arch_context_switch` into it lands in
// the trampoline with the entry point and argument already in registers.
//
// The layout is the switch's, read in the order it stores: r4-r7, then r8-r11,
// then SP and LR — ten words.
#include <openarch/abi.h>

extern "C" void openarch_cm_trampoline();

extern "C" void arch_context_init(void* ctx, void (*entry)(void*), void* arg,
                                  void* stack_top) {
    auto* w = static_cast<unsigned long*>(ctx);
    for (int i = 0; i < 10; ++i) w[i] = 0;

    // r4 and r5 carry what the trampoline needs; they are the first two slots.
    w[0] = reinterpret_cast<unsigned long>(entry);   // r4
    w[1] = reinterpret_cast<unsigned long>(arg);     // r5

    // ⚠️ THE STACK POINTER IS ALIGNED DOWN TO 8, WHICH AAPCS REQUIRES AT A
    // PUBLIC INTERFACE AND WHICH A CALLER PASSING THE TOP OF AN ODD-SIZED
    // BUFFER WILL OTHERWISE VIOLATE. The fault it produces is not a stack
    // fault: it is a misaligned access somewhere inside whatever the task
    // calls, which reports a location unrelated to the cause.
    auto sp = reinterpret_cast<unsigned long>(stack_top);
    sp &= ~static_cast<unsigned long>(7);
    w[8] = sp;                                        // SP
    w[9] = reinterpret_cast<unsigned long>(&openarch_cm_trampoline) | 1u;  // LR, thumb bit
}
