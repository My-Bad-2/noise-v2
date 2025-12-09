.section .text
.extern exception_handler
.type interrupt_common_stub, @function
# interrupt_common_stub:
#  - Saves all GPRs into a TrapFrame-compatible layout.
#  - Optionally switches GS between user and kernel for per‑CPU data.
#  - Calls the `exception_handler`, then restores state and iretq's.
# This centralizes interrupt entry/exit so IDT entries can all jump here.
interrupt_common_stub:
    cld

    # Save General Purpose Registers
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    # Speculative Execution Mitigation (Zero registers)
    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorl %edi, %edi
    xorq %r8,  %r8
    xorq %r9,  %r9
    xorq %r10, %r10
    xorq %r11, %r11
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15

    movq %rsp, %rdi

    # We check the Code Segment (CS) saved on the stack to see if we came from User Mode.
    movq 144(%rsp), %rax
    testb $3, %al        # Check bottom 2 bits (RPL). If != 0, it's User Mode.
    jz 1f
    swapgs               # Switch GS to Kernel Per-CPU Data
1:
    call exception_handler
    movq %rax, %rsp

    # Check CS again to see if we are returning to User Mode
    movq 144(%rsp), %rax
    testb $3, %al
    jz 2f
    swapgs               # Switch GS back to User TLS
2:
    # Restore Registers
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    # Clean Stack (Remove Vector + Error Code)
    addq $16, %rsp
    iretq

.macro make_stub number
    .align 8
    isr_\number:
        # Push Dummy Error Code (0) if the CPU doesn't push one automatically
        .if !(\number == 8 || (\number >= 10 && \number <= 14) || \number == 17 || \number == 30)
            pushq $0
        .endif
        
        # Push Interrupt Vector Number
        pushq $\number
        jmp interrupt_common_stub
.endm

.macro make_entry number
    .quad isr_\number
.endm

.section .text
.altmacro

.set i, 0
.rept 256
    make_stub %i
    .set i, i + 1
.endr

.section .data
.align 8
.global interrupt_stub_table
interrupt_stub_table:
    .set i, 0
    .rept 256
        make_entry %i
        .set i, i + 1
    .endr