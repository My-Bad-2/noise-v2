#include "cpu/regs.h"

.section .text
.global syscall_entry
.extern syscall_handler
.type syscall_entry, @function

syscall_entry:
    swapgs // Swap GS to switch from User Thread Local Storage to Kernel Per-CPU data

    movq %rsp, %gs:32 // Save user stack pointer immediately
    movq %gs:24, %rsp // Switch to kernel stack

    pushq $0x1B // SS = 0x18 | 3 (User Data)
    pushq %gs:32    // User RSP
    pushq %r11  // User rflags
    pushq $0x23 // CS = 0x20 | 3 (User Code)
    pushq %rcx  // User RIP

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

    // RDI = Syscall num
    // RSI = Stack Ptr
    movq %rax, %rdi
    movq %rsp, %rsi

    call syscall_handler

    // Save Return value (overwrite saved %rax on stack)
    movq %rax, 112(%rsp)

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

    // Prepare for Sysret
    popq %rcx   // Restore RIP
    addq $8, %rsp // Skip CS (sysret sets this automatically)
    popq %r11   // Restore RFLAGS
    popq %rsp   // Restore User RSP

    // Swap GS back to User Mode
    swapgs
    sysretq