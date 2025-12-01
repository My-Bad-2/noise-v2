#pragma once

#include "hal/interface/uart.hpp"

namespace kernel::arch {
void init();
hal::IUART* get_kconsole();
}  // namespace kernel::arch