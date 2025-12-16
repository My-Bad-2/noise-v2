#pragma once

#include <cstdint>

namespace kernel::cpu::arch {
struct [[gnu::packed]] IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist_index;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
};

struct [[gnu::packed]] IDTR {
    uint16_t limit;
    uint64_t base;
};

class IDTManager {
   public:
    static void setup_idt();
    static void load_table();

   private:
    static IDTEntry* idt;
    static IDTR idtr;
};
}  // namespace kernel::cpu::arch