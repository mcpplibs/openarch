# openarch

The architecture-mechanism layer: execution contexts, traps and address spaces,
as one interface over several instruction sets.

⚠️ **Status: 0.1.0 is a probe, not a layer.** One architecture is implemented and
one primitive of the two that decide whether this layer can exist. See
[What has not been shown](#what-has-not-been-shown).

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

This layer's viability is decided by two primitives, not by the count of
interfaces it eventually carries:

1. **Context switching** — implemented for riscv64 and aarch64.
2. **The page-table entry** — implemented for riscv64 and aarch64.

Everything else (per-CPU bases, barriers, timer ticks) varies far less between
architectures, and having them would say nothing about whether the layer holds.

### The criterion

> A single interface survives a second, genuinely different machine.

aarch64 is that machine: a different callee-saved set, a stricter stack
alignment rule, a link register instead of a return-address register, and — the
part that mattered — a different answer to where a fact lives.

`examples/switch` is **one probe source**. It is built for both targets and run
under both emulators, and the output is identical:

```
main: switching to task
task: arg=42
main: back, witness=7 before=1234
switch ok
```

The third line is the assertion that catches a half-correct backend: a switch
that saved the return address and the stack pointer and nothing else would
print the first two and corrupt its caller. `before` is a `volatile` local read
after the round trip.

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

## What is checked, and where

| | Checked by |
|---|---|
| The switch reaches, returns and preserves | One probe source, two emulators, in CI |
| The entry encodings | A host unit test that holds **both** encoders at once |
| The cross-compilation works from three systems | A build-only matrix on Linux, macOS and Windows |

⭐ The encoders are pure functions in per-architecture namespaces, reachable on
any host through the `probe-riscv64` and `probe-aarch64` features. That is what
lets a test assert that the two **agree** — for instance that neither ever marks
a user page executable by the kernel, which riscv64 gets structurally and
aarch64 needs two explicit bits to achieve. No single-target build could make
that comparison.

## The repository layout, and why it is three packages

```
openarch/
├── mcpp.toml              [workspace]
├── abi/                   the contract — headers only, depends on nothing
│   ├── mcpp.toml          openarch-abi
│   └── include/openarch/
│       ├── abi.h          what a backend implements
│       └── pte_encode.h   the pure entry encoders, host-callable
├── spec/                  the C++ modules over the contract
│   ├── mcpp.toml          openarch          → depends on openarch-abi
│   ├── src/               context.cppm  trap.cppm  cpu.cppm  pte.cppm
│   └── tests/             the encoders, asserted on the host
├── backends/              one package per instruction set
│   ├── riscv64/mcpp.toml  openarch-riscv64  → depends on openarch-abi
│   └── aarch64/mcpp.toml  openarch-aarch64  → depends on openarch-abi
└── examples/switch/       one probe source, run on every machine
```

⭐ **`spec/` owns every module and no instruction; `backends/` own instructions
and export no module.** Both are asserted in CI rather than left to the
directory names — until 0.3.1 the backends lived under `src/arch/<arch>/` in the
same package, and the layering was a convention held up by a path.

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

The first attempt put the header in `spec/` and had the backends depend on it,
while `spec/` pulled a backend through a `cfg` dependency so that a consumer
would never name its own architecture. mcpp rejected that:

```
error: dependency cycle through package 'openarch'
       while computing its build-cache key
```

The cycle is real. Giving up automatic backend selection would have made every
consumer write its architecture down; copying the header into each backend would
have created two files that must agree with no mechanism to make them. The
contract belongs to neither side, and once it is its own package both may depend
on it and neither depends on the other.

### What a consumer writes

```toml
[dependencies]
openarch = "0.3.1"
```

Nothing about the architecture. `spec/mcpp.toml` resolves the backend from the
target:

```toml
[target.'cfg(all(arch = "riscv64", os = "none"))'.dependencies]
openarch-riscv64 = { path = "../backends/riscv64" }
```

⚠️ `os = "none"` is part of the predicate. A hosted aarch64 build — a macOS
runner, for instance — has an operating system already, and matching on the
architecture alone once compiled ELF assembly for Mach-O.

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
| Traps, per-CPU bases, barriers, timer ticks | Not started, and deliberately: their shape depends on the two primitives above, and writing them before the gate was passed would have produced code that no test could run |
| A third architecture | x86_64 is the obvious one and is blocked on an emulator: no upstream publishes prebuilt `qemu-system-x86_64` for the five host targets the package index serves, which is the bar `qemu-arm` and `qemu-riscv` both meet |
| Page-table **walking** | Out of scope. Building an entry is mechanism; deciding where entries go is policy, and belongs to the kernel |
