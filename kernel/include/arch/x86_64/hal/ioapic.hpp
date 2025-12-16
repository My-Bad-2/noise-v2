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
    /**
     * @brief Discover and initialize all IOAPIC controllers.
     *
     * Builds an internal list of IOAPICs from MADT, maps their MMIO
     * windows, computes their GSI ranges, and masks all pins by
     * default. This ensures no stray external interrupts fire until
     * explicit routing is configured.
     */
    static void init();

    /**
     * @brief Route a legacy ISA IRQ (0–15) to a LAPIC vector.
     *
     * Uses ACPI Interrupt Source Override (ISO) entries when present to
     * translate old-style IRQ numbers into GSIs and correct polarity/
     * trigger semantics, then programs the appropriate IOAPIC pin.
     */
    static void route_legacy_irq(uint8_t irq, uint8_t vector, uint32_t dest_lapic_id);

    /**
     * @brief Route an arbitrary GSI to a LAPIC vector.
     *
     * Intended for non-legacy interrupts (e.g. modern devices exposing
     * GSIs directly). Callers supply delivery/polarity/trigger flags to
     * encode the desired behavior.
     */
    static void route_gsi(uint32_t gsi, uint8_t vector, uint32_t dest_lapic_id,
                          size_t flags = IOAPIC_DELIVERY_FIXED | IOAPIC_DEST_PHYSICAL);

    /// Mask (disable) a given GSI at the IOAPIC level.
    static void mask_gsi(uint32_t gsi);

    /// Unmask (enable) a given GSI at the IOAPIC level.
    static void unmask_gsi(uint32_t gsi);

    /// Mask (disable) a legacy IRQ at the IOAPIC level.
    static void mask_legacy_irq(uint8_t irq);

    /// Unmask (enable) a legacy IRQ at the IOAPIC level.
    static void unmask_legacy_irq(uint8_t irq);

    static void send_eoi(uint8_t vector);

   private:
    static uint32_t read(int controller_idx, uint32_t reg);
    static void write(int controller_idx, uint32_t reg, uint32_t val);

    /// Look up which IOAPIC controller owns a given GSI.
    static int get_controller_idx(uint32_t gsi);
    /// Find an ACPI interrupt source override for the given legacy IRQ.
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

    /// Head of the ISO linked list parsed from ACPI MADT.
    static IsoInfo* iso_list;
};
}  // namespace kernel::hal