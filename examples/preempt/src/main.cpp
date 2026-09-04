// A preemptive scheduler over openarch, on a machine with no address space.
//
// ⭐⭐ THE CLAIM: two tasks that never yield are nevertheless interleaved, and
// each can prove it was interrupted rather than having given way.
//
// ⚠️ AND THAT IS WHY THE ASSERTION IS A COUNTER EACH TASK DID NOT ADVANCE. Two
// tasks that print would also print if the switch never happened and one simply
// ran to completion — the shape of green that says nothing.
#include <openarch/abi.h>

extern "C" void board_print(const char*);
extern "C" [[noreturn]] void board_exit(int);
extern "C" void board_start_tick(unsigned);

namespace {

constexpr int kTasks = 2;

alignas(8)  unsigned char g_stack[kTasks][2048];
unsigned* g_psp[kTasks] = {nullptr, nullptr};

volatile int  g_current = -1;
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

}  // namespace

// ⭐⭐ THE SWITCH AT PendSV IS *NOT* `arch_context_switch`, AND FINDING THAT OUT
// IS WHAT THIS EXAMPLE IS FOR.
//
// `arch_context_switch` is a COOPERATIVE primitive: it saves the callee-saved
// registers of whoever called it and resumes another such saved set. Inside an
// exception handler that is the wrong thing to swap. The hardware has already
// stacked r0-r3, r12, LR, PC and xPSR onto the interrupted task's stack and is
// running the handler on MSP; swapping the handler's own registers changes the
// handler's stack, and the exception return then unstacks a frame belonging to
// nobody.
//
// Measured: with `arch_context_switch` called from PendSV, the program builds,
// boots, and reports that neither task ever observed the other — the tasks were
// never actually interleaved, and only the counter assertion caught it.
//
// The M-profile mechanism is different in kind: tasks run on PSP, the handler
// on MSP, and the switch is performed by saving r4-r11 below the hardware frame
// on the outgoing task's PSP, pointing PSP at the incoming task, restoring its
// r4-r11, and returning with an EXC_RETURN that says "resume in Thread mode on
// PSP". The hardware unstacks the rest.
//
// ⚠️ THIS IS A GAP IN openarch, NOT A DETAIL OF THIS BOARD. The layer has a
// primitive for "switch to another saved context" and none for "switch the
// context this trap will return to". Every architecture needs the second to
// preempt, and every architecture spells it differently — which is exactly the
// shape of thing this layer exists to abstract. Recorded rather than papered
// over; see this example's README.
//
// What openarch still supplies here: `arch_context_init` lays out the initial
// frames, `arch_trap_enable_interrupts` gates the timer, and the fault vectors
// route through its trampoline.
extern "C" __attribute__((used)) unsigned* pendsv_pick(unsigned* outgoing_psp) {
    if (g_current < 0) return outgoing_psp;
    g_psp[g_current] = outgoing_psp;
    g_current = (g_current + 1) % kTasks;
    return g_psp[g_current];
}

extern "C" __attribute__((naked)) void PendSV_Handler() {
    __asm__ volatile(
        "mrs   r0, psp\n"
        "subs  r0, r0, #32\n"
        "stmia r0!, {r4-r7}\n"        // low callee-saved
        "mov   r4, r8\n"
        "mov   r5, r9\n"
        "mov   r6, r10\n"
        "mov   r7, r11\n"
        "stmia r0!, {r4-r7}\n"        // high callee-saved, via the low four
        "subs  r0, r0, #32\n"
        "push  {lr}\n"
        "bl    pendsv_pick\n"          // r0 = outgoing PSP, returns incoming
        "pop   {r1}\n"                 // the EXC_RETURN we were entered with
        "adds  r0, r0, #16\n"
        "ldmia r0!, {r4-r7}\n"        // high callee-saved
        "mov   r8, r4\n"
        "mov   r9, r5\n"
        "mov   r10, r6\n"
        "mov   r11, r7\n"
        "subs  r0, r0, #32\n"
        "ldmia r0!, {r4-r7}\n"        // low callee-saved
        "adds  r0, r0, #16\n"
        "msr   psp, r0\n"
        "bx    r1\n");
}

extern "C" void preempt_verdict();

// ⚠️ THE VERDICT IS REACHED FROM THE TIMER, NOT FROM A TASK. The tasks never
// return — that is the point of them — so nothing in task_body could report.
// After enough ticks for preemption to have happened many times over, the
// handler stops the world and reports.
extern "C" void scheduler_tick() {
    if (++g_ticks == 400) preempt_verdict();
}

extern "C" void Reset_Handler() {
    board_print("preempt: openarch on a machine with no address space\n");

    // ⚠️ THE INITIAL FRAME IS THE ONE THE HARDWARE WILL UNSTACK, so it is laid
    // out the way an exception entry would have left it: eight words of
    // hardware frame (r0-r3, r12, LR, PC, xPSR) above eight of callee-saved.
    // `arch_context_init` cannot do this — it lays out a COOPERATIVE context,
    // which is the distinction this example exists to show.
    for (int i = 0; i < kTasks; ++i) {
        auto* top = reinterpret_cast<unsigned*>(
            &g_stack[i][sizeof(g_stack[i])]);
        top -= 16;
        for (int w = 0; w < 16; ++w) top[w] = 0;
        top[8]  = static_cast<unsigned>(i);                      // r0 = task id
        top[14] = reinterpret_cast<unsigned>(&task_body) | 1u;   // PC, thumb bit
        top[15] = 0x01000000u;                                   // xPSR, T set
        g_psp[i] = top;
    }

    // A short period: the tasks must be interrupted many times over the window
    // the main loop waits.
    board_start_tick(2000);
    arch_trap_enable_interrupts(1);

    // Enter the first task: point PSP at its frame, switch to Thread-mode-on-PSP
    // and branch in. From here the timer owns the interleaving.
    g_current = 0;
    __asm__ volatile(
        "msr psp, %0\n"
        "movs r0, #2\n"
        "msr control, r0\n"   // Thread mode uses PSP
        "isb\n"
        :: "r"(g_psp[0] + 16) : "r0", "memory");
    task_body(reinterpret_cast<void*>(0ul));
    for (;;) {}   // unreachable
}

// Called from the tasks' own progress by the semihosting host after enough
// ticks — kept as a separate entry so the check reads as a check.
extern "C" __attribute__((used)) void preempt_verdict() {
    const bool both = g_observed_preemption[0] && g_observed_preemption[1];
    board_print(both ? "preempt: both tasks observed preemption\n"
                     : "preempt: FAILED — no task observed the other running\n");
    board_exit(both ? 0 : 1);
}
