.section .text
.global context_switch
.type context_switch, @function

context_switch:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, 8(%rdi)
    movq 8(%rsi), %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx

    ret

.extern .trap_return
.global switch_trampoline
.type switch_trampoline, @function

switch_trampoline:
    jmp .trap_return