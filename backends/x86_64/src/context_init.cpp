// arch_context_init — x86_64.
//
// ⚠️ THIS BACKEND IS THE ONLY ONE THAT WRITES TO THE NEW CONTEXT'S STACK, AND
// THE REASON IS ARCHITECTURAL RATHER THAN STYLISTIC.
//
// riscv64 and aarch64 hold the return address in a register (`ra`, `x30`), so
// preparing a context that has never run means storing the trampoline's address
// into that register's slot. x86_64 holds it on the stack, and `ret` reads it
// from there — so the trampoline address has to be PLANTED at the top of the
// new stack, and the saved `rsp` has to point at it.
//
// The consequence is that a context here is not self-contained: it refers to a
// stack that must already exist and must remain valid. That is true on the
// other two machines as well, but only here is it true before the context has
// executed a single instruction.

// ⚠️ `openarch/types.h` AND NOT `openarch/abi.h`. This file implements a
// function the ABI declares, and it needs the ABI's WIDTHS rather than its
// declarations — the register-file mirror below is this backend's own layout,
// not something the contract names. Including the contract would compile and
// would state a dependency that is not there.
#include <openarch/types.h>

extern "C" void arch_context_entry();   // the trampoline in context.S

namespace {

struct Saved {
    arch_u64 rbx;
    arch_u64 rbp;
    arch_u64 r12;   // entry
    arch_u64 r13;   // arg
    arch_u64 r14;
    arch_u64 r15;
    arch_u64 rsp;
};

static_assert(sizeof(Saved) == 7 * 8,
              "context.S stores seven quadwords");
static_assert(sizeof(Saved) <= 128,
              "arch::context reserves 128 bytes; x86_64 needs 56");

}  // namespace

extern "C" void arch_context_init(void* ctx, void (*entry)(void*), void* arg,
                                  void* stack_top) noexcept {
    auto* s = static_cast<Saved*>(ctx);

    auto top = reinterpret_cast<arch_u64>(stack_top) & ~15ULL;

    // ⚠️ TWO SLOTS, NOT ONE, AND THE SECOND IS WHAT MAKES THE ALIGNMENT RIGHT.
    //
    // The System V AMD64 ABI states the requirement at the CALL, not at the
    // entry: `rsp` is 16-byte aligned when `call` executes, so a function
    // begins with `rsp ≡ 8 (mod 16)` — the return address `call` pushed. Code
    // the compiler generates aligns its own frame from that assumption, and a
    // frame built from the wrong residue faults on the first 16-byte spill and
    // is silently misaligned otherwise.
    //
    // The chain here is: `arch_context_switch` restores the saved `rsp` and
    // executes `ret`, which pops eight bytes. So for `arch_context_entry` — and
    // therefore for `entry`, which it reaches by `jmp` — to begin at `≡ 8`, the
    // saved `rsp` must be 16-ALIGNED, not eight below the top.
    //
    // One slot would leave `rsp ≡ 0` at `entry`. That is the arrangement the
    // other two backends have, because on riscv64 and aarch64 the ABI requires
    // the stack pointer to be 16-aligned AT function entry: there is no pushed
    // return address, so the residue is zero and porting this by analogy gives
    // exactly the wrong answer.
    //
    // The lower slot holds the trampoline's address; the upper holds zero,
    // which is where `arch_context_entry` would return to if the interface
    // permitted `entry` to return. It does not, and a stack walker reading zero
    // stops rather than following whatever was there.
    auto* stack = reinterpret_cast<arch_u64*>(top);
    *--stack = 0;
    *--stack = reinterpret_cast<arch_u64>(&arch_context_entry);

    s->rbx = 0;
    s->rbp = 0;                                                   // frame chain ends here
    s->r12 = reinterpret_cast<arch_u64>(entry);
    s->r13 = reinterpret_cast<arch_u64>(arg);
    s->r14 = 0;
    s->r15 = 0;
    s->rsp = reinterpret_cast<arch_u64>(stack);
}
