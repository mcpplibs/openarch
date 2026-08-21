// The aarch64 half of the probe's machine: QEMU's `virt`.
#include "machine.h"

extern "C" int probe_main();

namespace {
// The PL011 UART. ⚠️ A different device at a different address from the
// riscv64 machine's 16550A — which is the point: nothing about the probe above
// changes, and everything about reaching a terminal does.
volatile unsigned int* const kUart = reinterpret_cast<unsigned int*>(0x09000000);
}  // namespace

namespace machine {

void putc(char c) { *kUart = static_cast<unsigned int>(c); }
void print(const char* s) { while (s && *s) putc(*s++); }

void print_int(int v) {
    if (v < 0) { putc('-'); v = -v; }
    char d[12]; int n = 0;
    do { d[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
    while (n-- > 0) putc(d[n]);
}

// ⚠️ NO SYSCON HERE, AND THE DIFFERENCE IS REAL RATHER THAN AN OMISSION.
//
// QEMU's aarch64 `virt` has no memory-mapped power-off register of the kind
// riscv's syscon provides. Shutdown goes through PSCI, a firmware call —
// `SYSTEM_OFF` is function 0x84000008, reached by `hvc` when the machine
// starts at EL1 under QEMU's default configuration.
[[noreturn]] void poweroff(int code) {
    (void)code;   // PSCI SYSTEM_OFF carries no status
    register unsigned long x0 asm("x0") = 0x84000008UL;
    asm volatile("hvc #0" :: "r"(x0) : "memory");
    for (;;) { asm volatile("wfi"); }
}

}  // namespace machine

extern "C" [[noreturn]] void kmain() { machine::poweroff(probe_main()); }

asm(".section .text.entry,\"ax\",@progbits\n"
    ".globl _start\n"
    "_start:\n"
    "  ldr x30, =__stack_top\n"
    "  mov sp, x30\n"
    "  bl kmain\n"
    "1:\n"
    "  b 1b\n");
