// openarch.pte — the page-table entry.
//
// WHY THIS IS THE SECOND GATE PRIMITIVE AND NOT THE FIFTH INTERFACE
//
// Context switching asks whether an abstraction can hide a register file. This
// asks something harder: whether it can hide a disagreement about where a fact
// LIVES. The two machines place the same information in different places, and
// one of them places part of it outside the entry entirely.
//
// ⭐ WHAT THE SECOND ARCHITECTURE ACTUALLY BROKE, MEASURED RATHER THAN
// ANTICIPATED.
//
// riscv64 writes the memory type INTO the entry: with Svpbmt, bits [62:61] say
// PMA, NC or IO, and an entry is therefore self-describing. aarch64 writes a
// three-bit INDEX into the entry — `AttrIndx`, bits [4:2] — and the index
// selects one of eight byte-wide fields in `MAIR_EL1`, a system register. An
// aarch64 entry does not say what kind of memory it maps; it says "attribute
// number three", and what number three means is a property of the CPU at the
// moment of the access.
//
// An interface offering `memory_type::device` therefore cannot be implemented
// on aarch64 by encoding alone. Two ways out, and only one of them is this
// layer's:
//
//   * expose the index and let the kernel own MAIR — which pushes an aarch64
//     concept into every caller including the ones that will never run on it;
//   * own the attribute register, so that `device` means the same thing on
//     both machines.
//
// The second is taken. `install_memory_attributes()` programs a canonical MAIR
// layout on aarch64 and does nothing on riscv64, and a kernel calls it once
// during boot on both. That is mechanism rather than policy: the layout is not
// a choice a kernel makes differently, it is the register a machine requires
// before `device` is meaningful.
//
// ⚠️ The consequence is a rule this interface now carries: on aarch64 an entry
// built by `make_leaf` is only correct for a CPU on which
// `install_memory_attributes` has run. Stating it is the price of hiding it.

export module openarch.pte;

export namespace arch {

// What an access may do. Ordered so that a wider permission is a superset of a
// narrower one, which is what lets a caller compare them.
enum class perm {
    read,
    read_write,
    read_exec,
    read_write_exec,
};

// What kind of memory is mapped.
//
// Two values and not more, because these are the two that both machines
// distinguish and that a kernel cannot avoid: ordinary memory, which may be
// cached, reordered and speculatively read, and device memory, which may not.
// A third value would have to mean the same thing on every machine openarch
// serves, and "write-combining" does not.
enum class memory_type {
    normal,
    device,
};

// One leaf entry. Opaque in the same sense as `context`: the value is the
// architecture's, and a consumer that read its fields would be writing code
// that only builds on one machine.
struct pte {
    unsigned long bits;
};

// Builds a leaf entry mapping the physical address `phys`.
//
// `phys` must be page-aligned; the low bits are the entry's own and are
// discarded rather than trusted. `user` marks the mapping reachable from
// unprivileged code.
//
// ⚠️ The entry is VALID and ACCESSED. Neither machine faults on a first touch
// in the arrangement this layer serves, and leaving the accessed bit clear
// costs a fault on every machine to record something nothing here reads.
pte make_leaf(unsigned long phys, perm p, memory_type mt, bool user) noexcept;

// Whether an entry maps anything.
bool is_valid(pte e) noexcept;

// The physical address an entry maps, or zero if it maps nothing.
unsigned long phys_of(pte e) noexcept;

// Programs whatever the machine needs before `memory_type` is meaningful.
//
// Does nothing on riscv64, where the type is in the entry. On aarch64 it writes
// `MAIR_EL1` with the layout `make_leaf` encodes indices into. A kernel calls
// it once per CPU during boot, on every machine, and does not need to know
// which machines need it.
//
// ⚠️ Must run at EL1 or above on aarch64. There is no arrangement in which a
// caller of this layer is not privileged, so this is a note rather than a
// parameter.
void install_memory_attributes() noexcept;

}  // namespace arch
