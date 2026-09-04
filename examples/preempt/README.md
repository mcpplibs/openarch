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

⚠️ **That was a gap in the layer, not a detail of this board — and it is closed
in 0.8.0.** openarch had a primitive for *switch to another saved context* and
none for *switch the context this trap will return to*. Every architecture needs
the second in order to preempt, and every architecture spells it differently,
which is the shape of thing this layer exists to abstract.

```c
void arch_trap_switch(arch_trap_frame* f, void* from, void* to);
```

Call it from a handler. It returns to the handler normally; the switch happens
when the trap does.

⭐⭐ **And the four machines do not implement it the same way, which is why it is
an interface function rather than a line of kernel code.**

| | How the trap resumes somewhere else |
|---|---|
| riscv64 | a cooperative switch inside the dispatcher, with `mepc` saved across it |
| aarch64 | the same, with `ELR_EL1` and `SPSR_EL1` saved across it |
| x86_64 | the same, and nothing to save — the `iret` frame travels with the stack |
| Cortex-M | **not that at all**: pend PendSV, which the hardware takes at exception exit |

The first three work because the trap runs on the interrupted context's own
stack. M-profile's does not — that is the whole of the finding above — so it
uses the mechanism the machine does offer. ⭐ And that mechanism turns out to be
the interface's own sentence about the two primitives: **PendSV pended from
thread mode is taken at once, and pended from a handler is taken when the
handler exits.** `arch_context_switch` and `arch_trap_switch` on this backend
are the same instruction sequence; the hardware supplies the difference.

## ⭐ What that did to this example

Thirty lines of hand-written PendSV assembly are gone. What is left is forty
lines of scheduling *policy*:

```c++
void on_tick(arch_trap_frame* f) {
    if (++g_ticks == 400) verdict();
    const int prev = g_current;
    g_current = (prev + 1) % kTasks;
    arch_trap_switch(f, g_ctx[prev], g_ctx[g_current]);
}
```

CI asserts the absence: a `grep` for `asm` in `src/main.cpp` fails the build. If
that code comes back, the primitive has stopped carrying its weight.

⚠️ **The board must still name `openarch_cm_pendsv` in vector slot 14.** The
table's location is a board fact and the hardware reads it by address, so the
package that implements the switch cannot install itself. A board that forgets
is not broken at link time — the switch simply never happens, which is why this
example asserts preemption rather than progress.

## What openarch does supply here

* `arch_context_init` — the initial frames, in the one layout both the
  cooperative and the preemptive path read
* `arch_trap_enable_interrupts` / `arch_trap_interrupts_enabled` — PRIMASK
* the trap trampoline, which normalises a fault into the frame every handler on
  every architecture reads
* `arch_cpu_fence` — `dmb` / `dsb` / `isb`
* `arch_trap_switch` and `openarch_cm_enter` — the switch itself, and the way
  into the first task

## What this backend does not supply, and says so

`mcpplibs/openarch-cortex-m` declares `openarch-backend` and
`openarch:preemption`, and **not** `openarch:address-space` or
`openarch:percpu-register`. M-profile has a
region-based MPU with no page-table entry to construct, and no TPIDR-class
register. A kernel that needs either states it and is refused by name at
resolution:

```
error: no package provides capability 'openarch:address-space'
       required by 'mykernel'
```

rather than by a wall of `undefined reference to arch_pte_*` at link time.
