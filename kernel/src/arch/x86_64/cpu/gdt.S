.section .text
.global load_gdt_and_flush
.type load_gdt_and_flush, @function

# load_gdt_and_flush(gdtr):
#  - Installs a new GDT and performs a far return to reload CS.
#  - Used when switching to per‑CPU GDTs so execution continues on the
#    freshly installed code/data descriptors.
#
# RDI = pointer to GDTR
load_gdt_and_flush:
    lgdtq (%rdi)
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %ss

    # Far return through the new code segment (selector 0x08).
    lea 1f(%rip), %rax
    pushq $0x08
    pushq %rax
    lretq
1:
    retq