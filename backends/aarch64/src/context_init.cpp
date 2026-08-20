// openarch.context — the part of the aarch64 backend that can be C++.
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

// The saved image, in the order context.S stores it: x19-x30 as six `stp`
// pairs, then SP.
//
// ⚠️ This layout and the offsets in context.S are the same decision written
// twice; the assertion below catches a size disagreement, and the field order
// is checked by the probe actually running rather than by a comment.
struct Saved {
    arch_u64 x[12];   // x19 .. x30   (x29 = frame pointer, x30 = link)
    arch_u64 sp;
};

static_assert(sizeof(Saved) == 13 * 8,
              "context.S stores thirteen doublewords");
static_assert(sizeof(Saved) <= 128,
              "arch::context reserves 128 bytes; aarch64 needs 104");

// The two indices the trampoline reads back.
constexpr int kX19 = 0;    // entry point
constexpr int kX20 = 1;    // its argument
constexpr int kX30 = 11;   // link register — where the switch returns to

}  // namespace

extern "C" void arch_context_init(void* ctx, void (*entry)(void*), void* arg,
                                  void* stack_top) noexcept {
    auto* s = static_cast<Saved*>(ctx);

    // AAPCS64 requires SP to be 16-byte aligned at every instruction that uses
    // it as a base, which is stricter than the call-boundary rule RISC-V
    // states. Aligning down here rather than requiring the caller to do it: a
    // kernel computing `base + size` has no reason to know this architecture's
    // alignment, and an unaligned SP faults in a way that points nowhere near
    // the cause.
    auto top = reinterpret_cast<arch_u64>(stack_top) & ~15UL;

    for (int i = 0; i < 12; ++i) s->x[i] = 0;

    // x19 and x20 carry the entry point and its argument through the switch.
    // ⚠️ Not x0/x1: those are argument registers, and the switch restores no
    // argument register — x0 holds `&from` on the way in. Callee-saved
    // registers are the only ones that survive, which is why the trampoline
    // reads them rather than being handed parameters.
    s->x[kX19] = reinterpret_cast<arch_u64>(entry);
    s->x[kX20] = reinterpret_cast<arch_u64>(arg);
    s->x[kX30] = reinterpret_cast<arch_u64>(&arch_context_entry);
    s->sp      = top;
}
