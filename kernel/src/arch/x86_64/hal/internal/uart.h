#pragma once

#include <stdint.h>

/**
 * @file uart.h
 * @brief Internal register definitions for 16550-compatible UARTs.
 *
 * This header is internal to the x86_64 UART HAL implementation. It
 * exposes raw register offsets and bit masks for the 16550 family of
 * UARTs accessed via I/O ports.
 *
 * The higher-level UART driver (`kernel::hal::UART16550`) uses these
 * constants together with the port I/O helpers in `hal/io.hpp` to
 * implement the `IUart` interface.
 */

// 16550-compatible UART register offsets (from base I/O port).
#define DATA                     0  // RBR (read) / THR (write)
#define INTERRUPT                1  // IER
#define INTERRUPT_IDENTIFICATION 2  // IIR (read) / FCR (write)
#define LINE_CONTROL             3  // LCR
#define MODEM_CONTROL            4  // MCR
#define LINE_STATUS              5  // LSR
#define MODEM_STATUS             6  // MSR
#define SCRATCH                  7  // SCR

// Backwards-compatibility alias (typo retained for transitional code paths).
#define INTERRUPT_IDENTIFACTOR INTERRUPT_IDENTIFICATION

// Divisor latch registers when DLAB is set.
#define BAUD_RATE_LOW   DATA                      // DLL when DLAB=1
#define BAUD_RATE_HIGH  INTERRUPT                 // DLH when DLAB=1
#define FIFO_CONTROLLER INTERRUPT_IDENTIFICATION  // FCR when written

// LCR bits.
#define LINE_DS_5        0
#define LINE_DS_6        1
#define LINE_DS_7        2
#define LINE_DS_8        3
#define LINE_DLAB_STATUS (1 << 7)

// MCR bits.
#define MODEM_DTR      (1u << 0)
#define MODEM_RTS      (1u << 1)
#define MODEM_OUT1     (1u << 2)
#define MODEM_OUT2     (1u << 3)
#define MODEM_LOOPBACK (1u << 4)

// IER bits.
#define INTERRUPT_WHEN_DATA_AVAILABLE    (1u << 0)
#define INTERRUPT_WHEN_TRANSMITTER_EMPTY (1u << 1)
#define INTERRUPT_WHEN_BREAK_EMPTY       (1u << 2)
#define INTERRUPT_WHEN_STATUS_UPDATE     (1u << 3)

// LSR bits.
#define LINE_DATA_READY            (1u << 0)
#define LINE_OVERRUN_ERROR         (1u << 1)
#define LINE_PARITY_ERROR          (1u << 2)
#define LINE_FRAMING_ERROR         (1u << 3)
#define LINE_BREAK_INDICATOR       (1u << 4)
#define LINE_TRANSMITTER_BUF_EMPTY (1u << 5)
#define LINE_TRANSMITTER_EMPTY     (1u << 6)
#define LINE_IMPENDING_ERROR       (1u << 7)

// FCR bits and trigger levels.
#define ENABLE_FIFO         (1u << 0)
#define FIFO_CLEAR_RECEIVE  (1u << 1)
#define FIFO_CLEAR_TRANSMIT (1u << 2)
#define FIFO_ENABLE_64_BYTE (1u << 5)
#define FIFO_TRIGGER_LEVEL1 (0u << 6)
#define FIFO_TRIGGER_LEVEL2 (1u << 6)
#define FIFO_TRIGGER_LEVEL3 (2u << 6)
#define FIFO_TRIGGER_LEVEL4 (3u << 6)