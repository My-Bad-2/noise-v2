#pragma once

#include <cstdint>

namespace kernel::cpu::arch {
/**
 * @brief Raw IDT entry format for x86_64.
 *
 * Populated by `IDTManager::setup_idt` with pointers to common stubs in
 * `idt.S`. Higher-level code should treat this as opaque.
 */
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

/**
 * @brief Manager for the shared IDT used on the boot CPU.
 *
 * In the current design a single IDT is created once and then loaded
 * onto each CPU. It wires all 256 vectors to the common assembly stub
 * and lets `InterruptDispatcher` handle routing in C++.
 */
class IDTManager {
   public:
    static void setup_idt();
    static void load_table();

   private:
    static IDTEntry* idt;
    static IDTR idtr;
};
}  // namespace kernel::cpu::arch