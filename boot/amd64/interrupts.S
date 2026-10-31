.section .text,"ax"
.code64
.extern interrupt_dispatch
.global interrupt_stub_table
.global interrupt_stub_0
interrupt_stub_0:
    pushq $0
    pushq $0
    jmp interrupt_common
.global interrupt_stub_1
interrupt_stub_1:
    pushq $0
    pushq $1
    jmp interrupt_common
.global interrupt_stub_2
interrupt_stub_2:
    pushq $0
    pushq $2
    jmp interrupt_common
.global interrupt_stub_3
interrupt_stub_3:
    pushq $0
    pushq $3
    jmp interrupt_common
.global interrupt_stub_4
interrupt_stub_4:
    pushq $0
    pushq $4
    jmp interrupt_common
.global interrupt_stub_5
interrupt_stub_5:
    pushq $0
    pushq $5
    jmp interrupt_common
.global interrupt_stub_6
interrupt_stub_6:
    pushq $0
    pushq $6
    jmp interrupt_common
.global interrupt_stub_7
interrupt_stub_7:
    pushq $0
    pushq $7
    jmp interrupt_common
.global interrupt_stub_8
interrupt_stub_8:
    pushq $8
    jmp interrupt_common
.global interrupt_stub_9
interrupt_stub_9:
    pushq $0
    pushq $9
    jmp interrupt_common
.global interrupt_stub_10
interrupt_stub_10:
    pushq $10
    jmp interrupt_common
.global interrupt_stub_11
interrupt_stub_11:
    pushq $11
    jmp interrupt_common
.global interrupt_stub_12
interrupt_stub_12:
    pushq $12
    jmp interrupt_common
.global interrupt_stub_13
interrupt_stub_13:
    pushq $13
    jmp interrupt_common
.global interrupt_stub_14
interrupt_stub_14:
    pushq $14
    jmp interrupt_common
.global interrupt_stub_15
interrupt_stub_15:
    pushq $0
    pushq $15
    jmp interrupt_common
.global interrupt_stub_16
interrupt_stub_16:
    pushq $0
    pushq $16
    jmp interrupt_common
.global interrupt_stub_17
interrupt_stub_17:
    pushq $17
    jmp interrupt_common
.global interrupt_stub_18
interrupt_stub_18:
    pushq $0
    pushq $18
    jmp interrupt_common
.global interrupt_stub_19
interrupt_stub_19:
    pushq $0
    pushq $19
    jmp interrupt_common
.global interrupt_stub_20
interrupt_stub_20:
    pushq $0
    pushq $20
    jmp interrupt_common
.global interrupt_stub_21
interrupt_stub_21:
    pushq $21
    jmp interrupt_common
.global interrupt_stub_22
interrupt_stub_22:
    pushq $0
    pushq $22
    jmp interrupt_common
.global interrupt_stub_23
interrupt_stub_23:
    pushq $0
    pushq $23
    jmp interrupt_common
.global interrupt_stub_24
interrupt_stub_24:
    pushq $0
    pushq $24
    jmp interrupt_common
.global interrupt_stub_25
interrupt_stub_25:
    pushq $0
    pushq $25
    jmp interrupt_common
.global interrupt_stub_26
interrupt_stub_26:
    pushq $0
    pushq $26
    jmp interrupt_common
.global interrupt_stub_27
interrupt_stub_27:
    pushq $0
    pushq $27
    jmp interrupt_common
.global interrupt_stub_28
interrupt_stub_28:
    pushq $0
    pushq $28
    jmp interrupt_common
.global interrupt_stub_29
interrupt_stub_29:
    pushq $29
    jmp interrupt_common
.global interrupt_stub_30
interrupt_stub_30:
    pushq $30
    jmp interrupt_common
.global interrupt_stub_31
interrupt_stub_31:
    pushq $0
    pushq $31
    jmp interrupt_common
.global interrupt_stub_32
interrupt_stub_32:
    pushq $0
    pushq $32
    jmp interrupt_common
.global interrupt_stub_33
interrupt_stub_33:
    pushq $0
    pushq $33
    jmp interrupt_common
.global interrupt_stub_34
interrupt_stub_34:
    pushq $0
    pushq $34
    jmp interrupt_common
.global interrupt_stub_35
interrupt_stub_35:
    pushq $0
    pushq $35
    jmp interrupt_common
.global interrupt_stub_36
interrupt_stub_36:
    pushq $0
    pushq $36
    jmp interrupt_common
.global interrupt_stub_37
interrupt_stub_37:
    pushq $0
    pushq $37
    jmp interrupt_common
.global interrupt_stub_38
interrupt_stub_38:
    pushq $0
    pushq $38
    jmp interrupt_common
.global interrupt_stub_39
interrupt_stub_39:
    pushq $0
    pushq $39
    jmp interrupt_common
.global interrupt_stub_40
interrupt_stub_40:
    pushq $0
    pushq $40
    jmp interrupt_common
.global interrupt_stub_41
interrupt_stub_41:
    pushq $0
    pushq $41
    jmp interrupt_common
.global interrupt_stub_42
interrupt_stub_42:
    pushq $0
    pushq $42
    jmp interrupt_common
.global interrupt_stub_43
interrupt_stub_43:
    pushq $0
    pushq $43
    jmp interrupt_common
.global interrupt_stub_44
interrupt_stub_44:
    pushq $0
    pushq $44
    jmp interrupt_common
.global interrupt_stub_45
interrupt_stub_45:
    pushq $0
    pushq $45
    jmp interrupt_common
.global interrupt_stub_46
interrupt_stub_46:
    pushq $0
    pushq $46
    jmp interrupt_common
.global interrupt_stub_47
interrupt_stub_47:
    pushq $0
    pushq $47
    jmp interrupt_common
interrupt_common:
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
    movq %rsp, %rdi
    movq %rsp, %r12
    andq $-16, %rsp
    cld
    call interrupt_dispatch
    movq %r12, %rsp
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
    addq $16, %rsp
    iretq
.section .rodata,"a"
.align 8
interrupt_stub_table:
    .quad interrupt_stub_0
    .quad interrupt_stub_1
    .quad interrupt_stub_2
    .quad interrupt_stub_3
    .quad interrupt_stub_4
    .quad interrupt_stub_5
    .quad interrupt_stub_6
    .quad interrupt_stub_7
    .quad interrupt_stub_8
    .quad interrupt_stub_9
    .quad interrupt_stub_10
    .quad interrupt_stub_11
    .quad interrupt_stub_12
    .quad interrupt_stub_13
    .quad interrupt_stub_14
    .quad interrupt_stub_15
    .quad interrupt_stub_16
    .quad interrupt_stub_17
    .quad interrupt_stub_18
    .quad interrupt_stub_19
    .quad interrupt_stub_20
    .quad interrupt_stub_21
    .quad interrupt_stub_22
    .quad interrupt_stub_23
    .quad interrupt_stub_24
    .quad interrupt_stub_25
    .quad interrupt_stub_26
    .quad interrupt_stub_27
    .quad interrupt_stub_28
    .quad interrupt_stub_29
    .quad interrupt_stub_30
    .quad interrupt_stub_31
    .quad interrupt_stub_32
    .quad interrupt_stub_33
    .quad interrupt_stub_34
    .quad interrupt_stub_35
    .quad interrupt_stub_36
    .quad interrupt_stub_37
    .quad interrupt_stub_38
    .quad interrupt_stub_39
    .quad interrupt_stub_40
    .quad interrupt_stub_41
    .quad interrupt_stub_42
    .quad interrupt_stub_43
    .quad interrupt_stub_44
    .quad interrupt_stub_45
    .quad interrupt_stub_46
    .quad interrupt_stub_47
.section .note.GNU-stack,"",@progbits
