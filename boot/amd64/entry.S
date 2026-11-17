/* Limine enters the kernel in 64-bit long mode with a valid stack and page
 * tables.  The kernel keeps that address space until its own VM bootstrap
 * has installed the low supervisor mappings used by the existing tests. */
.section .text.boot,"ax"
.code64
.global _start
.type _start, @function
.extern kernel_main
_start:
    cli
    cld
    xorq %rbp, %rbp
    andq $-16, %rsp
    call kernel_main
1:
    cli
    hlt
    jmp 1b
.size _start, .-_start
.section .note.GNU-stack,"",@progbits
