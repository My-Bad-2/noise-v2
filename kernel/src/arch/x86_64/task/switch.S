.section .text

.extern trap_return
.global new_thread_entry
.type new_thread_entry, @function
new_thread_entry:
    fninit
    // MAGIC: Check SIMD::init() for ldmxcsr value description
    movl $0x1f80, %eax
    pushq %rax
    ldmxcsr (%rsp)
    addq $8, %rsp
    jmp trap_return

.extern thread_exit
.global kernel_thread_entry
.type kernel_thread_entry, @function
kernel_thread_entry:
    popq %rdi   // Pop Argument
    popq %rax   // Pop `entry_function`
    sti
    call *%rax
    call thread_exit

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