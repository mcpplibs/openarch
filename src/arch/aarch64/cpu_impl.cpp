// openarch.cpu — the aarch64 backend.
module openarch.cpu;

namespace arch {

// `TPIDR_EL1` is architectural and readable only at EL1 or above, which is
// where a kernel is. Unlike riscv's `tp` it cannot collide with a hosted
// program's thread pointer, because a hosted program cannot reach it.
void* percpu() noexcept {
    void* p;
    asm volatile("mrs %0, tpidr_el1" : "=r"(p));
    return p;
}

void set_percpu(void* p) noexcept {
    asm volatile("msr tpidr_el1, %0" :: "r"(p));
}

void fence(barrier b) noexcept {
    switch (b) {
        // ⚠️ THREE INSTRUCTIONS, NOT ONE WITH OPERANDS, AND THE DIFFERENCE
        // BETWEEN THE FIRST TWO IS THE REASON `complete` EXISTS IN THE
        // INTERFACE.
        //
        // `dmb` orders accesses as other agents observe them; it does not wait
        // for them. `dsb` waits. A kernel that writes a device register and
        // then expects the device to have seen it needs the second, and a
        // caller that had only `memory` would silently get the first.
        case barrier::memory:   asm volatile("dmb sy" ::: "memory"); break;
        case barrier::store:    asm volatile("dmb st" ::: "memory"); break;
        case barrier::complete: asm volatile("dsb sy" ::: "memory"); break;
        // `isb` is not a memory barrier at all: it discards what the fetch path
        // has already decided. riscv spells the same intent `fence.i`, which
        // sits in the fence family there and does not here.
        case barrier::fetch:    asm volatile("isb" ::: "memory"); break;
    }
}

}  // namespace arch
