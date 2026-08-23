// openarch.cpu — the running processor: where its private storage is, and how
// to order what it does.
//
// ⭐ TWO THINGS THAT LOOK ALIKE AND ARE NOT.
//
// The per-CPU base is nearly the same on both machines: one register that the
// hardware never touches, which a kernel points at a structure of its own.
// riscv reserves `tp` by convention; aarch64 has `TPIDR_EL1` as an
// architectural register. The difference is only where it lives.
//
// The barriers are not alike at all. riscv has ONE instruction, `fence`, whose
// operands are two SETS drawn from {read, write, device-in, device-out} — a
// cross product, and `fence rw, rw` is a point in it. aarch64 has THREE
// instructions with different meanings — `dmb` orders, `dsb` completes, `isb`
// resynchronises the fetch — each taking a shareability domain.
//
// An interface cannot expose either shape without exporting one machine's model
// into the other's callers. What it can expose is the set of QUESTIONS both
// answer, and the four below are those questions.
//
// ⚠️ The names say what is guaranteed, not which instruction is emitted. A
// caller that wants a specific encoding wants the architecture, not this layer.

module;
#include <openarch/abi.h>
export module mcpplibs.openarch.cpu;

export namespace arch {

// The processor's private pointer.
//
// Neither machine gives this any meaning: the hardware reads it never, and a
// kernel stores whatever it likes. It exists here because the REGISTER differs
// and the intent does not.
//
// ⚠️ Undefined before a kernel sets it. Both machines leave it whatever reset
// or the previous occupant left, so reading first and writing later is a way to
// obtain a plausible-looking pointer to nothing.
inline void* percpu() noexcept          { return ::arch_cpu_percpu(); }
inline void  set_percpu(void* p) noexcept { ::arch_cpu_set_percpu(p); }

// The running CONTEXT's private pointer, which is a different question from the
// one above and is asked by different code.
//
// The per-CPU slot is read by a kernel that wants to know which hart it is on.
// This one is read by the TOOLCHAIN, on every access to a `thread_local`, and
// it is set by whoever creates the context. Where a program carries no loader,
// that is the openkal implementation beneath it.
//
// ⚠️ On riscv64 the two slots want the same register (`tp`), and abi.h records
// the measurement that made the conflict real rather than theoretical. Naming
// them separately is what lets an implementation choose; it does not make the
// register conflict go away.
inline void* tls() noexcept             { return ::arch_cpu_tls(); }
inline void  set_tls(void* p) noexcept  { ::arch_cpu_set_tls(p); }

// The four orderings both machines can state.
enum class barrier {
    // Everything before is ordered before everything after, as observed by
    // other agents. riscv `fence rw, rw`; aarch64 `dmb sy`.
    memory = ARCH_BARRIER_MEMORY,

    // Stores before are ordered before stores after. Cheaper than `memory`
    // where a machine distinguishes the two, and identical where it does not.
    // riscv `fence w, w`; aarch64 `dmb st`.
    store = ARCH_BARRIER_STORE,

    // Everything before has COMPLETED, not merely been ordered. This is the
    // one a caller wants before touching a device register or changing a
    // translation. riscv `fence rw, rw` is already completion-flavoured for
    // device accesses; aarch64 needs `dsb sy`, which `dmb` does not provide.
    complete = ARCH_BARRIER_COMPLETE,

    // Instruction fetch sees the writes that came before. Required after
    // writing code, and after changing anything the fetch path caches. riscv
    // `fence.i`; aarch64 `isb`.
    fetch = ARCH_BARRIER_FETCH,
};

inline void fence(barrier b) noexcept { ::arch_cpu_fence(static_cast<int>(b)); }

}  // namespace arch
