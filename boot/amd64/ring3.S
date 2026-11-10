/* Ring 3 entry, cooperative syscall suspension, process exit/fault recovery.
 * Every return to kernel passes through the saved kernel launcher frame.
 * Only one user process executes at any instant on this bootstrap CPU. */
.section .bss,"aw",@nobits
.align 8
ring3_saved_rsp: .quad 0
.section .text,"ax"
.code64
.global ring3_resume
ring3_resume:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp,ring3_saved_rsp(%rip)
    cli
    /* Build a valid 64-bit privilege-change iret frame. */
    movq %rdi,%r11
    pushq 152(%r11) /* SS */
    pushq 144(%r11) /* RSP */
    pushq 136(%r11) /* RFLAGS */
    pushq 128(%r11) /* CS */
    pushq 120(%r11) /* RIP */
    movq 0(%r11),%r15
    movq 8(%r11),%r14
    movq 16(%r11),%r13
    movq 24(%r11),%r12
    movq 40(%r11),%r10
    movq 48(%r11),%r9
    movq 56(%r11),%r8
    movq 64(%r11),%rdi
    movq 72(%r11),%rsi
    movq 80(%r11),%rbp
    movq 88(%r11),%rbx
    movq 96(%r11),%rdx
    movq 104(%r11),%rcx
    movq 112(%r11),%rax
    movq 32(%r11),%r11
    iretq
.global ring3_syscall_stub
ring3_syscall_stub:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp,%rdi
    movq %rsp,%r12
    andq $-16,%rsp
    cld
    call ring3_trap_syscall
    movq %r12,%rsp
    testl %eax,%eax
    jnz ring3_switch_to_kernel
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    iretq
.global ring3_switch_to_kernel
ring3_switch_to_kernel:
    cli
    movq ring3_saved_rsp(%rip),%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    sti
    ret
.section .note.GNU-stack,"",@progbits
