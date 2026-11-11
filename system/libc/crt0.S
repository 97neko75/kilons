.text
.global _start
.type _start,@function
_start:
    mov (%rsp), %rdi
    lea 8(%rsp), %rsi
    lea 8(%rsi,%rdi,8), %rax
    mov %rax,environ(%rip)
    and $-16,%rsp
    call main
    mov %eax,%edi
    call _exit
.size _start,.-_start
.section .note.GNU-stack,"",@progbits
