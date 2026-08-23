# openarch

The architecture-mechanism layer: execution contexts, traps and address spaces,
as one interface over several instruction sets.

**Status: 0.4.0.** Four interfaces — contexts, page-table entries, traps,
per-CPU state and barriers — over **three** instruction sets: riscv64, aarch64
and x86_64. One probe source builds and runs on all three and produces
byte-identical output.

## What this is, and what it is not

| | |
|---|---|
| openkal | what a program asks of a kernel |
| openarch | what a kernel asks of a machine |
| openhal | what a program asks of a device |

openarch carries **mechanism and no policy**. It can switch contexts; it does
not schedule. It can install a trap vector and construct a page-table entry; it
does not decide what a fault means or how memory is laid out.

## The gate, and what passing it cost

This layer's viability was decided by two primitives, not by the count of
interfaces it eventually carries:

1. **Context switching**
2. **The page-table entry**

Everything else (per-CPU bases, barriers, traps) varies far less between
architectures, and having them first would have said nothing about whether the
layer holds.

### The criterion

> A single interface survives a second, genuinely different machine.

aarch64 was that machine: a different callee-saved set, a stricter stack
alignment rule, a link register instead of a return-address register, and — the
part that mattered — a different answer to where a fact lives.

### And then a third, which is what turned the gate into evidence

⭐ riscv64 and aarch64 are both load/store RISC machines with a weak memory
model and a fixed instruction width. An interface that fits both may fit because
it is right or because they are alike, and no amount of testing on those two
distinguishes the cases.

**x86_64 is neither.** Variable-length instructions; total store order, under
which three of the four barriers need no instruction at all; an interrupt
mechanism that is a table of 256 gates rather than a base register; and a
console reached by `out` rather than by a store, so no pointer can name it. What
survives all three is an abstraction.

`examples/switch` is **one probe source**. It is built for both targets and run
under both emulators, and the output is identical:

```
main: switching to task
task: arg=42
main: back, witness=7 before=1234
trap: raising
trap: back, witness=1
cpu: percpu round-trips
cpu: tls round-trips
cpu: the two slots are distinct
cpu: four barriers accepted
switch ok
```

The third line is the assertion that catches a half-correct backend: a switch
that saved the return address and the stack pointer and nothing else would
print the first two and corrupt its caller. `before` is a `volatile` local read
after the round trip.

⭐ **`the two slots are distinct` is the line that says a comment became a
check.** There are two pointer slots — one the PROCESSOR owns (`arch_cpu_percpu`,
where a kernel keeps what describes this hart) and one the running CONTEXT owns
(`arch_cpu_tls`, where the toolchain expects thread-local storage). On aarch64
and x86_64 they are different registers and the distinction is free. On riscv64
`tp` was being used for both, and this file's own backend carried a comment
saying so and a constraint — *"must not be compiled into anything that also uses
a thread pointer"* — that nothing enforced.

Measured 2026-08-23: something does. An openkal implementation on this
architecture writes `tp` so a program's `thread_local` works. The probe printed
`cpu: the two slots ALIAS`, the per-CPU pointer moved to `mscratch` — the
register this privilege level provides for it — and the line now reads
`distinct` on all three.

### What the second architecture actually changed

Two things, and neither was visible with one backend.

**A saved context is integer state, and now it says so.** riscv64 saves none of
`fs0`-`fs11`; that looked like a property of that backend until AAPCS64 named
`d8`-`d15` callee-saved as well and the arithmetic settled it — saving them
would need 168 bytes, and the interface reserves 128. The reserved size had
already assumed a contract that nothing had stated.

**A page-table entry does not always carry its own meaning.** riscv64 writes the
memory type into the entry (Svpbmt, bits [62:61]). aarch64 writes a three-bit
*index* into `MAIR_EL1`, so an aarch64 entry says "attribute number one" and
what that means is a property of the CPU. An interface offering
`memory_type::device` therefore cannot be satisfied by encoding alone. This
layer owns the register: `install_memory_attributes()` writes a canonical MAIR
on aarch64 and does nothing on riscv64, and a kernel calls it once during boot
on both.

That is the shape of the finding the gate exists to produce — not "the interface
was wrong", but "the interface was under-specified in a way one machine could
not reveal".

### What the third architecture changed

**The `MAIR_EL1` decision stopped being aarch64's exception.** With two machines
it was one against one, and "this layer owns the attribute register" could
fairly be called a workaround. x86_64 does the same thing: `PWT`, `PCD` and
`PAT` are three scattered bits forming an index into `IA32_PAT`, a
model-specific register. The majority is now two to one the other way, and
owning the register is the general case.

⚠️ Its version of the rule is *stricter*. An unprogrammed `MAIR_EL1` field reads
as the most restrictive memory type, so a too-early aarch64 mapping is slow and
correct. `IA32_PAT`'s reset value has **write-through** at index 1, so a device
mapping used before `install_memory_attributes()` is cached rather than
uncached: writes reach the device eventually, at a time the program did not
choose, and nothing faults.

**`pc` does not mean the same thing on every machine, and the interface absorbed
that rather than restating it.** Both RISC machines report the address of the
instruction that trapped. x86_64 divides its exceptions into *faults*, which do
that, and *traps*, which report the address of the NEXT instruction — and `int3`,
the breakpoint `instr_len` exists to step over, is a trap. The backend
normalises it, so `f->pc += f->instr_len` resumes in the same place on all
three. The alternative was to tell every handler ever written, including the
ones that will only ever run on RISC machines, that `pc` means something
different here.

**One interface promise turned out to be unexpressible in an entry.** riscv
qualifies its `X` bit by `U`, and aarch64 has separate `PXN` and `UXN` bits, so
on both "a user mapping is not kernel-executable" is a property of the encoding.
x86_64 has one `NX` bit covering every privilege level. The rule comes from
`CR4.SMEP` instead, which `install_memory_attributes()` sets — the same shape of
answer as `MAIR_EL1`, arrived at for a different reason.

## What is checked, and where

| | Checked by |
|---|---|
| The switch reaches, returns and preserves; traps classify; per-CPU round-trips; four barriers are accepted | One probe source, three emulators, in CI |
| The entry encodings | A host unit test that holds **all three** encoders at once |
| The two faces declare one library | A host test of `static_assert`s, on a machine with no backend at all |
| The ABI's frozen layout | `tests/abi_shape.cpp`, in byte offsets rather than in `sizeof` of another member |
| The interface owns no instruction; the backends export no module | Two greps in CI, because until 0.3.1 the layering was a convention held up by a path |
| The cross-compilation works from three systems | A build-only matrix on Linux, macOS and Windows |

⭐ The encoders are pure `inline` functions in per-architecture namespaces, so a
host build holds all three at once. That is what lets a test assert that they
**agree** — for instance that neither RISC machine ever marks a user page
executable by the kernel, which riscv64 gets structurally and aarch64 needs two
explicit bits to achieve. No single-target build could make that comparison.

⚠️ It is also what lets a test record where they *cannot* agree. x86_64 has one
`NX` bit for every privilege level, so the same assertion is not writable for
it; the test states that instead, and asserts the weaker thing that is true —
that an executable user page and an executable kernel page differ only in `U/S`.

## The repository layout: one package, two faces, three backends

```
openarch/
├── mcpp.toml              [package] openarch  AND  [workspace]
├── src/                   the C++ face — context, pte, trap, cpu,
│                          re-exported by module `mcpplibs.openarch`
├── tests/                 what the two faces must agree about
├── abi/                   the contract — headers only, depends on nothing
│   ├── mcpp.toml          openarch-abi
│   └── include/
│       ├── mcpplibs/openarch.h   the C face, entire
│       └── openarch/
│           ├── types.h    the widths, named once and asserted once
│           ├── abi.h      what a backend implements
│           └── pte_encode.h   the pure entry encoders, host-callable
├── backends/              one package per instruction set
│   ├── riscv64/mcpp.toml  openarch-riscv64  → provides "openarch-backend"
│   ├── aarch64/mcpp.toml  openarch-aarch64  → provides "openarch-backend"
│   └── x86_64/mcpp.toml   openarch-x86-64   → provides "openarch-backend"
└── examples/switch/       one probe source, run on every machine
```

⭐ **The root is both a package and a workspace, and that is what makes a
consumer's side one line.** A virtual workspace — `[workspace]` with no
`[package]` — would put the interface in a member directory, and
`openarch = "0.4.0"` would have to name it.

⭐ **The root owns every module and no instruction; the backends own
instructions and export no module.** Both are asserted in CI rather than left to
the directory names — until 0.3.1 the backends lived under `src/arch/<arch>/` in
the same package, and the layering was a convention held up by a path.

### The two faces

A consumer writes one dependency line and then reaches the layer either way:

```c
#include <mcpplibs/openarch.h>   /* C, and C++ that wants the C names */
```
```cpp
import mcpplibs.openarch;        // the four modules, re-exported
```

They are two spellings of one library rather than two declarations that line up.
The module's `trap_frame` **is** `::arch_trap_frame` — a `using`, not a
lookalike — and its enumerations are *defined from* the contract's:
`illegal = ARCH_TRAP_ILLEGAL`. `tests/faces.cpp` checks the derivation, which is
a weaker thing to have to check than an agreement.

⚠️ The C face is not a leftover. openarch is what a kernel's earliest code uses,
and that code is frequently not C++: a boot stub in C, a vendor's board file, a
runtime that speaks the C ABI and nothing else.

### Which backend, and whose

The backend arrives through a **feature**, and three questions that had been
answered by one mechanism are now separate:

| The consumer wants | What its manifest says |
|---|---|
| the backend for its target | `openarch = "0.4.0"` |
| a particular one | `{ version = "0.4.0", default-features = false, features = ["backend-riscv64"] }` |
| **its own implementation** | `{ version = "0.4.0", default-features = false, features = ["backend-external"] }` plus a package that `provides = ["openarch-backend"]` |

`backend-auto` is on by default and resolves per target.
`backend-external` names no package: it *requires the capability*, so a graph
with no provider fails at configure time saying so, rather than at link time
naming a mangled symbol. This is the shape `std-freestanding` uses for its
allocator, so the ecosystem has one pattern for "a default that stays
replaceable" rather than two.

⚠️ **`backend-auto` deliberately does NOT require the capability**, and the first
version of the table had it do so. Features are additive and a `requires` is
unconditional even when the `feature-deps` that satisfy it are
target-conditional — so requiring it made a host build of this package's own
tests impossible:

```
error: no package provides capability 'openarch-backend' required by 'openarch'
```

A hosted target having no backend is a fact about the target, not an error the
consumer can act on. `import mcpplibs.openarch` still compiles there, and the
encoders are header-only, which is what `tests/` runs on.

### What the split forced

A module implementation unit must live in the same package as the module it
implements. `openarch.pte`, `openarch.trap` and `openarch.cpu` were implemented
that way, so separating the packages was impossible until their boundary became
a **C ABI** — which is what `openarch.context` had always used, and what openkal
uses throughout.

⚠️ The gain is not tidiness. A specification whose boundary is a C ABI can be
implemented by something that is not a C++ module: an assembler file, a vendor's
blob, a second backend for the same instruction set at a different privilege
level. riscv needs that last one — the backend here traps into M-mode, and a
kernel under SBI traps into S-mode, which is the same mechanism with a different
prefix on every register.

### Why `abi/` is its own package

The first attempt put the header in the interface package and had the backends
depend on it, while the interface pulled a backend so that a consumer would
never name its own architecture. mcpp rejected that:

```
error: dependency cycle through package 'openarch'
       while computing its build-cache key
```

The cycle is real. Giving up automatic backend selection would have made every
consumer write its architecture down; copying the header into each backend would
have created two files that must agree with no mechanism to make them. The
contract belongs to neither side, and once it is its own package both may depend
on it and neither depends on the other.

### How the target chooses

```toml
[target.'cfg(all(arch = "riscv64", os = "none"))'.feature-deps.backend-auto]
openarch-riscv64 = { path = "backends/riscv64" }
```

The feature decides *whether* a backend is linked; the predicate decides *which*.

⚠️ `os = "none"` is part of every predicate. A hosted aarch64 build — a macOS
runner, for instance — has an operating system already, and matching on the
architecture alone once compiled ELF assembly for Mach-O.

## Starting from it

```
mcpp new mykernel --template openarch
cd mykernel
mcpp run --target riscv64-none-elf
mcpp run --target aarch64-none-elf
mcpp run --target x86_64-none-elf
```

The template is the probe: one `src/main.cpp`, three `machine_<arch>.cpp` files
carrying the console and the power-off register, three linker scripts, and — on
x86_64 only — the hundred instructions that reach long mode. ⭐ It is worth
building for two targets and comparing the output rather than taking the claim
on trust.

⚠️ CI renders the template by hand rather than through `mcpp new`. The
scaffolder resolves `--template` from the index and takes no path, so asking it
for a template a commit ADDS would resolve the previously published version and
fail on a template that version does not carry. What belongs here is the
template's content; that the scaffolder can fetch it is mcpp's own concern.

## Why one repository


openkal and openhal have open implementer sets and depend on third parties
arriving. openarch's set is bounded and small, and nobody implements the
aarch64 context switch differently. The interface and its implementations must
therefore co-evolve: the only way to learn that an interface is wrong is to add
the second architecture, and splitting the repository would slow exactly the
loop that decides whether the layer is viable.

## What has not been done

| | Status |
|---|---|
| Timer ticks | **Answered, not implemented.** `examples/clock-study` reads a counter on all three machines directly and `FINDING.md` records the result: all three provide a monotonic counter with one address-free instruction, and only aarch64 reports how fast it runs. So `counter()` belongs here and `frequency()` and `set_deadline()` do not — the interface is narrower than the one that would have been written first |
| Page-table **walking** | Out of scope. Building an entry is mechanism; deciding where entries go is policy, and belongs to the kernel |
| A second backend for one ISA | The arrangement now supports it — `backend-riscv64` names a backend rather than an architecture — and riscv will want it: this backend traps into M-mode, and a kernel under SBI traps into S-mode |
