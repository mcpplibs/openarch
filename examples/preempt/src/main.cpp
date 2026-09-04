// A preemptive scheduler over openarch, on a machine with no address space.
//
// ⭐⭐ THE CLAIM: two tasks that never yield are nevertheless interleaved, and
// each can prove it was interrupted rather than having given way.
//
// ⚠️ AND THAT IS WHY THE ASSERTION IS A COUNTER EACH TASK DID NOT ADVANCE. Two
// tasks that print would also print if the switch never happened and one simply
// ran to completion — the shape of green that says nothing.
//
// ⭐ THERE IS NO ASSEMBLY IN THIS FILE, AND THERE USED TO BE THIRTY LINES OF IT.
//
// The first version of this example hand-wrote a PendSV handler: save r4-r11
// below the hardware frame on the outgoing task's PSP, swap the pointer,
// restore, return with an EXC_RETURN that resumes Thread mode on PSP. It had to,
// because openarch had a primitive for "switch to another saved context" and
// none for "switch the context this trap will return to" — and on M-profile the
// first is not the second.
//
// That gap is `arch_trap_switch` now, so what remains here is forty lines of
// scheduling POLICY: pick a task, ask the layer to resume it. Which is what an
// example of this layer should have looked like from the start.
#include <openarch/abi.h>

extern "C" void board_print(const char*);
extern "C" [[noreturn]] void board_exit(int);
extern "C" void board_start_tick(unsigned);
extern "C" void openarch_cm_enter(void* ctx);

namespace {

constexpr int kTasks = 2;

alignas(16) unsigned char g_ctx[kTasks][128];
alignas(8)  unsigned char g_stack[kTasks][2048];

volatile int  g_current = 0;
volatile int  g_ticks   = 0;
volatile int  g_progress[kTasks] = {0, 0};
// Each task records what the OTHER task's progress was when it last ran. If it
// ever differs from what it saw before, something ran while this task did not.
volatile int  g_witness[kTasks] = {-1, -1};
volatile bool g_observed_preemption[kTasks] = {false, false};

void task_body(void* arg) {
    const int me    = static_cast<int>(reinterpret_cast<unsigned long>(arg));
    const int other = 1 - me;
    for (;;) {
        ++g_progress[me];
        const int seen = g_progress[other];
        if (g_witness[me] >= 0 && seen != g_witness[me])
            g_observed_preemption[me] = true;   // the other side moved
        g_witness[me] = seen;
        // No yield, no wfi, no cooperation of any kind. Whatever interleaving
        // occurs is the timer's doing.
        for (volatile int spin = 0; spin < 200; ++spin) {}
    }
}

void verdict() {
    const bool both = g_observed_preemption[0] && g_observed_preemption[1];
    board_print(both ? "preempt: both tasks observed preemption\n"
                     : "preempt: FAILED - no task observed the other running\n");
    board_exit(both ? 0 : 1);
}

// The whole of the scheduler. It runs in the timer's handler, picks the next
// task, and hands both contexts to the layer; the switch happens when the trap
// returns, not here.
void on_tick(arch_trap_frame* f) {
    if (++g_ticks == 400) verdict();
    const int prev = g_current;
    const int next = (prev + 1) % kTasks;
    g_current = next;
    arch_trap_switch(f, g_ctx[prev], g_ctx[next]);
}

}  // namespace

// ⚠️ THE VERDICT IS REACHED FROM THE TIMER, NOT FROM A TASK. The tasks never
// return — that is the point of them — so nothing in `task_body` could report.
extern "C" void scheduler_tick() {
    // The frame is the handler's, and `arch_trap_switch` is documented to take
    // the one its handler received. This board routes SysTick straight here
    // rather than through openarch's trampoline, so it builds the one field
    // the layer reads on this machine.
    arch_trap_frame f{};
    f.kind = ARCH_TRAP_INTERRUPT;
    on_tick(&f);
}

extern "C" void Reset_Handler() {
    board_print("preempt: openarch on a machine with no address space\n");

    for (int i = 0; i < kTasks; ++i)
        arch_context_init(g_ctx[i], &task_body,
                          reinterpret_cast<void*>(static_cast<unsigned long>(i)),
                          &g_stack[i][sizeof(g_stack[i])]);

    // A short period: the tasks must be interrupted many times over the window
    // the verdict waits for.
    board_start_tick(2000);
    arch_trap_enable_interrupts(1);

    g_current = 0;
    openarch_cm_enter(g_ctx[0]);   // does not return
    for (;;) {}
}
