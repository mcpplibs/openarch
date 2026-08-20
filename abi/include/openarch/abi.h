/* openarch's ABI: what a backend implements, and the only thing that crosses
 * the boundary between this specification and an implementation of it.
 *
 * ⚠️ WHY THERE IS A C ABI HERE AT ALL, WHEN THE MODULES ARE C++.
 *
 * The interface and its backends are separate PACKAGES, and a module
 * implementation unit cannot live in a different package from the module it
 * implements — that is a property of C++ modules, not of this build system.
 * Until 0.3.0 the backends were implementation units of `openarch.pte`,
 * `openarch.trap` and `openarch.cpu`, which is exactly why they had to sit in
 * the same package.
 *
 * `openarch.context` was already free of that: it declared `extern "C"` and the
 * backend defined plain symbols. Splitting the repository made the other three
 * follow, and the result is better for a reason that has nothing to do with the
 * split: a specification whose boundary is a C ABI can be implemented by
 * something that is not a C++ module — an assembler file, a vendor's blob, a
 * second language — and one whose boundary is a C++ module cannot.
 *
 * This is the same shape openkal uses, and for the same reason.
 *
 * ⚠️ EVERY TYPE HERE IS FIXED-WIDTH OR A POINTER. `unsigned long long` and not
 * `unsigned long`: the latter is 64 bits on the systems this was written on and
 * 32 on Windows, and a page-table entry is 64 bits everywhere. That mistake has
 * already been made once in this repository and was caught only because the
 * constants involved were `constexpr`.
 */
#ifndef OPENARCH_ABI_H
#define OPENARCH_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── openarch.context ───────────────────────────────────────────────────────
 *
 * The storage is the caller's and is opaque to it: 128 bytes, 16-aligned. Each
 * backend asserts its own layout fits, so a machine that needs more fails to
 * build rather than corrupting the next object.
 *
 * ⚠️ A saved context is INTEGER state. Neither backend saves floating-point
 * registers; a kernel whose tasks use them saves them itself, or compiles with
 * floating point disabled. See the note in context.cppm for how the second
 * architecture turned that from an accident into a contract. */
void arch_context_switch(void* from, void* to);
void arch_context_init(void* ctx, void (*entry)(void*), void* arg,
                       void* stack_top);

/* ── openarch.pte ──────────────────────────────────────────────────────────
 *
 * `perm`: 0 read, 1 read_write, 2 read_exec, 3 read_write_exec
 * `mt`  : 0 normal, 1 device
 * `user`: non-zero for a mapping reachable from unprivileged code            */
unsigned long long arch_pte_make_leaf(unsigned long long phys, int perm,
                                      int mt, int user);
int                arch_pte_valid(unsigned long long bits);
unsigned long long arch_pte_phys(unsigned long long bits);

/* Programs whatever the machine needs before a memory type is meaningful.
 * Empty on riscv64, where the type is in the entry; writes `MAIR_EL1` on
 * aarch64, where the entry holds only an index into it. */
void arch_pte_install_memory_attributes(void);

/* ── openarch.trap ─────────────────────────────────────────────────────────
 *
 * ⚠️ THE LAYOUT IS FROZEN AND IS SHARED WITH ASSEMBLY. Each backend's entry
 * stub reserves exactly `sizeof(arch_trap_frame)` bytes above the registers it
 * saved; a disagreement would corrupt a saved register rather than fail.
 *
 * `kind` uses the same ordering as `arch::trap_kind`:
 *   0 breakpoint  1 page_fault  2 illegal  3 unaligned  4 interrupt  5 other
 *
 * `instr_len` exists because a portable handler that resumes past a breakpoint
 * cannot derive it: `rv64gc` emits the two-byte `c.ebreak`, aarch64 has one
 * instruction width. Only the backend knows.                                  */
typedef struct arch_trap_frame {
    unsigned long long pc;
    unsigned long long addr;
    unsigned long long cause;
    int                kind;
    unsigned           instr_len;
} arch_trap_frame;

typedef void (*arch_trap_handler_fn)(arch_trap_frame*);

arch_trap_handler_fn arch_trap_set_handler(arch_trap_handler_fn h);
void                 arch_trap_enable_interrupts(int on);
int                  arch_trap_interrupts_enabled(void);

/* ── openarch.cpu ──────────────────────────────────────────────────────────
 *
 * `barrier`: 0 memory, 1 store, 2 complete, 3 fetch — the four orderings both
 * machines can state. riscv expresses them with one instruction taking two
 * sets; aarch64 with three instructions taking a shareability domain.        */
void* arch_cpu_percpu(void);
void  arch_cpu_set_percpu(void* p);
void  arch_cpu_fence(int barrier);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* OPENARCH_ABI_H */
