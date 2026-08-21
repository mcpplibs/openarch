/* QEMU's x86 machines load a multiboot `-kernel` image at the addresses its
 * multiboot header names.
 *
 * ⚠️ QEMU'S MULTIBOOT LOADER ACCEPTS ONLY A 32-BIT ELF, AND THIS IMAGE IS A
 * 64-BIT ONE. Measured, with the image linked the obvious way:
 *
 *     qemu-system-x86_64: Cannot load x86-64 image, give a 32bit one.
 *
 * There is no way to produce an ELF32 here — the code is x86-64 and an ELF's
 * class is a property of the whole file — so the image is loaded through
 * multiboot's OTHER route: the "a.out kludge", flag bit 16, under which the
 * header carries explicit load addresses and the loader never parses the ELF
 * at all. GRUB2 accepts ELF64 and does not need this; QEMU does.
 *
 * ⭐ `SIZEOF_HEADERS` IS WHAT MAKES THE KLUDGE'S ARITHMETIC COME OUT.
 *
 * The loader computes the file offset it starts copying from as
 *
 *     header_addr - load_addr
 *
 * so that difference must equal the multiboot header's ACTUAL offset in the
 * file. Starting the image at `0x100000 + SIZEOF_HEADERS` makes the ELF
 * headers occupy exactly the bytes between `load_addr` and `header_addr`:
 * offset and address stay congruent, the linker emits no padding, and the
 * difference is the offset by construction rather than by a number somebody
 * has to keep correct.
 *
 * Linking it the plain way — `. = 0x100000` — put `.multiboot` at file offset
 * 0x1000 with address 0x100000, so `load_addr` would have had to be 0xFF000,
 * inside the legacy BIOS window where a write is discarded.
 */
ENTRY(_start)
SECTIONS {
    __load_start = 0x100000;
    . = __load_start + SIZEOF_HEADERS;

    .multiboot : { KEEP(*(.multiboot)) }
    .text      : { *(.text.entry) *(.text.entry64) *(.text*) }
    .rodata    : { *(.rodata*) }
    .data      : { *(.data*) }
    __load_end = .;

    /* ⚠️ 4 KiB, BECAUSE THE PAGE TABLES LIVE HERE. The boot stub builds a
     * PML4, a PDPT and a PD in `.bss`, and the processor requires each to be
     * page-aligned; a table at a lower alignment is not diagnosed, it is walked
     * with the low bits of its address read as flags. */
    .bss ALIGN(4096) : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    . = ALIGN(16); . = . + 0x4000; __stack_top = .;
}
