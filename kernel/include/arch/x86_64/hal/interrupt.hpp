#pragma once

#include "hal/interface/interrupt.hpp"

namespace kernel::cpu::arch {
class InterruptDispatcher {
   public:
    static void register_handler(uint8_t vector, IInterruptHandler* handler,
                                 bool eoi_first = false);

    static void map_legacy_irq(uint8_t irq, uint8_t vector, IInterruptHandler* handler,
                               uint32_t dest_cpu = 0, bool eoi_first = false);

    static void map_pci_irq(uint32_t gsi, uint8_t vector, IInterruptHandler* handler,
                            uint32_t dest_cpu = 0, bool eoi_first = false);

    static void unregister_handler(uint8_t vector);
    static void unmap_legacy_irq(uint8_t irq, uint8_t vector);
    static void unmap_pci_irq(uint32_t gsi, uint8_t vector);

    static void dispatch(TrapFrame* frame);

   private:
    static void default_handler(TrapFrame* frame, uint32_t cpu_id);
    static IInterruptHandler* handlers[256];
};
}  // namespace kernel::cpu::arch