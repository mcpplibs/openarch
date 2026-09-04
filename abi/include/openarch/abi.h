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
 * ⚠️ EVERY TYPE HERE IS FIXED-WIDTH OR A POINTER, AND THE WIDTHS ARE NAMED IN
 * ONE PLACE RATHER THAN SPELLED AT EACH USE.
 *
 * `openarch/types.h` defines `arch_u32`, `arch_u64` and `arch_uptr`, and
 * asserts their widths. Until 0.4.0 this file wrote `unsigned long long` forty
 * times with a comment explaining why it was not `unsigned long` — a rule that
 * was described and never checked. The one time it was broken, in `1UL << 53`,
 * the shift was wider than the type on Windows: undefined, and in practice
 * silently zero. It was caught by accident, because the constant happened to be
 * `constexpr` and the compiler was forced to evaluate it.
 */
#ifndef OPENARCH_ABI_H
#define OPENARCH_ABI_H

#include <openarch/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── The vocabulary ────────────────────────────────────────────────────────
 *
 * ⭐ THESE ENUMERATORS ARE THE CONTRACT'S, NOT THE C++ FACE'S, AND THAT IS THE
 * POINT.
 *
 * Until 0.4.0 the C ABI took bare `int` and named the meanings in a comment,
 * while `mcpplibs.openarch` had `enum class perm`, `memory_type`, `trap_kind`
 * and `barrier` with the values written out again. Two faces of one library
 * held the same four tables, agreeing by inspection.
 *
 * The tables live here now, and the C++ enumerations are DEFINED from them —
 * `read_write = ARCH_PERM_READ_WRITE` — so the faces cannot drift: there is one
 * table and the other spelling is derived from it. `tests/faces.cpp` checks the
 * derivation rather than the agreement, which is a weaker thing to have to
 * check.
 *
 * ⚠️ THE PROTOTYPES STILL TAKE `int`. A C enumeration's underlying type is
 * implementation-defined, so a parameter declared `enum arch_perm` is a
 * different parameter under a different compiler — and a contract whose whole
 * purpose is to let a backend be built by something else must not have that
 * property. The enumerators are names for values passed as `int`.             */
enum arch_perm {
    ARCH_PERM_READ            = 0,
    ARCH_PERM_READ_WRITE      = 1,
    ARCH_PERM_READ_EXEC       = 2,
    ARCH_PERM_READ_WRITE_EXEC = 3
};

enum arch_memory_type {
    ARCH_MT_NORMAL = 0,
    ARCH_MT_DEVICE = 1
};

enum arch_trap_kind {
    ARCH_TRAP_BREAKPOINT = 0,
    ARCH_TRAP_PAGE_FAULT = 1,
    ARCH_TRAP_ILLEGAL    = 2,
    ARCH_TRAP_UNALIGNED  = 3,
    ARCH_TRAP_INTERRUPT  = 4,
    ARCH_TRAP_OTHER      = 5
};

enum arch_barrier {
    ARCH_BARRIER_MEMORY   = 0,
    ARCH_BARRIER_STORE    = 1,
    ARCH_BARRIER_COMPLETE = 2,
    ARCH_BARRIER_FETCH    = 3
};

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
 * `perm` is an `arch_perm`, `mt` an `arch_memory_type`, and `user` is non-zero
 * for a mapping reachable from unprivileged code.                            */
arch_u64 arch_pte_make_leaf(arch_u64 phys, int perm,
                                      int mt, int user);
int                arch_pte_valid(arch_u64 bits);
arch_u64 arch_pte_phys(arch_u64 bits);

/* Programs whatever the machine needs before a memory type is meaningful.
 * Empty on riscv64 and armv7a, where the type is in the entry; writes
 * `MAIR_EL1` on aarch64, where the entry holds only an index into it. */
void arch_pte_install_memory_attributes(void);

/* HOW WIDE THE MACHINE'S PAGE-TABLE ENTRY ACTUALLY IS, IN BYTES.
 *
 * The three functions above carry an entry in an `arch_u64` and say nothing
 * about how it is STORED. That was invisible while every backend was a 64-bit
 * machine with 64-bit entries, and `pte_encode.h` said so in as many words: "a
 * page-table entry is 64 bits on every machine here". It is not. ARMv7-A's
 * short-descriptor entry is 32 bits, and a caller who sizes a table from
 * `sizeof(arch::pte)` builds one twice as large as the hardware walks — every
 * second word read as an entry, with no diagnostic anywhere.
 *
 * The value fits in `arch_u64` on both, so the carrier did not have to change.
 * What was missing is a way to ASK, which is what this adds. A caller writes
 *
 *     entries * arch_pte_entry_bytes()
 *
 * and gets the right size on both classes of machine.
 *
 * Returns 8 on riscv64, aarch64 and x86_64; 4 on armv7a. A backend with no
 * address space (Cortex-M) returns 0, which is the same answer its `provides`
 * already gives by withholding `openarch:address-space` — a caller that got
 * this far has a bug in its own layering. */
arch_u32 arch_pte_entry_bytes(void);

/* ── openarch.trap ─────────────────────────────────────────────────────────
 *
 * ⚠️ THE LAYOUT IS FROZEN AND IS SHARED WITH ASSEMBLY. Each backend's entry
 * stub reserves exactly `sizeof(arch_trap_frame)` bytes above the registers it
 * saved; a disagreement would corrupt a saved register rather than fail.
 *
 * `kind` is an `arch_trap_kind`, stored as `int` for the reason given above.
 *
 * `instr_len` exists because a portable handler that resumes past a breakpoint
 * cannot derive it: `rv64gc` emits the two-byte `c.ebreak`, aarch64 has one
 * instruction width. Only the backend knows.                                  */
typedef struct arch_trap_frame {
    arch_u64 pc;
    arch_u64 addr;
    arch_u64 cause;
    int                kind;
    arch_u32           instr_len;
} arch_trap_frame;

typedef void (*arch_trap_handler_fn)(arch_trap_frame*);

arch_trap_handler_fn arch_trap_set_handler(arch_trap_handler_fn h);
void                 arch_trap_enable_interrupts(int on);
int                  arch_trap_interrupts_enabled(void);

/* ⭐⭐ THE ACTION THE TRAP GROUP WAS MISSING: ACTING ON A TRAP RATHER THAN
 * OBSERVING ONE.
 *
 * `arch_trap_set_handler` lets a kernel SEE a trap and
 * `arch_trap_enable_interrupts` lets it MASK one. Neither lets it change what
 * the trap returns to — and that is the whole of preemption, which is the
 * principal reason to use this layer on a microcontroller at all.
 *
 * Call it from inside a handler. The context that was interrupted is saved and
 * a handle to it is written through `from`; the trap then resumes `to` instead.
 * It RETURNS NORMALLY to the handler: the switch happens when the trap does,
 * not at the call.
 *
 *     void tick(arch_trap_frame* f) {
 *         int next = pick();
 *         if (next != current) {
 *             int prev = current; current = next;
 *             arch_trap_switch(f, &ctx[prev], &ctx[next]);
 *         }
 *     }                                 // ← the switch happens after this
 *
 * ⭐ THE SAME SHAPE AS `arch_context_switch`, DIFFERING ONLY IN WHEN IT TAKES
 * EFFECT. One is "switch now"; this one is "switch on the way out". `from` and
 * `to` are the same 128-byte, 16-aligned storage, laid out by the same
 * `arch_context_init`, so a task can be resumed by either.
 *
 * ⚠️ EVERY MACHINE NEEDS IT AND EVERY MACHINE SPELLS IT DIFFERENTLY, WHICH IS
 * WHY IT IS HERE. riscv64 edits `mepc`, aarch64 `ELR_EL1`, x86_64 the interrupt
 * frame's `RIP`/`RSP` — and M-profile none of those, because its handler runs
 * on a different stack from the task and the switch has to be performed by a
 * pended exception. Three of the four implement it as a cooperative switch
 * taken inside the trap; the fourth cannot, and that difference is exactly the
 * thing an abstraction earns its place by hiding.
 *
 * Backends that implement it declare the capability `openarch:preemption`. A
 * kernel that preempts requires it, and a machine that cannot is refused by
 * name at resolution rather than at link time.
 *
 * ⚠️ `f` IS THE FRAME THE HANDLER RECEIVED. Passing a frame from a different
 * trap, or a null pointer, is undefined: a backend may read the machine state
 * the frame describes.
 *
 * ⚠️⚠️ CALLED MORE THAN ONCE BEFORE THE TRAP RETURNS, THE FIRST `from` AND THE
 * LAST `to` ARE THE ONES THAT APPLY. This is not a convenience; it is the only
 * consistent answer, and getting it wrong cost this layer a defect that
 * presented as a flake.
 *
 * The context being saved is the one that was interrupted, and only the FIRST
 * call in a trap window can name it — by the second, the caller's idea of
 * "current" has already moved. The context to resume is whatever the caller
 * last asked for. A backend that simply overwrote both would write one task's
 * saved stack pointer into another task's storage, losing both.
 *
 * It is not a theoretical window. On M-profile the switch is performed by an
 * exception at the LOWEST priority, so it runs only once no handler is active
 * — and two timer ticks can arrive first. Measured: the second task never ran,
 * and the two contexts held stack pointers 32 bytes apart on one stack.       */
void arch_trap_switch(arch_trap_frame* f, void* from, void* to);

/* ── openarch.cpu ──────────────────────────────────────────────────────────
 *
 * `barrier` is an `arch_barrier` — the four orderings both machines can state.
 * riscv expresses them with one instruction taking two sets; aarch64 with three
 * instructions taking a shareability domain; x86_64 with a memory model under
 * which three of the four need no instruction at all.                        */
void* arch_cpu_percpu(void);
void  arch_cpu_set_percpu(void* p);
void  arch_cpu_fence(int barrier);

/* ⭐ THE OTHER POINTER SLOT: THE ONE THE RUNNING *CONTEXT* OWNS, NOT THE ONE
 * THE PROCESSOR OWNS.
 *
 * `arch_cpu_percpu` is per PROCESSOR — a kernel points it at a structure that
 * describes this hart. This pair is per CONTEXT: it is where the toolchain
 * expects to find the thread-local storage of whatever is running now, and a
 * program compiled with `thread_local` reads through it on every access.
 *
 * ⚠️ THEY ARE NOT THE SAME SLOT AND ON ONE MACHINE THEY COMPETE FOR THE SAME
 * REGISTER. cpu_impl.cpp for riscv64 has said so since it was written:
 *
 *     `tp' IS A CONVENTION HERE, NOT AN ARCHITECTURAL REGISTER. [...] a kernel
 *     may use it for its per-CPU pointer --- but a hosted program on the same
 *     ISA would find its thread pointer there instead. [...] this backend must
 *     not be compiled into anything that also uses a thread pointer.
 *
 * That warning has now been met by an actual program. Measured 2026-08-23:
 * `openkal-opensbi`'s startup object writes `tp` so that libc++abi's
 * `thread_local` works, because on a machine with no operating system nobody
 * else will. Without it the first `throw` faults inside `__cxa_get_globals` and
 * the diagnostic names an exception function and an address --- it says nothing
 * about a thread pointer.
 *
 * ⇒ So the slot belongs here, beside the one it is confused with, and the two
 * are separate operations because on aarch64 and x86_64 they are separate
 * REGISTERS and on riscv64 they are not. An implementation that needs both on
 * riscv64 must move the per-CPU pointer to `sscratch`; this interface makes
 * that a decision with a name rather than a collision discovered at run time.
 *
 * ⚠️ Undefined before someone sets it, exactly as the per-CPU slot is. */
void* arch_cpu_tls(void);
void  arch_cpu_set_tls(void* p);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* OPENARCH_ABI_H */
