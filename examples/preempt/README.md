# preempt — a preemptive scheduler on a machine with no address space

Two tasks that never yield are interleaved by a timer. Each proves it was
interrupted by observing a counter it did not advance itself.

```
$ mcpp run
preempt: openarch on a machine with no address space
preempt: both tasks observed preemption
```

## Why this example exists

`examples/switch` calls `arch_context_switch` and returns — a cooperative round
trip. It is the right probe for "does one interface hold across several
machines", and the wrong one for Cortex-M, because it never enters a trap. On
M-profile the trap group is exactly what changes: the vector table is an array
indexed by exception number rather than one entry point, and the hardware stacks
half the register file before a handler runs.

## ⭐⭐ What it found

**`arch_context_switch` is a cooperative primitive, and preemption needs
something openarch does not have.**

The first version of this example called `arch_context_switch` from PendSV. It
compiled, linked, booted, and reported that neither task had ever observed the
other. Nothing failed; the tasks were simply never interleaved, and only the
counter assertion distinguished that from success.

The reason is structural. Inside an exception handler the hardware has already
stacked `r0-r3`, `r12`, `LR`, `PC` and `xPSR` onto the interrupted task's stack
and is running the handler on MSP. Swapping the callee-saved registers of
whoever called `arch_context_switch` swaps the *handler's* state; the exception
return then unstacks a frame belonging to nobody.

The M-profile mechanism is different in kind: tasks run on PSP, the handler on
MSP, and a switch means saving `r4-r11` below the hardware frame on the outgoing
task's PSP, pointing PSP at the incoming task, restoring its `r4-r11`, and
returning with an `EXC_RETURN` that resumes Thread mode on PSP.

⚠️ **This is a gap in the layer, not a detail of this board.** openarch has a
primitive for *switch to another saved context* and none for *switch the context
this trap will return to*. Every architecture needs the second in order to
preempt, and every architecture spells it differently — which is the shape of
thing this layer exists to abstract. riscv64 edits `sepc` and the saved
registers; aarch64 edits `ELR_EL1` and `SP_EL0`; x86_64 edits the interrupt
frame. Naming that as a fifth interface group is a decision for the layer, taken
with more than one machine in view; it is recorded here rather than papered over.

## What openarch does supply here

* `arch_context_init` — the initial cooperative frames
* `arch_trap_enable_interrupts` / `arch_trap_interrupts_enabled` — PRIMASK
* the trap trampoline, which normalises a fault into the frame every handler on
  every architecture reads
* `arch_cpu_fence` — `dmb` / `dsb` / `isb`

## What this backend does not supply, and says so

`mcpplibs/openarch-cortex-m` declares `openarch-backend` and **not**
`openarch:address-space` or `openarch:percpu-register`. M-profile has a
region-based MPU with no page-table entry to construct, and no TPIDR-class
register. A kernel that needs either states it and is refused by name at
resolution:

```
error: no package provides capability 'openarch:address-space'
       required by 'mykernel'
```

rather than by a wall of `undefined reference to arch_pte_*` at link time.
