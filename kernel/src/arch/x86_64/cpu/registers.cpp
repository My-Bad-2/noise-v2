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
    asm volatile("mov %0, %%cr3" ::"r"(this->raw) : "memory");
}

Cr4 Cr4::read() {
    Cr4 cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4.raw));
    return cr4;
}

void Cr4::write() {
    asm volatile("mov %0, %%cr4" ::"r"(raw) : "memory");
}

void InvpcidDesc::flush(InvpcidType type) {
    asm volatile("invpcid (%0), %1" ::"r"(this), "r"(static_cast<uint64_t>(type)) : "memory");
}

Msr Msr::read(uint32_t index) {
    Msr msr;
    uint32_t low, high;

    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(index));

    msr.index = index;
    msr.value = (static_cast<uint64_t>(high) << 32) | low;

    return msr;
}

void Msr::write() {
    uint32_t low  = this->value & 0xFFFFFFFF;
    uint32_t high = this->value >> 32;

    asm volatile("wrmsr" ::"c"(this->index), "a"(low), "d"(high));
}

Mxcsr Mxcsr::read() {
    Mxcsr mxcsr;
    asm volatile("stmxcsr %0" : "=m"(mxcsr.raw));
    return mxcsr;
}

void Mxcsr::write() {
    asm volatile("ldmxcsr %0" ::"m"(raw));
}

void Xcr0::write() {
    uint32_t eax = raw & 0xFFFFFFFF;
    uint32_t edx = raw >> 32;
    asm volatile("xsetbv" ::"a"(eax), "d"(edx), "c"(0) : "memory");
}

Xcr0 Xcr0::read() {
    Xcr0 xcr0;
    uint32_t eax, edx;
    asm volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    xcr0.raw = (static_cast<uint64_t>(edx) << 32) | eax;
    return xcr0;
}
}  // namespace kernel::arch