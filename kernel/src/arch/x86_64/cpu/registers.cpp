#include "cpu/registers.hpp"

namespace kernel::arch {
Cr0 Cr0::read() {
    Cr0 cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0.raw));
    return cr0;
}

void Cr0::write() {
    asm volatile("mov %0, %%cr0" ::"r"(raw) : "memory");
}

Cr2 Cr2::read() {
    Cr2 cr2;
    asm volatile("mov %%cr2, %0" : "=r"(cr2.linear_address));
    return cr2;
}

void Cr2::write() {
    asm volatile("mov %0, %%cr2" ::"r"(linear_address) : "memory");
}

Cr3 Cr3::read() {
    Cr3 cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3.raw));
    return cr3;
}

void Cr3::write() {
    asm volatile("mov %0, %%cr3" ::"r"(raw) : "memory");
}

Cr4 Cr4::read() {
    Cr4 cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4.raw));
    return cr4;
}

void Cr4::write() {
    asm volatile("mov %0, %%cr4" ::"r"(raw) : "memory");
}
}  // namespace kernel::arch