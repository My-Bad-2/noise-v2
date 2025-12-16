#pragma once

#include <cstdint>

#define IOPB_SIZE 0x2000  // 64K I/O ports / 8 bits per byte

namespace kernel::cpu::arch {
struct CPUData;
}

namespace kernel::cpu::arch {
struct [[gnu::packed]] GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
};

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

struct [[gnu::packed]] TSSBlock {
    TSS64 header;
    uint8_t iopb[IOPB_SIZE];
    uint8_t terminator;  // Must be 0xFF to signal end of bitmap.
};

struct [[gnu::packed]] GDTR {
    uint16_t limit;
    uint64_t base;
};

class GDTManager {
   public:
    static void setup_tss(CPUData* cpu, uint64_t stack_top);
    static void setup_gdt(CPUData* cpu);
    static void load_tables(CPUData* cpu);
    static void set_io_perm(CPUData* arch, uint16_t port, bool enable);
};
}  // namespace kernel::cpu::arch