/* QEMU aarch64 `virt` places a `-kernel` image at 0x40000000. */
ENTRY(_start)
SECTIONS {
    . = 0x40000000;
    .text   : { *(.text.entry) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { __bss_start = .; *(.bss*) *(COMMON) __bss_end = .; }
    . = ALIGN(16); . = . + 0x4000; __stack_top = .;
}
