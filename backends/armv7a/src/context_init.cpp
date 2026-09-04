// openarch.context — the part of the armv7a backend that can be C++.
//
// `arch_context_init` writes a saved-context image and never touches the live
// stack pointer, so unlike the switch itself it has no reason to be assembly.
//
// `openarch/types.h` AND NOT `openarch/abi.h`: this file needs the ABI's
// WIDTHS rather than its declarations. The register-file mirror below is this
// backend's own layout, not something the contract names.
#include <openarch/types.h>

extern "C" void arch_context_entry();   // the trampoline in context.S

namespace {

// The saved image, in the order context.S stores it: r4-r11 as one `stm`, then
// SP and LR.
struct Saved {
    arch_u32 r[8];    // r4 .. r11
    arch_u32 sp;
    arch_u32 lr;
};

static_assert(sizeof(Saved) == 10 * 4,
              "context.S stores ten words");
static_assert(sizeof(Saved) <= 128,
              "arch::context reserves 128 bytes; armv7a needs 40");

constexpr int kR4 = 0;    // entry point
constexpr int kR5 = 1;    // its argument

}  // namespace

extern "C" void arch_context_init(void* ctx, void (*entry)(void*), void* arg,
                                  void* stack_top) noexcept {
    auto* s = static_cast<Saved*>(ctx);

    // AAPCS requires the stack pointer to be 8-byte aligned at every public
    // interface. Aligning down here rather than requiring the caller to do it:
    // a kernel computing `base + size` has no reason to know this
    // architecture's alignment, and the fault an unaligned SP produces points
    // nowhere near the cause.
    const auto top = reinterpret_cast<arch_u32>(stack_top) & ~7U;

    for (int i = 0; i < 8; ++i) s->r[i] = 0;

    // Not r0/r1: those are argument registers and the switch restores none of
    // them — r0 holds `&from` on the way in. Callee-saved registers are the
    // only ones that survive, which is why the trampoline reads them rather
    // than being handed parameters.
    s->r[kR4] = reinterpret_cast<arch_u32>(entry);
    s->r[kR5] = reinterpret_cast<arch_u32>(arg);
    s->sp     = top;
    s->lr     = reinterpret_cast<arch_u32>(&arch_context_entry);
}
