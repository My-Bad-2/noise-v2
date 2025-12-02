/**
 * @file arch.cpp
 * @brief x86_64 architecture-specific bootstrap and helpers.
 *
 * This source file contains the concrete implementation of the minimal
 * x86_64 architecture interface declared in `arch.hpp`. It wires up a
 * simple kernel console UART and provides basic CPU control helpers.
 */

#include "arch.hpp"
#include "hal/interface/uart.hpp"
#include "hal/uart.hpp"

namespace kernel::arch {

void init() {
    // Architecture-specific initialization hook.
    // Currently empty, but this is the place to:
    //  - Initialize descriptor tables (GDT/IDT).
    //  - Configure basic hardware needed before entering the main kernel.
    //  - Set up early console, timers, etc.
}

hal::IUART* get_kconsole() {
    // Static instance bound to the default COM port selected by UART16550.
    static hal::UART16550 uart{};
    // Upcast to the generic HAL UART interface.
    return &uart;
}

void halt(bool interrupts) {
    // Infinite loop: there is no return from this function.
    while (true) {
        if (!interrupts) {
            // Disable interrupts so that no further IRQs are handled.
            asm volatile("cli");
        }

        // Enter low-power halt state until next interrupt (if any).
        asm volatile("hlt");
    }
}

void pause() {
    asm volatile("pause");
}

}  // namespace kernel::arch