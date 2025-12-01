/**
 * @file uart.hpp
 * @brief x86_64-specific implementation of the HAL UART interface.
 *
 * This header declares the `hal::UART16550` class, a concrete implementation
 * of the generic `hal::IUart` interface for 16550-compatible UARTs on x86_64.
 * It uses legacy I/O ports (e.g. COM1..COM4) and is typically used for early
 * boot logging and debug consoles.
 */
#pragma once

#include "hal/interface/uart.hpp"

/**
 * @defgroup Base I/O Port Address
 * @brief Base I/O port addresses for standard PC-compatible COM ports.
 *
 * These constants are the conventional port bases for 16550 UARTs on
 * PC hardware. Only COM1 is enabled by default via the constructor
 * default argument.
 * @{
 */
#define COM1_PORT 0x3F8
#define COM2_PORT 0x2E8
#define COM3_PORT 0x3E8
#define COM4_PORT 0x2E8
/** @} */

namespace kernel::hal {

/**
 * @brief 16550-compatible UART implementation for x86_64.
 *
 * This class wraps a single 16550 (or compatible) UART accessed through
 * legacy I/O ports. It implements the blocking `IUart` API, making it
 * suitable for low-level logging and simple serial consoles.
 *
 * Usage example:
 * @code
 * hal::UART16550 uart{COM1_PORT};
 * if (uart.init(115200)) {
 *     uart.send_string("Hello, world!\n");
 * }
 * @endcode
 *
 * The implementation assumes an input clock of 115200 Hz for baud divisor
 * calculation, which is typical on PC-compatible hardware.
 */
class UART16550 : public IUART {
   public:
    /**
     * @brief Construct a UART driver bound to a specific I/O port base.
     *
     * @param port Base I/O port of the UART (e.g. COM1_PORT).
     */
    explicit UART16550(uint16_t port = COM1_PORT) : port_base(port) {}

    /**
     * @brief Initialize the UART hardware.
     *
     * Programs the baud rate, configures 8N1 framing, enables and clears
     * FIFOs, and performs a simple loopback self-test. The port is left in
     * normal (non-loopback) mode on success.
     *
     * @param baud_rate Desired baud rate (e.g. 115200).
     * @return true if initialization and loopback test succeeded, false otherwise.
     */
    bool init(uint32_t baud_rate) override;

    /**
     * @brief Send a single character (blocking).
     *
     * Busy-waits until the transmitter holding register is empty, then writes
     * the character to the data register.
     *
     * @param c Character to transmit.
     */
    void send_char(char c) override;

    /**
     * @brief Receive a single character (blocking).
     *
     * Busy-waits until data is available in the receiver buffer, then reads
     * and returns it.
     *
     * @return The received character.
     */
    char recieve_char() override;

    /**
     * @brief Check if a character is available to read.
     *
     * @return true if the UART indicates at least one byte is present.
     */
    bool is_data_ready() override;

    /**
     * @brief Check if the transmitter can accept a new character.
     *
     * @return true if the transmitter holding register is empty.
     */
    bool is_tx_ready() override;

   private:
    /**
     * @brief Write a byte to a UART register.
     *
     * @param reg  Register offset from the base port.
     * @param value Value to write.
     */
    void write(uint16_t reg, uint8_t value) const;

    /**
     * @brief Read a byte from a UART register.
     *
     * @param reg Register offset from the base port.
     * @return The value read from the register.
     */
    uint8_t read(uint16_t reg) const;

    /// Base I/O port for this UART instance (e.g. 0x3F8 for COM1).
    uint16_t port_base;
};

}  // namespace kernel::hal