#pragma once

/**
 * @file arch.hpp
 * @brief x86_64 architecture-level initialization and helpers.
 *
 * This header declares the public entry points used by the kernel to
 * initialize and interact with the x86_64 architecture-specific layer.
 * It exposes a minimal set of functions for early setup, obtaining a
 * console UART, and basic CPU control (halt/pause).
 */

#include "hal/interface/uart.hpp"

namespace kernel::arch {

/**
 * @brief Perform x86_64 architecture-specific initialization.
 *
 * This function is intended to be called once during early kernel boot.
 * Typical responsibilities include:
 *  - Setting up descriptor tables (GDT/IDT).
 *  - Initializing basic devices required for early output.
 *  - Preparing the environment for higher-level subsystems.
 *
 * The exact behavior is implementation-defined and may evolve over time.
 */
void init();

/**
 * @brief Get a pointer to the kernel console UART.
 *
 * Returns a pointer to a statically-allocated UART implementation that
 * can be used for early logging and debug output. The returned object
 * implements the generic `hal::IUART` interface.
 *
 * The pointer remains valid for the lifetime of the kernel.
 *
 * @return Pointer to a kernel console UART instance.
 */
hal::IUART* get_kconsole();

/**
 * @brief Halt the CPU in an infinite loop.
 *
 * This function repeatedly executes the `hlt` instruction inside a loop.
 * If @p interrupts is false, interrupts are disabled via `cli` before
 * halting on each iteration, effectively stopping the CPU. If interrupts
 * are left enabled, the CPU can still wake in response to hardware
 * interrupts.
 *
 * @param interrupts If true, keep interrupts enabled while halting.
 */
[[noreturn]] void halt(bool interrupts);

/**
 * @brief Hint to the CPU that the current thread is in a spin-wait loop.
 *
 * Executes the `pause` instruction, which can reduce power consumption
 * and improve performance on hyper-threaded CPUs when spinning.
 */
void pause();

void disable_interrupts();
void enable_interrupts();
}  // namespace kernel::arch