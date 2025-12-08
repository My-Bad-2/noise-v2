#include "cpu/idt.hpp"
#include <string.h>
#include <cstdint>
#include "cpu/exception.hpp"
#include "libs/log.hpp"

#define MAX_IDT_ENTRIES 256

#define IDT_ATTR_PRESENT   0x80
#define IDT_ATTR_RING0     0x00
#define IDT_ATTR_RING3     0x60
#define IDT_TYPE_INTERRUPT 0x0E  // Disables interrupts
#define IDT_TYPE_TRAP      0x0F  // Keeps interrupts enabled

extern "C" uintptr_t interrupt_stub_table[];

namespace kernel::cpu::arch {
IDTEntry* IDTManager::idt = nullptr;
IDTR IDTManager::idtr     = {};

void IDTManager::setup_idt() {
    auto encode_gate = [&](int interrupt, uint64_t base, uint16_t selector, uint8_t flags,
                           uint8_t ist) {
        idt[interrupt].offset_low    = base & 0xFFFF;
        idt[interrupt].offset_middle = (base >> 16) & 0xFFFF;
        idt[interrupt].offset_high   = (base >> 32) & 0xFFFFFFFF;

        idt[interrupt].selector   = selector;
        idt[interrupt].ist_index  = ist;
        idt[interrupt].attributes = flags;
        idt[interrupt].reserved   = 0;
    };

    if (idt != nullptr) {
        return;
    }

    idt = new IDTEntry[MAX_IDT_ENTRIES];
    LOG_DEBUG("IDT: allocated table at %p", idt);

    memset(idt, 0, sizeof(IDTEntry) * MAX_IDT_ENTRIES);

    for (int i = 0; i < MAX_IDT_ENTRIES; ++i) {
        uint8_t ist   = 0;
        uint8_t flags = IDT_ATTR_PRESENT | IDT_ATTR_RING0 | IDT_TYPE_INTERRUPT;

        // Force NMI and Double Fault to use dedicated stacks so that
        // catastrophic events do not rely on potentially corrupted stacks.
        if (i == EXCEPTION_DOUBLE_FAULT || i == EXCEPTION_NON_MASKABLE_INTERRUPT) {
            ist = 1;
        }

        encode_gate(i, interrupt_stub_table[i], 0x08, flags, ist);
    }

    idtr.base  = reinterpret_cast<uint64_t>(idt);
    idtr.limit = (sizeof(IDTEntry) * MAX_IDT_ENTRIES) - 1;
}

void IDTManager::load_table() {
    asm volatile("lidt %0" ::"m"(idtr));
    LOG_INFO("IDT: loaded table base=0x%lx limit=0x%x", idtr.base, idtr.limit);
}
}  // namespace kernel::cpu::arch