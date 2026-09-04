ENTRY(Reset_Handler)
MEMORY {
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}
SECTIONS {
  /* KEEP: the vector table is referenced by nothing — the hardware reads it by
     address — so --gc-sections would otherwise collect it. */
  .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } > FLASH
  .data : { *(.data*) } > RAM
  .bss  : { *(.bss*) *(COMMON) } > RAM
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
