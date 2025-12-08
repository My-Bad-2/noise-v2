#pragma once

#include <cstdint>

#define IOPB_SIZE 0x2000  // 64K I/O ports / 8 bits per byte

namespace kernel::cpu::arch {
struct CPUData;
}

namespace kernel::cpu::arch {
/**
 * @brief Raw 64‑bit GDT entry descriptor.
 *
 * Kept as a POD matching the hardware format so we can fill it directly.
 * The higher-level code treats this as opaque and only configures access
 * bits and base/limit fields via `GDTManager`.
 */
struct [[gnu::packed]] GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
};

/**
 * @brief 64‑bit TSS descriptor stored in the GDT.
 *
 * Encapsulates the base/limit of a `TSS64` and is used by `ltr`.
 */
struct [[gnu::packed]] TSSDescriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
};

/**
 * @brief 64‑bit Task State Segment used by x86_64.
 *
 * Only a subset of fields are actively used:
 *  - `rsp0` for ring‑0 stack on privilege transitions.
 *  - `ist[]` for interrupt stacks.
 *  - `iomap_base` to locate the I/O permission bitmap.
 *
 * The rest remain reserved to keep layout compatible with the hardware
 * specification.
 */
struct [[gnu::packed]] TSS64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

/**
 * @brief TSS plus I/O permission bitmap block.
 *
 * The I/O permission bitmap controls which legacy I/O ports ring‑3 code
 * may access. A value of 1 in the bitmap denies access to that port.
 *
 * Keeping the bitmap co-located with the TSS simplifies per‑CPU I/O
 * isolation: flipping a single bit here affects only that CPU.
 */
struct [[gnu::packed]] TSSBlock {
    TSS64 header;
    uint8_t iopb[IOPB_SIZE];
    uint8_t terminator;  // Must be 0xFF to signal end of bitmap.
};

struct [[gnu::packed]] GDTR {
    uint16_t limit;
    uint64_t base;
};

/**
 * @brief Helper for building/loading per‑CPU GDTs and TSS.
 *
 * Why a helper:
 *  - Encapsulates fragile descriptor encoding logic.
 *  - Centralizes the policy for kernel/user segments and TSS layout.
 *  - Provides an API to edit the I/O bitmap without leaking hardware
 *    details everywhere.
 */
class GDTManager {
   public:
    /// Initialize TSS stack pointers and I/O bitmap for a CPU.
    static void setup_tss(CPUData* cpu, uint64_t stack_top);

    /// Populate the per‑CPU GDT entries, including TSS descriptor.
    static void setup_gdt(CPUData* cpu);

    /// Load GDTR and TR for this CPU (segment state is refreshed in asm stub).
    static void load_tables(CPUData* cpu);

    /// Enable/disable access to an I/O port in the TSS I/O bitmap.
    static void set_io_perm(CPUData* arch, uint16_t port, bool enable);
};
}  // namespace kernel::cpu::arch