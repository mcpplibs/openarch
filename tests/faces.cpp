// The two faces are one library.
//
// openarch is reachable two ways — `#include <mcpplibs/openarch.h>` and
// `import mcpplibs.openarch;` — and the claim this file checks is that those
// are two spellings of one thing rather than two parallel declarations that
// happen to line up today.
//
// ⭐ THE CHECKS ARE ABOUT DERIVATION, NOT AGREEMENT, AND THE DIFFERENCE IS THE
// WHOLE VALUE OF THE FILE.
//
// "The C `ARCH_TRAP_ILLEGAL` and the C++ `trap_kind::illegal` are both 2" is an
// agreement: it can be true this morning and false this afternoon, and the only
// thing keeping it true is that somebody edits both places. "`trap_kind::illegal`
// IS `ARCH_TRAP_ILLEGAL`" is a derivation: there is one table and the other
// spelling is computed from it, so the assertions below cannot fail unless
// somebody deliberately unpicks the definition.
//
// A test that can only fail deliberately looks useless, and would be if the
// derivation were obvious in the source. It is not: an enumerator written
// `illegal = ARCH_TRAP_ILLEGAL` and one written `illegal,` compile identically
// today, because the C table happens to be 0,1,2,… in the same order. The
// second form is what this repository had until 0.4.0, and the way it would
// have failed is by someone inserting a value into the middle of one table.
//
// ⚠️ THIS RUNS ON THE HOST, WHERE THERE IS NO BACKEND. That is deliberate. The
// `backend` feature resolves to nothing on a hosted target, so nothing here
// links an implementation of the ABI, and everything asserted below is a
// property of the interface alone. If a future edit made the interface's
// declarations depend on having a backend, this file would stop building — and
// that dependency is precisely what the layering forbids.

#include <mcpplibs/openarch.h>

import mcpplibs.openarch;

#include <cstdio>
#include <type_traits>

// ── The types are the same types, not compatible ones ───────────────────────
//
// `arch::trap_frame` is a `using` declaration of `::arch_trap_frame`. Were it a
// separate `struct` with the same members, this assertion is the only thing
// that would notice, and the symptom in the field would be a backend's assembly
// writing a saved register at an offset the handler reads a different field
// from.
static_assert(std::is_same_v<arch::trap_frame, ::arch_trap_frame>,
              "the module's trap_frame must BE the ABI's, not resemble it");
static_assert(std::is_same_v<arch::trap_handler, ::arch_trap_handler_fn>,
              "the module's handler type must BE the ABI's");

// ── The enumerations are defined from the ABI's ─────────────────────────────
static_assert(static_cast<int>(arch::perm::read)            == ARCH_PERM_READ);
static_assert(static_cast<int>(arch::perm::read_write)      == ARCH_PERM_READ_WRITE);
static_assert(static_cast<int>(arch::perm::read_exec)       == ARCH_PERM_READ_EXEC);
static_assert(static_cast<int>(arch::perm::read_write_exec) == ARCH_PERM_READ_WRITE_EXEC);

static_assert(static_cast<int>(arch::memory_type::normal) == ARCH_MT_NORMAL);
static_assert(static_cast<int>(arch::memory_type::device) == ARCH_MT_DEVICE);

static_assert(static_cast<int>(arch::trap_kind::breakpoint) == ARCH_TRAP_BREAKPOINT);
static_assert(static_cast<int>(arch::trap_kind::page_fault) == ARCH_TRAP_PAGE_FAULT);
static_assert(static_cast<int>(arch::trap_kind::illegal)    == ARCH_TRAP_ILLEGAL);
static_assert(static_cast<int>(arch::trap_kind::unaligned)  == ARCH_TRAP_UNALIGNED);
static_assert(static_cast<int>(arch::trap_kind::interrupt)  == ARCH_TRAP_INTERRUPT);
static_assert(static_cast<int>(arch::trap_kind::other)      == ARCH_TRAP_OTHER);

static_assert(static_cast<int>(arch::barrier::memory)   == ARCH_BARRIER_MEMORY);
static_assert(static_cast<int>(arch::barrier::store)    == ARCH_BARRIER_STORE);
static_assert(static_cast<int>(arch::barrier::complete) == ARCH_BARRIER_COMPLETE);
static_assert(static_cast<int>(arch::barrier::fetch)    == ARCH_BARRIER_FETCH);

// ── `kind_of` reads what a backend wrote ────────────────────────────────────
//
// ⚠️ A `constexpr` frame and not a call to the backend, because there is no
// backend here. What is being checked is the conversion `kind_of` performs, and
// the value it converts is the one the ABI says the backend stores.
constexpr arch::trap_frame make(int kind) {
    return arch::trap_frame{ 0, 0, 0, kind, 4 };
}
static_assert(arch::kind_of(make(ARCH_TRAP_BREAKPOINT)) == arch::trap_kind::breakpoint);
static_assert(arch::kind_of(make(ARCH_TRAP_INTERRUPT))  == arch::trap_kind::interrupt);
static_assert(arch::kind_of(make(ARCH_TRAP_OTHER))      == arch::trap_kind::other);

int main() {
    // Everything above is a compile-time assertion, so reaching here is the
    // result. Printing it keeps the test's output the same shape as the others'
    // and gives a runner something to see.
    std::printf("faces: the C header and the module declare one library\n");
    return 0;
}
