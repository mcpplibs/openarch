/* The x86_64 half of the probe's machine: reaching the state openarch assumes.
 *
 * ⭐ THIS FILE HAS NO COUNTERPART ON THE OTHER TWO MACHINES, AND ITS EXISTENCE
 * IS THE THIRD ARCHITECTURE'S MOST VISIBLE DISAGREEMENT.
 *
 * riscv64 and aarch64 begin executing 64-bit instructions at reset. A probe for
 * them sets a stack pointer and calls C. x86_64 begins in 16-bit real mode, and
 * a multiboot loader hands over in 32-bit protected mode with paging off — so
 * everything openarch's backend assumes (long mode, a 64-bit code segment, a
 * stack) has to be constructed first, in about a hundred instructions that
 * belong to no layer of the library.
 *
 * ⚠️ AND THAT IS WHY THEY ARE HERE RATHER THAN IN THE BACKEND. `backends/x86_64`
 * states that it assumes long mode is already entered, for the same reason no
 * backend carries a linker script: how a particular image reaches that state is
 * a property of how it is LOADED, and a multiboot image, a UEFI application and
 * a bzImage arrive by three different routes. A backend that picked one would
 * be a bootloader wearing a machine layer's name.
 */

/* ── The multiboot header ──────────────────────────────────────────────────
 *
 * Bit 16 is the "a.out kludge": the header carries the load addresses itself and
 * the loader never parses the ELF. That is what lets QEMU load this image at
 * all — its multiboot loader accepts only a 32-bit ELF, and this one is 64-bit.
 * See x86_64.ld for why `SIZEOF_HEADERS` makes the address arithmetic come out.
 *
 * The checksum is defined as the value that makes the first three fields sum to
 * zero in 32-bit arithmetic.                                                   */
.set MB_MAGIC,    0x1BADB002
.set MB_FLAGS,    0x00010000            /* bit 16: the a.out kludge          */
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

    .section .multiboot,"a",@progbits
    .align 4
mb_header:
    .long MB_MAGIC
    .long MB_FLAGS
    .long MB_CHECKSUM
    /* The five address fields the kludge requires. With them the loader copies
     * bytes and jumps; it never looks at the ELF, which is what lets a 64-bit
     * image be loaded by a loader that only parses 32-bit ones. */
    .long mb_header                     /* header_addr                       */
    .long __load_start                  /* load_addr                         */
    .long __load_end                    /* load_end_addr                     */
    .long __bss_end                     /* bss_end_addr — zeroed by the loader */
    .long _start                        /* entry_addr                        */

/* ── The 32-bit entry ──────────────────────────────────────────────────────*/
    .code32
    .section .text.entry,"ax",@progbits
    .globl _start
    .type  _start, @function
_start:
    cli
    movl    $boot_stack_top, %esp

    /* ⚠️ ZERO `.bss` BEFORE ANYTHING READS IT, AND THE PAGE TABLES ARE WHY
     * THIS IS NOT OPTIONAL HERE. On the other two machines an unzeroed `.bss`
     * gives the probe a wrong counter. Here it gives the processor a page table
     * full of whatever the previous occupant of that memory left, and the walk
     * reads those bytes as present entries pointing at addresses that are not
     * page tables. */
    movl    $__bss_start, %edi
    movl    $__bss_end, %ecx
    subl    %edi, %ecx
    xorl    %eax, %eax
    rep stosb

    /* PML4[0] → PDPT, PDPT[0] → PD. Present, writable. */
    movl    $pdpt, %eax
    orl     $0x03, %eax
    movl    %eax, pml4

    movl    $pd, %eax
    orl     $0x03, %eax
    movl    %eax, pdpt

    /* PD[i] maps a 2 MiB page at i * 2 MiB, identity, for the first gigabyte.
     *
     * ⚠️ 2 MiB PAGES RATHER THAN 1 GiB ONES. A single PDPT entry with `PS` set
     * would map the whole gigabyte in one line of code, and 1 GiB pages are an
     * OPTIONAL feature — `cpuid` leaf 0x80000001, `edx` bit 26. Emulators and
     * processors that lack it do not fault on the attempt; they interpret the
     * `PS` bit as reserved and take a page fault on the first access, from
     * inside the code that has just enabled paging. */
    xorl    %ecx, %ecx
1:  movl    %ecx, %eax
    shll    $21, %eax
    orl     $0x83, %eax             /* present | writable | page-size */
    movl    %eax, pd(,%ecx,8)
    incl    %ecx
    cmpl    $512, %ecx
    jb      1b

    movl    $pml4, %eax
    movl    %eax, %cr3

    /* CR4.PAE. Long mode requires it; without it the mode switch below is a
     * #GP rather than a transition. */
    movl    %cr4, %eax
    orl     $(1 << 5), %eax
    movl    %eax, %cr4

    /* EFER: LME to enable long mode, NXE so that bit 63 of a page-table entry
     * means "no execute" rather than "reserved".
     *
     * ⚠️ NXE IS NOT DECORATION HERE. `openarch.pte`'s encoder sets that bit for
     * a non-executable mapping. With NXE clear the processor treats it as a
     * reserved bit and every access through such an entry is a page fault —
     * which is the encoder producing entries that are correct by the manual and
     * fatal on the machine. The backend's `install_memory_attributes` sets it
     * too, for a kernel that reaches this layer by some other route. */
    movl    $0xC0000080, %ecx
    rdmsr
    orl     $((1 << 8) | (1 << 11)), %eax
    wrmsr

    /* CR0.PG. Paging on; the processor is in long mode from this instruction,
     * but still executing in a 32-bit compatibility segment. */
    movl    %cr0, %eax
    orl     $(1 << 31), %eax
    movl    %eax, %cr0

    lgdt    gdt64_descriptor
    ljmp    $0x08, $long_mode_entry
    .size   _start, . - _start

/* ── The 64-bit entry ──────────────────────────────────────────────────────*/
    .code64
    .section .text.entry64,"ax",@progbits
    .type   long_mode_entry, @function
long_mode_entry:
    /* ⚠️ THE DATA SEGMENTS STILL HOLD 32-BIT SELECTORS AND MUST BE RELOADED.
     * In long mode the processor ignores the base and limit of `ds`, `es` and
     * `ss`, which makes it easy to believe they need no attention — but their
     * SELECTORS are still checked, and one left pointing at a descriptor the
     * new GDT does not define faults on the first stack operation. */
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs

    movq    $__stack_top, %rsp
    xorq    %rbp, %rbp

    call    kmain
1:  hlt
    jmp     1b
    .size   long_mode_entry, . - long_mode_entry

/* ── The descriptor table ──────────────────────────────────────────────────
 *
 * Three entries: the required null descriptor, a 64-bit code segment, and a
 * data segment.
 *
 * ⚠️ THE `L` BIT — BIT 53, THE `0xA` IN THE SIXTH BYTE — IS WHAT MAKES THE CODE
 * SEGMENT 64-BIT, AND `D` MUST BE CLEAR BESIDE IT. A descriptor with both set
 * is reserved: the far jump below faults instead of transferring, and the fault
 * is delivered through an IDT that does not exist yet, so the machine triple-
 * faults and the emulator resets with nothing printed.                        */
    .section .rodata.gdt,"a",@progbits
    .align 8
gdt64:
    .quad 0x0000000000000000        /* null */
    .quad 0x00AF9A000000FFFF        /* code: P, DPL0, execute/read, L=1, D=0 */
    .quad 0x00AF92000000FFFF        /* data: P, DPL0, read/write            */
gdt64_end:

gdt64_descriptor:
    .word gdt64_end - gdt64 - 1
    .long gdt64

/* ── Storage ───────────────────────────────────────────────────────────────*/
    .section .bss
    .align 4096
pml4:            .skip 4096
pdpt:            .skip 4096
pd:              .skip 4096
boot_stack:      .skip 4096
boot_stack_top:

    .section .note.GNU-stack,"",@progbits
