ENTRY(_start)
SECTIONS
{
  . = 1M;
  .multiboot2 ALIGN(8) : { KEEP(*(.multiboot2)) }
  .text ALIGN(4096) : { *(.text*) }
  .rodata ALIGN(4096) : { *(.rodata*) }
  .data ALIGN(4096) : { *(.data*) }
  .bss ALIGN(4096) : { *(COMMON) *(.bss*) }
  __kernel_end = .;
  /DISCARD/ : { *(.eh_frame*) *(.note.GNU-stack) }
}
