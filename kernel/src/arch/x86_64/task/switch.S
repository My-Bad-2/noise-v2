.section .text

.extern trap_return
.global new_thread_entry
.type new_thread_entry, @function
new_thread_entry:
    fninit
    // MAGIC: Check SIMD::init() for ldmxcsr value description
    ldmxcsr $0x1f80
    jmp trap_return

.global context_switch
.type context_switch, @function
context_switch:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, 24(%rdi)
    movq 24(%rsi), %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx

    ret 