#pragma once

#include <cstdint>
#include "hal/interface/interrupt.hpp"

namespace kernel::cpu::arch {
/**
 * @brief Interrupt dispatcher for x86_64.
 *
 * Maintains a static table of up to 256 handlers, one per vector. The
 * assembly stub builds a `TrapFrame` and calls into `dispatch`, which
 * then forwards to the registered handler or a default panic handler.
 *
 * Why:
 *  - Separates low-level IDT mechanics from higher-level interrupt
 *    routing and naming.
 *  - Makes it easy to plug in architecture-neutral handlers for
 *    exceptions and device interrupts.
 *  - Centralizes policies like "who sends EOIs" and "what to do with
 *    spurious or unmapped interrupts".
 */
class InterruptDispatcher {
   public:
    /// Register a handler for a specific interrupt/exception vector.
    static void register_handler(uint8_t vector, IInterruptHandler* handler);

    /// Unregister a handler for a specific interrupt/exception vector.
    static void unregister_handler(uint8_t vector);

    /// Dispatch an interrupt to the appropriate handler based on vector.
    static TrapFrame* dispatch(TrapFrame* frame);

    /**
     * @brief Connect a legacy ISA IRQ to an IDT vector and LAPIC target.
     *
     * Wrapper that:
     *  - Installs the handler for @p vector.
     *  - Asks the IOAPIC layer to route the legacy IRQ (with any ACPI
     *    ISO overrides) to the appropriate LAPIC destination.
     *
     * This keeps PCI/ISA routing logic out of device drivers.
     */
    static void map_legacy_irq(uint8_t irq, uint8_t vector, IInterruptHandler* handler,
                               uint32_t dest_cpu = 0);

    /**
     * @brief Connect a PCI/GS-based interrupt to an IDT vector.
     *
     * Used for modern devices that expose GSIs directly. The dispatcher
     * arranges both the IDT handler binding and the IOAPIC redirection
     * to the chosen CPU and vector.
     */
    static void map_pci_irq(uint32_t gsi, uint8_t vector, IInterruptHandler* handler,
                            uint32_t dest_cpu = 0);

    /// Tear down an existing legacy IRQ mapping and unregister the handler.
    static void unmap_legacy_irq(uint8_t irq, uint8_t vector);

    /// Tear down an existing PCI/GSI mapping and unregister the handler.
    static void unmap_pci_irq(uint32_t gsi, uint8_t vector);

   private:
    static TrapFrame* default_handler(TrapFrame* frame, uint32_t cpu_id);
    static IInterruptHandler* handlers[256];
};
}  // namespace kernel::cpu::arch