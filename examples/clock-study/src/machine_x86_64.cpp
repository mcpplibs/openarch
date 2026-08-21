// The x86_64 half of the probe's machine: QEMU's `pc`/`q35`.
//
// ⭐ THE CONSOLE IS NOT MEMORY, WHICH IS THE THIRD MACHINE'S QUIETEST
// DISAGREEMENT.
//
// Both other halves of this probe reach a terminal by storing to an address:
// riscv's 16550A at 0x10000000, aarch64's PL011 at 0x09000000. x86 has a
// SEPARATE ADDRESS SPACE for devices, reached only by the `in` and `out`
// instructions, and no pointer can name port 0x3F8. A `machine.h` that had
// offered "the console's address" instead of "print a character" would have
// been an interface that two machines could implement and a third could not.
#include "machine.h"

extern "C" int probe_main();

namespace {

constexpr unsigned short kCom1 = 0x3F8;

// Line status register bit 5: the transmit holding register is empty.
constexpr unsigned char kThre = 0x20;

inline void outb(unsigned short port, unsigned char v) noexcept {
    asm volatile("outb %0, %1" :: "a"(v), "Nd"(port));
}

inline void outw(unsigned short port, unsigned short v) noexcept {
    asm volatile("outw %0, %1" :: "a"(v), "Nd"(port));
}

inline unsigned char inb(unsigned short port) noexcept {
    unsigned char v;
    asm volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

// ⚠️ A NAMESPACE-SCOPE `bool` AND NOT A FUNCTION-LOCAL `static`. A local static
// with a non-constant initialiser needs `__cxa_guard_acquire`, which lives in
// the C++ runtime this image does not link. The compiler emits the call without
// complaint and the link fails naming a symbol that appears nowhere in the
// source.
bool g_uart_ready = false;

void uart_init() noexcept {
    outb(kCom1 + 1, 0x00);   // no interrupts
    outb(kCom1 + 3, 0x80);   // DLAB: the next two writes are the divisor
    outb(kCom1 + 0, 0x01);   // 115200 baud
    outb(kCom1 + 1, 0x00);
    outb(kCom1 + 3, 0x03);   // 8 bits, no parity, one stop bit
    outb(kCom1 + 2, 0xC7);   // enable and clear the FIFOs
    outb(kCom1 + 4, 0x03);   // data terminal ready, request to send
    g_uart_ready = true;
}

}  // namespace

namespace machine {

void putc(char c) {
    if (!g_uart_ready) uart_init();
    while ((inb(kCom1 + 5) & kThre) == 0) { }
    outb(kCom1, static_cast<unsigned char>(c));
}

void print(const char* s) { while (s && *s) putc(*s++); }

void print_int(int v) {
    if (v < 0) { putc('-'); v = -v; }
    char d[12]; int n = 0;
    do { d[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
    while (n-- > 0) putc(d[n]);
}

// ⚠️ TWO PORTS, BECAUSE QEMU MOVED THE REGISTER AND BOTH SPELLINGS ARE STILL
// IN THE FIELD. Writing 0x2000 to the ACPI PM1a control block requests soft-off;
// QEMU's `q35` and modern `pc` place that block at 0x604, and versions before
// 2.0 placed it at 0xB004. Neither write faults on a machine that does not
// decode the port, so issuing both costs nothing and removes a dependency on
// which QEMU is installed.
//
// The `hlt` loop is what runs if neither is decoded — a machine that will not
// power itself off, which is a correct outcome rather than a hang to diagnose.
[[noreturn]] void poweroff(int code) {
    (void)code;   // the ACPI request carries no status
    outw(0x604,  0x2000);
    outw(0xB004, 0x2000);
    for (;;) { asm volatile("hlt"); }
}

}  // namespace machine

// ⚠️ NO `_start` HERE. The other two machine files carry an entry stub in an
// `asm` block, because on those machines the entry sets a stack pointer and
// calls C. Reaching that state on x86_64 takes a hundred instructions and its
// own linker section, so it lives in `boot_x86_64.S` beside this file.
extern "C" [[noreturn]] void kmain() { machine::poweroff(probe_main()); }
