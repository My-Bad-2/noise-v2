#pragma once

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
 */
class InterruptDispatcher {
   public:
    /// Register a handler for a specific interrupt/exception vector.
    static void register_handler(uint8_t vector, IInterruptHandler* handler);

    static void unregister_handler(uint8_t vector);
    
    /// Dispatch an interrupt to the appropriate handler based on vector.
    static void dispatch(TrapFrame* frame);

   private:
    static void default_handler(TrapFrame* frame, uint32_t cpu_id);
    static IInterruptHandler* handlers[256];
};
}  // namespace kernel::cpu::arch