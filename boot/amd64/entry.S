/* Multiboot2 enters in 32-bit protected mode with paging disabled. */
.section .multiboot2,"a"
.align 8
header_start:
.long 0xe85250d6
.long 0
.long header_end - header_start
.long -(0xe85250d6 + 0 + (header_end - header_start))
.short 0
.short 0
.long 8
header_end:

.section .text,"ax"
.code32
.global _start
.extern kernel_main
_start:
    cli
    movl $stack_top, %esp
    cmpl $0x36d76289, %eax
    jne boot_fail
    /* Preserve Multiboot2 information pointer for the C entry. */
    movl %ebx, boot_info_ptr
    /* Build identity mapping of first 1 GiB with 2 MiB pages. */
    movl $pml4, %edi
    movl $pdpt, %eax
    orl $3, %eax
    movl %eax, (%edi)
    movl $pdpt, %edi
    movl $pd, %eax
    orl $3, %eax
    movl %eax, (%edi)
    xorl %ecx, %ecx
1:
    movl %ecx, %eax
    shll $21, %eax
    orl $0x83, %eax
    movl %eax, pd(,%ecx,8)
    incl %ecx
    cmpl $512, %ecx
    jne 1b
    movl $pml4, %eax
    movl %eax, %cr3
    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4
    movl $0xc0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr
    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0
    lgdt gdt_pointer
    ljmp $0x08, $long_mode_entry
boot_fail:
    cli
2:  hlt
    jmp 2b

.code64
long_mode_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs
    movq $stack_top, %rsp
    movl boot_info_ptr(%rip), %edi
    call kernel_main
3:  cli
    hlt
    jmp 3b

.align 8
gdt:
    .quad 0
    .quad 0x00af9a000000ffff
    .quad 0x00af92000000ffff
gdt_end:
gdt_pointer:
    .word gdt_end - gdt - 1
    .long gdt

.section .bss,"aw",@nobits
.align 4096
pml4: .skip 4096
pdpt: .skip 4096
pd:   .skip 4096
.align 16
stack_bottom: .skip 16384
stack_top:
boot_info_ptr: .skip 4
.section .note.GNU-stack,"",@progbits
