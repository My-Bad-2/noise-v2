/**
 * @file uart.cpp
 * @brief x86_64 16550 UART implementation of the HAL UART interface.
 *
 * This file contains the low-level implementation of `kernel::hal::UART16550`
 * using legacy I/O ports. All operations are blocking and suitable for
 * use in early boot and debugging.
 *
 * Architectural role:
 *  - Implements a concrete UART driver used as the kernel console.
 *  - `kernel::arch::get_kconsole()` typically instantiates this type
 *    and exposes it via the abstract `IUART` HAL interface.
 *  - The logging subsystem and any debug/console code send characters
 *    through this UART driver (directly or indirectly).
 */

#include "hal/uart.hpp"
#include "hal/io.hpp"
#include "internal/uart.h"

namespace kernel::hal {

void UART16550::write(uint16_t reg, uint8_t value) const {
    // Write a single byte to a UART register (base port + offset).
    out(this->port_base + reg, value);
}

uint8_t UART16550::read(uint16_t reg) const {
    // Read a single byte from a UART register (base port + offset).
    return in<uint8_t>(this->port_base + reg);
}

bool UART16550::is_tx_ready() {
    // Check if the transmitter holding register is empty.
    return this->read(LINE_STATUS) & LINE_TRANSMITTER_BUF_EMPTY;
}

bool UART16550::is_data_ready() {
    // Check if there is at least one byte available in the receiver buffer.
    return this->read(LINE_STATUS) & LINE_DATA_READY;
}

void UART16550::send_char(char c) {
    // Busy-wait until the transmitter can accept a new byte.
    while (!this->is_tx_ready()) {
        // Spin wait; upper layers should avoid excessive bursts here.
    }
    this->write(DATA, static_cast<uint8_t>(c));
}

char UART16550::recieve_char() {
    // Busy-wait until at least one byte is available to read.
    while (!this->is_data_ready()) {
        // Spin wait.
    }
    return static_cast<char>(this->read(DATA));
}

bool UART16550::init(uint32_t baud_rate) {
    // Disable all UART-generated interrupts; we use pure polling.
    this->write(INTERRUPT, 0x00);

    // Enable DLAB (Divisor Latch Access Bit) so we can program the baud rate.
    this->write(LINE_CONTROL, LINE_DLAB_STATUS);

    // Calculate divisor: base clock (assumed 115200 Hz) / desired baud rate.
    // NOLINTNEXTLINE
    uint16_t divisor = 115200 / (baud_rate);

    // Program divisor low and high bytes.
    this->write(BAUD_RATE_LOW, divisor & 0xFF);
    this->write(BAUD_RATE_HIGH, (divisor >> 8) & 0xFF);

    // Disable DLAB and set line to 8 data bits, no parity, 1 stop bit (8N1).
    this->write(LINE_CONTROL, LINE_DS_8);

    // Enable FIFO, clear both RX/TX queues, set highest trigger level.
    constexpr uint8_t fifo_flags =
        ENABLE_FIFO | FIFO_CLEAR_RECEIVE | FIFO_CLEAR_TRANSMIT | FIFO_TRIGGER_LEVEL4;
    this->write(FIFO_CONTROLLER, fifo_flags);

    // Enable RTS and DTR, and OUT2 (often required to enable interrupts).
    this->write(MODEM_CONTROL, MODEM_RTS | MODEM_DTR | MODEM_OUT2);

    // Enter loopback mode for a simple self-test.
    this->write(MODEM_CONTROL, this->read(MODEM_CONTROL) | MODEM_LOOPBACK);

    // Give the UART some time to become ready for transmission.
    size_t spins               = 0;
    constexpr size_t max_spins = (1 << 20);
    while (!this->is_tx_ready() && spins++ < max_spins) {
        asm volatile("pause");
    }

    // Send a test byte in loopback mode and verify that we can read it back.
    constexpr uint8_t test_val = 0xAE;
    this->write(DATA, test_val);

    spins = 0;
    while (!this->is_data_ready() && spins++ < max_spins) {
        asm volatile("pause");
    }

    if (this->read(DATA) != test_val) {
        // Loopback test failed; consider the UART unusable.
        return false;
    }

    // Leave loopback mode: keep RTS/DTR/OUT2 enabled for normal operation.
    this->write(MODEM_CONTROL, MODEM_RTS | MODEM_DTR | MODEM_OUT2);
    return true;
}

}  // namespace kernel::hal