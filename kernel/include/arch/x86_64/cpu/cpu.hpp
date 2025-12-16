#pragma once

#include "cpu/gdt.hpp"

#define CACHE_LINE_SIZE 64

namespace kernel::cpu {
struct PerCPUData;
}

namespace kernel::cpu::arch {
struct alignas(CACHE_LINE_SIZE) CPUData {
    GDTEntry gdt[7];
    TSSBlock tss_block;

    static void init(CPUData* arch, uint64_t stack_top);
    static void commit_state(PerCPUData* cpu);
};
}  // namespace kernel::cpu::arch