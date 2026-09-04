// Laying out a context so that BOTH ways of resuming it find what they expect.
//
// ⭐⭐ ONE LAYOUT, BECAUSE TWO WOULD BE A SILENT CORRUPTION. On this machine a
// preempted context is r4-r11 followed by the hardware exception frame, living
// on the task's own stack; a cooperative switch could have used a different
// shape stored in the caller's block. It does not — `arch_context_switch` here
// is a pended exception, so both paths save and restore this one shape. A
// kernel that mixes a yield with a timer therefore cannot corrupt a context by
// resuming it through the other door.
//
// The block the caller supplies holds ONE word: the saved stack pointer. The
// rest of the 128 bytes the interface reserves is unused, which is the honest
// consequence of the state living where the machine puts it.
//
//   [ctx] ──▶ saved SP ──▶ r4  r5  r6  r7  r8  r9 r10 r11   (8 words)
//                          r0  r1  r2  r3 r12  LR  PC xPSR   (the hardware's 8)
#include <openarch/abi.h>

extern "C" void openarch_cm_task_exit();

extern "C" void arch_context_init(void* ctx, void (*entry)(void*), void* arg,
                                  void* stack_top) {
    // ⚠️ EIGHT, NOT FOUR. AAPCS requires 8-byte alignment at a public
    // interface, and the exception entry the hardware performs assumes the
    // frame it stacks is 8-aligned. A caller passing the top of an odd-sized
    // buffer otherwise produces a misaligned access somewhere inside whatever
    // the task calls, reported at a location unrelated to its cause.
    auto sp = reinterpret_cast<unsigned long>(stack_top);
    sp &= ~static_cast<unsigned long>(7);

    auto* frame = reinterpret_cast<unsigned long*>(sp) - 16;
    for (int i = 0; i < 16; ++i) frame[i] = 0;

    frame[8]  = reinterpret_cast<unsigned long>(arg);              // r0
    frame[13] = reinterpret_cast<unsigned long>(&openarch_cm_task_exit) | 1u;
    frame[14] = reinterpret_cast<unsigned long>(entry) | 1u;       // PC, thumb bit
    // ⚠️ xPSR WITH THE T BIT SET, AND NOTHING ELSE. Clearing it would make the
    // exception return enter ARM state, which no M-profile core implements —
    // the fault is a UsageFault at the first instruction of the task, reported
    // as an invalid state rather than as a bad context.
    frame[15] = 0x01000000ul;

    *static_cast<unsigned long*>(ctx) = reinterpret_cast<unsigned long>(frame);
}
