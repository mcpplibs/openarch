// openarch.context — the part of the riscv64 backend that can be C++.
//
// `arch_context_init` writes a saved-context image and never touches the live
// stack pointer, so unlike the switch itself it has no reason to be assembly.
// Keeping it here means the register layout is stated once in a readable form
// and once in the assembly that consumes it, with a static assertion binding
// the two.

// ⚠️ `openarch/types.h` AND NOT `openarch/abi.h`. This file implements a
// function the ABI declares, and it needs the ABI's WIDTHS rather than its
// declarations — the register-file mirror below is this backend's own layout,
// not something the contract names. Including the contract would compile and
// would state a dependency that is not there.
#include <openarch/types.h>

extern "C" void arch_context_entry();   // the trampoline in context.S

namespace {

// The saved image, in the order context.S stores it. ⚠️ This layout and the
// offsets in context.S are the same decision written twice; the assertion below
// catches a size disagreement, and the field order is checked by the probe
// actually running rather than by a comment.
struct Saved {
    arch_u64 ra;
    arch_u64 sp;
    arch_u64 s[12];   // s0-s11
};

static_assert(sizeof(Saved) == 14 * 8,
              "context.S stores fourteen doublewords");
static_assert(sizeof(Saved) <= 128,
              "arch::context reserves 128 bytes; riscv64 needs 112");

}  // namespace

extern "C" void arch_context_init(void* ctx, void (*entry)(void*), void* arg,
                                  void* stack_top) noexcept {
    auto* s = static_cast<Saved*>(ctx);

    // The RISC-V calling convention requires a 16-byte aligned stack pointer at
    // every call boundary. Aligning down here rather than requiring the caller
    // to do it: a kernel computing `base + size` has no reason to know this
    // architecture's alignment, and an unaligned stack fails in a way that
    // points nowhere near the cause.
    auto top = reinterpret_cast<arch_u64>(stack_top) & ~15UL;

    s->ra   = reinterpret_cast<arch_u64>(&arch_context_entry);
    s->sp   = top;
    // s1 and s2 carry the entry point and its argument through the switch.
    // ⚠️ Not a0/a1: those are argument registers, and the switch does not
    // restore them — a0 holds `&from` on the way in.
    s->s[0] = 0;                                              // s0 (frame ptr)
    s->s[1] = reinterpret_cast<arch_u64>(entry);          // s1
    s->s[2] = reinterpret_cast<arch_u64>(arg);            // s2
    for (int i = 3; i < 12; ++i) s->s[i] = 0;
}
