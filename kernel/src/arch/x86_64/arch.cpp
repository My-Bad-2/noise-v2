#include "arch.hpp"
#include "hal/interface/uart.hpp"
#include "hal/uart.hpp"

namespace kernel::arch {
void init() {
    // uart = hal::UART16550();
}

hal::IUART* get_kconsole() {
    static hal::UART16550 uart{};
    return &uart;
}
}  // namespace kernel::arch