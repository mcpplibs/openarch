/* openarch's types: the widths every other header in this package is written
 * in, stated once.
 *
 * ⚠️ THIS FILE EXISTS BECAUSE A WIDTH WAS AN ASSUMPTION IN FORTY PLACES AND A
 * STATEMENT IN NONE.
 *
 * Until 0.4.0 every 64-bit quantity in the ABI was spelled `unsigned long long`
 * at each use, with a comment at the top of `abi.h` explaining why it was not
 * `unsigned long`. That comment was correct and it was not a mechanism: the
 * property it described — "these are 64 bits" — was never checked anywhere, and
 * the one time it was violated, in `1UL << 53`, the violation was caught by
 * accident. The shift was inside a `constexpr`, so the compiler was forced to
 * evaluate it and reported `must be initialized by a constant expression`. Had
 * it been an ordinary expression, `unsigned long` being 32 bits on Windows
 * would have made it silently zero and produced page-table entries with every
 * high field missing, with no diagnostic at all.
 *
 * So the widths are named here, and the naming is what makes them assertable —
 * the `_Static_assert`s below are the whole point of the file, not decoration.
 * One place says what `arch_u64` is; one place checks it; every other file says
 * `arch_u64` and says nothing about widths.
 *
 * ⭐ WHY NOT `<stdint.h>`, WHICH DOES WORK HERE.
 *
 * Measured on llvm 22.1.8 for all three bare-metal targets with no sysroot at
 * all: `#include <stdint.h>` resolves and `uint64_t` is available, because the
 * header is the compiler's own rather than a C library's.
 *
 * The reason not to is the consumer this package exists to serve. openarch is
 * reached by the earliest code in a system, and some of that code is compiled
 * with `-nostdinc` — a C library being ported onto this layer is exactly that
 * consumer, and it has no header to include, not even the compiler's. openkal
 * made this decision first and for the same reason; matching it means the two
 * lowest layers of the ecosystem answer the question the same way.
 *
 * This file includes nothing.
 */
#ifndef OPENARCH_TYPES_H
#define OPENARCH_TYPES_H

/* ⚠️ TAKEN FROM THE COMPILER'S OWN SPELLING WHERE THERE IS ONE.
 *
 * Two of the three compilers this package is built with publish the type;
 * the third publishes the property the type is defined by and not the type,
 * so for that one the type is written from the property. Deriving it from
 * that compiler's own header instead would give this file an include, and
 * the consumer this file exists for has none. */
#if defined(__UINT64_TYPE__)
typedef __UINT32_TYPE__ arch_u32;
typedef __UINT64_TYPE__ arch_u64;
#elif defined(_MSC_VER)
typedef unsigned int     arch_u32;
typedef unsigned __int64 arch_u64;
#else
#  error "openarch requires a compiler that states a sixty-four bit type"
#endif

#if defined(__UINTPTR_TYPE__)
typedef __UINTPTR_TYPE__ arch_uptr;
#elif defined(_MSC_VER)
#  if defined(_WIN64)
typedef unsigned __int64 arch_uptr;
#  else
typedef unsigned int arch_uptr;
#  endif
#else
#  error "openarch requires a compiler that states the width of a pointer"
#endif

/* ── The checks ─────────────────────────────────────────────────────────────
 *
 * ⚠️ `arch_uptr` IS NOT ASSERTED TO BE EIGHT BYTES, AND THAT OMISSION IS
 * DELIBERATE. A page-table entry is 64 bits on every machine openarch serves
 * including a 32-bit one, so `arch_u64` has a fixed width; a pointer does not,
 * and `riscv32-none-elf` is a target this repository intends to reach. An
 * assertion that a pointer is eight bytes would pass on every machine tested
 * today and would be exactly the mistake openkal made in `fs.h`, where
 * `offsetof(modified_ns) == sizeof(kal_uintptr)` held on 64-bit targets and
 * failed on 32-bit ones because a four-byte member is followed by four bytes of
 * padding. What is asserted about a pointer is only that it fits its own type.
 *
 * Written for both languages: this header is included from C by a backend a
 * vendor supplies and from C++ by everything in this repository. */
#if defined(__cplusplus)
#  define OPENARCH_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#  define OPENARCH_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
/* A compiler older than C11 gets no check rather than a broken one. The
 * declaration below is a no-op that keeps the macro usable as a statement. */
#  define OPENARCH_STATIC_ASSERT(cond, msg) struct openarch_unused_##__LINE__
#endif

OPENARCH_STATIC_ASSERT(sizeof(arch_u32) == 4,
                       "arch_u32 must be exactly four bytes");
OPENARCH_STATIC_ASSERT(sizeof(arch_u64) == 8,
                       "arch_u64 must be exactly eight bytes: a page-table "
                       "entry is 64 bits on every machine openarch serves");
OPENARCH_STATIC_ASSERT(sizeof(arch_uptr) == sizeof(void*),
                       "arch_uptr must hold a pointer");

#endif /* OPENARCH_TYPES_H */
