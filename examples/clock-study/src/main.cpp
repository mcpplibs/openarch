// A study, not a demonstration. It answers where a clock belongs.
//
// ⭐ THIS FILE IS DELIBERATELY NOT WRITTEN AGAINST AN openarch INTERFACE, AND
// THAT IS THE WHOLE METHOD.
//
// The other four interfaces in this layer were written after a probe showed
// that all the machines could answer the same question. A clock has not been
// shown to have that property, and writing the interface first would decide the
// question by assumption: whatever shape got written would then look inevitable.
//
// So this reads each machine directly, and the SOURCE is the evidence. What it
// costs to get a reading on each machine — how much of it is architectural and
// how much has to be told by a board — is visible here as code rather than
// asserted in a document.
//
// ⚠️ TWO SEPARATE QUESTIONS, AND CONFLATING THEM IS THE MISTAKE THIS STUDY
// EXISTS TO AVOID.
//
//   1. "What time is it" — read a monotonic counter.
//   2. "Interrupt me at time T" — arm a comparator.
//
// They have different answers on the same machine. A `timer` interface that
// bundled them would be as wide as the wider of the two, and would then be
// unimplementable wherever the wider one needs board knowledge.

#include "machine.h"

namespace {

// ── What a reading costs, per machine ───────────────────────────────────────
//
// Each `read_*` below is annotated with where its numbers come from. That
// annotation is the study's output; the printed values only prove the code ran.

#if defined(__riscv)

// ⚠️ TWO SOURCES ON THIS MACHINE, AND THEY DIFFER IN EXACTLY THE WAY THE STUDY
// IS ABOUT.
//
//   * `rdtime` reads the `time` CSR. No address, no board knowledge. Whether it
//     is available at all depends on the platform wiring it: on a real machine
//     it may trap, and the trap handler is expected to emulate it from the
//     memory-mapped counter below.
//   * `mtime` lives in the CLINT, at an address only the BOARD knows. QEMU's
//     `virt` puts it at 0x0200BFF8; a different board puts it elsewhere.
//
// So riscv can answer "what time is it" without board knowledge *if* `rdtime`
// works, and cannot otherwise. The comparator (`mtimecmp`, 0x02004000 on this
// board) has no CSR form at all in M-mode — Sstc adds `stimecmp` for S-mode,
// and this backend runs in M.
inline unsigned long long read_counter_arch() {
    unsigned long long v;
    asm volatile("rdtime %0" : "=r"(v));
    return v;
}
constexpr unsigned long long kClintMtime = 0x0200BFF8ULL;   // BOARD
inline unsigned long long read_counter_board() {
    return *reinterpret_cast<volatile unsigned long long*>(kClintMtime);
}
inline unsigned long long frequency_hint() { return 0; }    // not architectural
constexpr const char* kArchNote =
    "riscv64: rdtime is a CSR (no board knowledge); mtime/mtimecmp are in the "
    "CLINT at a board address; no frequency register";

#elif defined(__aarch64__)

// ⭐ THE OPPOSITE SHAPE. The counter AND its frequency are architectural system
// registers. Nothing here needs an address.
//
// ⚠️ The comparator is architectural too (`CNTP_CVAL_EL0`), but the INTERRUPT it
// raises arrives on a PPI whose number is a board fact, and routing it needs an
// interrupt controller this layer does not own.
inline unsigned long long read_counter_arch() {
    unsigned long long v;
    asm volatile("mrs %0, cntpct_el0" : "=r"(v));
    return v;
}
inline unsigned long long read_counter_board() { return read_counter_arch(); }
inline unsigned long long frequency_hint() {
    unsigned long long v;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}
constexpr const char* kArchNote =
    "aarch64: cntpct_el0 and cntfrq_el0 are architectural registers; the "
    "comparator is architectural but its interrupt number is a board fact";

#elif defined(__x86_64__)

// ⚠️ THE THIRD SHAPE AGAIN. `rdtsc` needs no address, but it is a cycle counter
// whose frequency is not architectural: CPUID leaf 0x15 reports it on some
// processors and not others, and on older ones it must be calibrated against
// another timer.
//
// The comparator is worse: this machine has at least four unrelated candidates
// (PIT, HPET, the local APIC timer, TSC-deadline), and WHICH ONE EXISTS is a
// runtime question — HPET is discovered from an ACPI table, the local APIC is
// at an MSR-provided address, TSC-deadline is a CPUID bit.
inline unsigned long long read_counter_arch() {
    unsigned lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<unsigned long long>(hi) << 32) | lo;
}
inline unsigned long long read_counter_board() { return read_counter_arch(); }
inline unsigned long long frequency_hint() {
    // CPUID leaf 0x15: EAX = denominator, EBX = numerator, ECX = core crystal
    // Hz. A zero anywhere means the processor declines to say.
    unsigned a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x15), "c"(0));
    if (a == 0 || b == 0 || c == 0) return 0;
    return static_cast<unsigned long long>(c) * b / a;
}
constexpr const char* kArchNote =
    "x86_64: rdtsc needs no address but its frequency is not architectural "
    "(CPUID 0x15 may decline); the comparator is one of at least four, and "
    "which exists is a runtime question";

#else
#  error "the clock study has no reading for this architecture"
#endif

}  // namespace

extern "C" int probe_main() {
    machine::print("clock-study: ");
    machine::print(kArchNote);
    machine::putc('\n');

    // ⚠️ TWO READINGS WITH WORK BETWEEN THEM, AND THE WORK MATTERS. Two reads
    // back to back can both land in the same tick of a slow counter, and a
    // study that accepted `a <= b` would then report success for a counter that
    // is stuck.
    const auto a = read_counter_arch();
    for (volatile int i = 0; i < 200000; ++i) { }
    const auto b = read_counter_arch();

    machine::print("clock-study: architectural counter ");
    if (b > a) machine::print("advances\n");
    else       machine::print("DID NOT ADVANCE\n");

    const auto ba = read_counter_board();
    for (volatile int i = 0; i < 200000; ++i) { }
    const auto bb = read_counter_board();
    machine::print("clock-study: board-addressed counter ");
    if (bb > ba) machine::print("advances\n");
    else         machine::print("DID NOT ADVANCE\n");

    const auto f = frequency_hint();
    machine::print("clock-study: frequency ");
    if (f) { machine::print("is reported as "); machine::print_int(static_cast<int>(f / 1000)); machine::print(" kHz\n"); }
    else     machine::print("is NOT reported by the architecture\n");

    machine::print("clock-study done\n");
    return 0;
}
