#pragma once

#include "hal/apic.hpp"
#include "hal/mmio.hpp"

// Redirection Table Entry Bits
#define IOAPIC_DELIVERY_FIXED  0x000
#define IOAPIC_DELIVERY_LOWEST 0x100
#define IOAPIC_DELIVERY_SMI    0x200
#define IOAPIC_DELIVERY_NMI    0x400
#define IOAPIC_DELIVERY_INIT   0x500
#define IOAPIC_DELIVERY_EXT    0x700

#define IOAPIC_DEST_PHYSICAL 0x0000
#define IOAPIC_DEST_LOGICAL  0x0800

#define IOAPIC_POLARITY_HIGH 0x0000  // Active High
#define IOAPIC_POLARITY_LOW  0x2000  // Active Low

#define IOAPIC_TRIGGER_EDGE  0x0000
#define IOAPIC_TRIGGER_LEVEL 0x8000

#define IOAPIC_MASKED 0x10000ul

namespace kernel::hal {
/**
 * @brief I/O APIC (IOAPIC) abstraction.
 *
 * IOAPICs are the bridge between external IRQ lines (GSIs) and local
 * APICs. This class:
 *  - Discovers IOAPIC MMIO blocks and their GSI ranges from ACPI MADT.
 *  - Applies interrupt source overrides (ISOs) for legacy IRQs.
 *  - Programs redirection table entries to send interrupts to LAPICs.
 *
 * Why:
 *  - Centralizes APIC routing policy (delivery mode, polarity, trigger).
 *  - Shields the rest of the kernel from IOAPIC register layout and
 *    from the details of ACPI's GSI model.
 */
class IOAPIC {
   public:
    static void init();

    static void route_legacy_irq(uint8_t irq, uint8_t vector, uint32_t dest_lapic_id);
    static void route_gsi(uint32_t gsi, uint8_t vector, uint32_t dest_lapic_id,
                          size_t flags = IOAPIC_DELIVERY_FIXED | IOAPIC_DEST_PHYSICAL);

    static void mask_gsi(uint32_t gsi);
    static void unmask_gsi(uint32_t gsi);

    static void mask_legacy_irq(uint8_t irq);
    static void unmask_legacy_irq(uint8_t irq);

    static void send_eoi(uint8_t vector);

   private:
    static uint32_t read(int controller_idx, uint32_t reg);
    static void write(int controller_idx, uint32_t reg, uint32_t val);

    static int get_controller_idx(uint32_t gsi);
    static IsoInfo* find_iso(uint8_t irq);

    struct Controller {
        uint8_t id;  ///< IOAPIC hardware ID for diagnostics.
        MMIORegion virt_base;
        uint32_t gsi_start;  ///< First GSI handled by this IOAPIC.
        uint32_t gsi_end;    ///< Last GSI handled by this IOAPIC.
    };

    static constexpr int MAX_CONTROLLERS = 16;
    static Controller controllers[MAX_CONTROLLERS];
    static int num_controllers;

    static IsoInfo* iso_list;
};
}  // namespace kernel::hal