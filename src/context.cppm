// openarch.context — execution contexts, and switching between them.
//
// ⚠️ THIS IS ONE OF THE TWO PRIMITIVES THAT DECIDE WHETHER openarch EXISTS.
//
// The layer's gate is not "does it compile on a second architecture" but "does
// the abstraction survive one". Context switching and the page-table entry are
// where it is decided, and everything else in the layer is easier than both.
// The design record states the consequence plainly: if either breaks, what
// follows it is an illusion.
//
// WHAT MAKES THE ABSTRACTION POSSIBLE
//
// A switch happens at an ordinary function call site. The caller has already
// spilled everything the calling convention makes caller-saved, so a switch
// needs to preserve only the callee-saved registers. That observation holds on
// every architecture with a calling convention, and it is why one interface can
// cover machines whose register files have nothing in common:
//
//   riscv64      ra sp s0-s11            (13 integer registers)
//   aarch64      x19-x30 sp, d8-d15
//   x86_64 SysV  rbx rbp r12-r15 rsp
//
// ⚠️ WHY THE ZERO-COST ARGUMENT IS NOT THE USUAL ONE
//
// The design record's general rule for this layer is "explicit `inline` first,
// LTO as the fallback". It does not apply here and following it would produce a
// design that crashes.
//
// `arch_context_switch` cannot be a C++ function. A compiler-generated prologue
// saves registers relative to the CURRENT stack, and the epilogue would restore
// them after the stack pointer has been replaced — reading a frame that belongs
// to a different context. The switch is therefore a naked assembly symbol, and
// its cost is one `call`. That is not a compromise: a switch IS a call, so the
// declaration below adds nothing to what a hand-written call would cost, and
// the criterion for "zero cost" is that the emitted instruction sequence is
// identical rather than that the call disappears.

export module openarch.context;

export namespace arch {

// One saved execution context.
//
// Opaque by size and alignment rather than by field: the contents are the
// architecture's business, and a consumer that could see them would be able to
// write code that only builds on one machine. The storage is provided by the
// caller, which is what lets a kernel place contexts inside its own task
// structures rather than allocating them.
//
// ⚠️ 128 bytes covers riscv64's thirteen registers with room for the frame
// pointer conventions of the other two. It is checked against the real size by
// a static assertion in the backend, so a machine that needs more fails to
// build rather than corrupting the next object.
struct context {
    alignas(16) unsigned char storage[128];
};

extern "C" {

// Saves the current context into `from` and resumes `to`.
//
// Returns when something switches back to `from`. The first return therefore
// happens in a different context from the call, which is why the assembly form
// is load-bearing rather than an optimisation.
void arch_context_switch(context& from, context& to) noexcept;

// Prepares `ctx` so that switching to it begins executing `entry(arg)` on the
// stack whose highest address is `stack_top`.
//
// `stack_top` is one past the end, and is aligned down by the backend to
// whatever the architecture's calling convention requires. Passing an
// unaligned value is therefore not an error a caller has to avoid.
//
// ⚠️ `entry` must not return. There is no context to return TO: the initial
// return address is a trampoline that has no caller. A kernel gives each task
// an entry that ends by switching away.
void arch_context_init(context& ctx, void (*entry)(void*), void* arg,
                       void* stack_top) noexcept;

}  // extern "C"

}  // namespace arch
