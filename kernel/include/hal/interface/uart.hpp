/**
 * @file uart.hpp
 * @brief Hardware Abstraction Layer interface for UART peripherals.
 *
 * This header declares a minimal, blocking UART (Universal Asynchronous
 * Receiver/Transmitter) interface used by the kernel's Hardware Abstraction
 * Layer (HAL).
 *
 * The goal of this interface is to:
 *  - Provide a stable, hardware-agnostic contract for UART drivers.
 *  - Allow higher-level code (logging, CLI, debug console, boot output, etc.)
 *    to perform basic serial I/O without depending on a specific MCU or SoC.
 *  - Keep the API small and easy to implement on bare-metal targets.
 *
 * ### Design notes
 *
 * - **Blocking I/O**:
 *   All data transfer operations are specified as blocking. Implementations
 *   must not return from `send_char()` until the character has been accepted
 *   by the hardware transmit logic, and `recieve_char()` must wait until a
 *   character is available. This simplifies usage in early boot.
 *
 * - **Framing / configuration**:
 *   Unless otherwise documented by a concrete implementation, UARTs are
 *   expected to be configured for 8 data bits, no parity, 1 stop bit (8N1)
 *   using the provided baud rate. Other framing options can be supported in
 *   extended, implementation-specific APIs.
 *
 * - **Lifetime**:
 *   Instances are typically owned by platform-specific code and passed around
 *   via references or pointers.
 *
 * - **Thread-safety / concurrency**:
 *   This interface does not define any locking or concurrency guarantees.
 *   Implementations may be used from a single execution context (e.g. single
 *   core, no preemption) unless they explicitly document stronger guarantees.
 *
 * - **Portability**:
 *   Different hardware backends (MMIO UART, USB-CDC, emulated UART in a
 *   simulator, etc.) can provide their own `IUART` implementations, enabling
 *   the same higher-level code to run unmodified across platforms.
 */
#pragma once

#include <cstdint>
#include <cstddef>

namespace kernel::hal {
/**
 * @brief Interface for a hardware UART (Universal Asynchronous Receiver/Transmitter).
 *
 * This interface defines the basic operations required to configure and use a UART
 * device in a blocking manner. Implementations are expected to provide hardware-
 * specific behavior for initialization and data transfer, typically using 8N1 framing.
 *
 * A concrete implementation must ensure:
 *  - `init()` can be called at least once before any other operation.
 *  - `send_char()` transmits a single character, blocking as needed.
 *  - `recieve_char()` blocks until a character is available and then returns it.
 *  - `is_data_ready()` reports whether at least one character can be read
 *    without blocking.
 *  - `is_tx_ready()` reports whether a new character can be queued for
 *    transmission without blocking.
 */
class IUART {
   public:
    /**
     * @brief Initialize the UART peripheral.
     *
     * Configures the underlying UART hardware with the specified baud rate
     * using the standard 8 data bits, no parity, 1 stop bit (8N1) configuration,
     * unless the concrete implementation documents otherwise.
     *
     * This function should be called before any other member function is used.
     *
     * @param baud_rate Desired baud rate in bits per second (e.g. 115200).
     * @return true if the UART was successfully initialized, false otherwise.
     */
    virtual bool init(uint32_t baud_rate) = 0;

    /**
     * @brief Transmit a single character (blocking).
     *
     * This function must block until the given character has been written
     * to the UART transmit register or buffer. If the hardware provides
     * a FIFO or buffer, this call may return once the character is safely
     * queued for transmission.
     *
     * @param c Character to send.
     */
    virtual void send_char(char c) = 0;

    /**
     * @brief Receive a single character (blocking).
     *
     * This function must block until a character is available in the UART
     * receive buffer and then return it. Implementations may perform basic
     * error handling (e.g. framing errors) according to their requirements;
     * such behavior should be documented by the implementation.
     *
     * @return The received character.
     */
    virtual char recieve_char() = 0;

    /**
     * @brief Check if a character is available to be read.
     *
     * This is a non-blocking query that allows callers to avoid blocking
     * on `recieve_char()` when no data is present.
     *
     * @return true if at least one character is waiting in the receive buffer,
     *         false otherwise.
     */
    virtual bool is_data_ready() = 0;

    /**
     * @brief Check if the transmit buffer is ready for a new character.
     *
     * This is a non-blocking query that allows callers to avoid blocking
     * on `send_char()` when the transmitter is currently busy.
     *
     * @return true if a new character can be written to the transmit buffer,
     *         false otherwise.
     */
    virtual bool is_tx_ready() = 0;

    /**
     * @brief Helper function to send a null-terminated string (blocking).
     *
     * Sends each character of the given C-string sequentially by calling
     * `send_char()` until the terminating null character is reached.
     *
     * This function does not append any additional line endings and assumes
     * the pointer is valid and points to a null-terminated string.
     *
     * @param str Pointer to a null-terminated C-string to send.
     */
    void send_string(const char* str) {
        for (size_t i = 0; str[i] != '\0'; ++i) {
            this->send_char(str[i]);
        }
    }
};
}  // namespace kernel::hal