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

## The gate

This layer's viability is decided by two primitives, not by the count of
interfaces it eventually carries:

1. **Context switching** — implemented here for riscv64.
2. **The page-table entry** — not started.

Everything else (per-CPU bases, barriers, timer ticks) varies far less between
architectures, and having them says nothing about whether the layer holds.

## Why one repository

openkal and openhal have open implementer sets and depend on third parties
arriving. openarch's set is bounded and small, and nobody implements the
aarch64 context switch differently. The interface and its implementations must
therefore co-evolve: the only way to learn that an interface is wrong is to add
the second architecture, and splitting the repository would slow exactly the
loop that decides whether the layer is viable.

## What has not been shown

⚠️ **The gate has not been passed, and one architecture cannot pass it.**

The criterion is that a single interface survives a second, genuinely different
machine. With riscv64 alone there is nothing for the abstraction to fail
against — an interface shaped around one instruction set always fits it.

| | Status |
|---|---|
| riscv64 context switch | Implemented; a probe switches between two contexts and returns |
| Zero-cost criterion | The switch is a naked assembly symbol reached by an ordinary `call`; the emitted sequence is that call and nothing else |
| **Second architecture** | **Blocked.** `aarch64-none-elf` has no row in mcpp's target table and no emulator in the package index |
| Address spaces | Not started. Memory attributes are the expected failure point: x86 selects them through PAT, aarch64 through MAIR, riscv writes them into the entry |
| Traps, per-CPU, ticks, boot | Not started, and deliberately: see the gate |

Publishing a version that implemented seven interfaces for three architectures
would produce a great deal of code that no test could run, which is the failure
this project's own design record warns against most directly.
