// The shape of the ABI, asserted where a disagreement is a compile error.
//
// ⭐ THE FRAME IS SHARED WITH ASSEMBLY, AND ASSEMBLY CANNOT BE TOLD IT IS
// WRONG.
//
// Each backend's trap entry stub reserves exactly `sizeof(arch_trap_frame)`
// bytes above the registers it saved and stores into fixed offsets within them.
// The assembler does not consult this header; the numbers are written in the
// `.S` file. If a member is added, reordered, or changes width, the stub keeps
// writing to the old offsets and the handler keeps reading the new ones, and
// what the caller sees is a plausible-looking value in the wrong field. There
// is no crash and no diagnostic.
//
// So the layout is frozen, and freezing it means writing it down somewhere a
// build can check. That is this file.
//
// ⚠️ THE ASSERTIONS ARE WIDTH-INDEPENDENT, AND THE LESSON COST A CI FAILURE
// ELSEWHERE. openkal asserted `offsetof(modified_ns) == sizeof(kal_uintptr)`,
// which is true on a 64-bit target and false on a 32-bit one, where a four-byte
// member is followed by four bytes of padding before an eight-byte one. The
// correct assertion names the byte offset. Nothing here may be written in terms
// of a pointer's width.

#include <mcpplibs/openarch.h>

#include <cstddef>
#include <cstdio>
#include <type_traits>

// ── Fixed widths everywhere ────────────────────────────────────────────────
//
// ⚠️ RESTATED HERE EVEN THOUGH `openarch/types.h` ALREADY ASSERTS IT, BECAUSE
// THE TWO ASSERTIONS ARE ABOUT DIFFERENT THINGS. That one says the typedef
// resolved to something eight bytes wide on the compiler that read the header.
// This one says the ABI a consumer sees through `<mcpplibs/openarch.h>` is that
// same thing — which is what the offsets below are computed against, and what a
// backend assembled by a different compiler has to agree with.
static_assert(sizeof(arch_u64) == 8,
              "the ABI's 64-bit type must be 64 bits");
static_assert(sizeof(arch_u32) == 4);

// ── The trap frame ─────────────────────────────────────────────────────────
static_assert(offsetof(arch_trap_frame, pc)    == 0);
static_assert(offsetof(arch_trap_frame, addr)  == 8);
static_assert(offsetof(arch_trap_frame, cause) == 16);
static_assert(offsetof(arch_trap_frame, kind)  == 24);
// ⚠️ 28 and not `offsetof(kind) + sizeof(int)`. Writing it in terms of another
// member's size states the same number twice and would follow a mistake rather
// than catch one.
static_assert(offsetof(arch_trap_frame, instr_len) == 28);
static_assert(sizeof(arch_trap_frame) == 32,
              "each backend's stub reserves this many bytes; see the .S files");
static_assert(alignof(arch_trap_frame) == 8);

// A trivially copyable, standard-layout aggregate: what assembly can construct
// and C can pass. A member with a constructor would make the stub's stores
// undefined rather than merely unusual.
static_assert(std::is_standard_layout_v<arch_trap_frame>);
static_assert(std::is_trivially_copyable_v<arch_trap_frame>);

// ── The vocabulary is dense and starts at zero ─────────────────────────────
//
// Both facts are relied on: `kind_of` is a `static_cast` rather than a switch,
// and each backend's `.S` and `.cpp` map an architecture's own cause codes onto
// these by table lookup. A gap would make the cast produce a value no
// enumerator names.
static_assert(ARCH_PERM_READ == 0 && ARCH_PERM_READ_WRITE_EXEC == 3);
static_assert(ARCH_MT_NORMAL == 0 && ARCH_MT_DEVICE == 1);
static_assert(ARCH_TRAP_BREAKPOINT == 0 && ARCH_TRAP_OTHER == 5);
static_assert(ARCH_BARRIER_MEMORY == 0 && ARCH_BARRIER_FETCH == 3);

// ── The context store ──────────────────────────────────────────────────────
//
// The ABI documents 128 bytes, 16-aligned, and each backend asserts its own
// register set fits. Nothing here can check the backends — there is none on the
// host — so what is checked is that the documented figure is expressible as an
// object a caller can declare.
struct alignas(16) context_store { unsigned char bytes[128]; };
static_assert(sizeof(context_store) == 128);
static_assert(alignof(context_store) == 16);

int main() {
    std::printf("abi shape: trap frame %zu bytes, context store %zu bytes\n",
                sizeof(arch_trap_frame), sizeof(context_store));
    return 0;
}
