.section .text
.global _start

.extern kmain
.extern limine_base_revision

_start:
    // Disable interrupts
    cli

    // Limine Revision Check
    /** The limine_base_revision is an array of 3 qwords (uint64_t).
     *
     *  Offset 0  (Index 0): Magic 1 (0xf9562b2d5c95a6c8)
     *  Offset 8  (Index 1): Magic 2 / Is Revision valid? (0x6a7b384944536bdc)
     *  Offset 16 (Index 2): Requested Revision (N)
     *  
     *  If the handshake succeeds, Limine overwrites offset 16 with 0.
     */
    movq limine_base_revision + 16(%rip), %rax
    testq %rax, %rax
    jnz .Llimine_unsupported // If limine_base_revision[2] != 0, revision is not supported.

    // Zero RBP to mark the end of stack frames for debuggers/unwinders.
    xorq %rbp, %rbp
    call kmain

    // kmain() shouldn't return, but if it does, we halt.
    jmp .Lhang
.Llimine_unsupported:
    // Handshake failed. Load magic error code 0xBADB002 into RDI for debuggers.
    movq 0xBADB002, %rdi
.Lhang:
    hlt
    jmp .Lhang