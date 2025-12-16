#pragma once

#include "hal/interface/uart.hpp"

namespace kernel::arch {
[[noreturn]] void halt(bool interrupts);
void pause();

void disable_interrupts();
void enable_interrupts();
bool interrupt_status();

hal::IUART* get_kconsole();

void init();
}  // namespace kernel::arch