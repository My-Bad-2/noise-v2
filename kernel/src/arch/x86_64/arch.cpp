/**
 * @file arch.cpp
 * @brief x86_64 architecture-specific bootstrap and helpers.
 *
 * This source file contains the concrete implementation of the minimal
 * x86_64 architecture interface declared in `arch.hpp`. It wires up a
 * simple kernel console UART and provides basic CPU control helpers.
 */

#include "arch.hpp"
#include "cpu/exception.hpp"
#include "hal/interface/uart.hpp"
#include "hal/interrupt.hpp"
#include "hal/uart.hpp"
#include "cpu/idt.hpp"
#include "hal/handlers/df.hpp"
#include "hal/pic.hpp"
#include "hal/pit.hpp"

namespace kernel::arch {
namespace {
handlers::DFHandler df_handler;

void initialize_interrupt_subsystem() {
    cpu::arch::IDTManager::setup_idt();
    cpu::arch::InterruptDispatcher::register_handler(EXCEPTION_DOUBLE_FAULT, &df_handler);
    hal::LegacyPIC::remap();
}
}  // namespace

void init() {
    // Architecture-specific initialization hook.
    initialize_interrupt_subsystem();
    hal::PIT::init(1000);
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

void disable_interrupts() {
    asm volatile("cli");
}

void enable_interrupts() {
    asm volatile("sti");
}

bool interrupt_status() {
    uint64_t rflags = 0;
    asm volatile("pushfq; pop %0" : "=r"(rflags));
    return rflags & 0x200;
}
}  // namespace kernel::arch

extern "C" void exception_handler(kernel::cpu::arch::TrapFrame* frame) {
    kernel::cpu::arch::InterruptDispatcher::dispatch(frame);
}