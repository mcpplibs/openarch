// The riscv64 half of the probe's machine: QEMU's `virt`.
#include "machine.h"

extern "C" int probe_main();

namespace {
volatile unsigned char* const kUart     = reinterpret_cast<unsigned char*>(0x10000000);
volatile unsigned int*  const kPowerOff = reinterpret_cast<unsigned int*>(0x100000);
}  // namespace

namespace machine {

void putc(char c) { *kUart = static_cast<unsigned char>(c); }
void print(const char* s) { while (s && *s) putc(*s++); }

// ⚠️ Shared by both machines in behaviour but written twice, because a third
// translation unit for six lines would be a file whose only purpose is to be
// shared. If a fourth machine arrives, that trade changes.
void print_int(int v) {
    if (v < 0) { putc('-'); v = -v; }
    char d[12]; int n = 0;
    do { d[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
    while (n-- > 0) putc(d[n]);
}

// QEMU's `virt` syscon: 0x5555 is "pass", and the exit status the emulator
// reports is derived from it. Ending on the firmware's terms rather than on a
// timeout is what lets CI read a verdict.
[[noreturn]] void poweroff(int code) {
    *kPowerOff = code == 0 ? 0x5555u : 0x3333u;
    for (;;) { }
}

}  // namespace machine

// The entry point. `.text.entry` is placed first by the linker script, because
// execution begins at the load address rather than at whichever function the
// linker happened to put there.
extern "C" [[noreturn]] void kmain() { machine::poweroff(probe_main()); }

asm(".section .text.entry,\"ax\",@progbits\n"
    ".globl _start\n"
    "_start:\n"
    "  la sp, __stack_top\n"
    "  call kmain\n"
    "1:\n"
    "  j 1b\n");
