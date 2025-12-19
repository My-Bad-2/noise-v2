#pragma once

#include "cpu/gdt.hpp"

namespace kernel::cpu::arch {
struct alignas(CACHE_LINE_SIZE) CpuData {
    GDTManager* gdt;

    CpuData() : gdt(new GDTManager) {}
};
}  // namespace kernel::cpu::arch