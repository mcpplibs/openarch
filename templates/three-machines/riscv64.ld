/* QEMU riscv `virt` begins executing at 0x80000000 with `-bios none -kernel`. */
ENTRY(_start)
SECTIONS {
    . = 0x80000000;
    .text   : { *(.text.entry) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { __bss_start = .; *(.bss*) *(COMMON) __bss_end = .; }
    . = ALIGN(16); . = . + 0x4000; __stack_top = .;
}
