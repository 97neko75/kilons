ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
  . = 0x32000000;
  .text : { *(.text*) *(.rodata*) } :text
  . = ALIGN(4096);
  .data : { *(.data*) *(.bss*) *(COMMON) } :data
  /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) }
}
