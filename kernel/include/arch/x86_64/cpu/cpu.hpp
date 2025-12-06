#pragma once

#include "cpu/gdt.hpp"

#define CACHE_LINE_SIZE 64

namespace kernel::cpu {
struct PerCPUData;
}

namespace kernel::cpu::arch {
/**
 * @brief Architecture-specific per‑CPU state for x86_64.
 *
 * `CPUData` holds the per‑CPU GDT and TSS. It is embedded inside the
 * higher‑level `PerCPUData` structure and is cache‑line aligned to avoid
 * false sharing between cores when CPUs update their own state.
 *
 * Why:
 *  - Each core needs its own TSS (stacks, IST entries, IOPL bitmap).
 *  - Keeping the GDT/TSS per‑CPU simplifies future SMP support and
 *    per‑core privilege tweaking (e.g. I/O bitmap).
 */
struct alignas(CACHE_LINE_SIZE) CPUData {
    GDTEntry gdt[7];
    TSSBlock tss_block;

    /// Initialize architectural state for a core before it starts running.
    static void init(CPUData* arch, uint64_t stack_top);
    /// Commit the initialized state to hardware (load GDT/TSS/GS base).
    static void commit_state(CPUData* cpu);
};
}  // namespace kernel::cpu::arch