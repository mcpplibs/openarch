/* The three things a probe needs from a machine, and nothing else.
 *
 * ⚠️ THIS FILE EXISTS SO THAT THE PROBE ITSELF DOES NOT.
 *
 * The property under test is that ONE piece of code compiles and runs on two
 * genuinely different instruction sets. That test is worthless if the code is
 * written twice, and it is also worthless if the difference is hidden — so the
 * difference is put here, in three functions and two implementations of about
 * thirty lines each, and everything else is shared.
 *
 * A console address and a power-off register are BOARD facts. openarch does not
 * carry them, and a probe that depended on a board package would be limited to
 * the architectures that have one — which today is one architecture, which is
 * the situation the gate exists to leave.
 */
#ifndef OPENARCH_PROBE_MACHINE_H
#define OPENARCH_PROBE_MACHINE_H

namespace machine {
void putc(char c);
void print(const char* s);
void print_int(int v);
[[noreturn]] void poweroff(int code);
}  // namespace machine

#endif
