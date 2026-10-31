.section .text
.global context_switch
.type context_switch, @function
/* System V amd64: rdi = &old_rsp, rsi = new_rsp. */
context_switch:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)
    movq %rsi, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.section .note.GNU-stack,"",@progbits

.section .text
.global thread_start
.type thread_start, @function
thread_start:
    popq %rdi
    call thread_run
1:  cli
    hlt
    jmp 1b
