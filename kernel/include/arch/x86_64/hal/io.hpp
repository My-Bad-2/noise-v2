/**
 * @file io.hpp
 * @brief Low-level x86_64 I/O port access helpers.
 *
 * This header provides thin wrappers around the `in` and `out` x86 I/O
 * instructions. They are intended for use by HAL components that need to
 * talk to legacy devices through the I/O port space (e.g. 16550 UARTs,
 * PIC, PIT).
 *
 * All functions are inline templates around inline assembly, and are
 * inherently architecture-specific and unsafe if misused.
 */

#pragma once

#include <cstdint>
#include <concepts>

namespace kernel::hal {

/**
 * @brief Read a value from an I/O port.
 *
 * The size of the read is determined by the template parameter `T`, which
 * must be `uint8_t`, `uint16_t`, or `uint32_t`. The function issues the
 * appropriate `inb`, `inw`, or `inl` instruction.
 *
 * @tparam T Unsigned integer type up to 32 bits.
 * @param port I/O port number.
 * @return Value read from the port.
 */
template <typename T>
    requires(sizeof(T) <= sizeof(uint32_t))
inline T in(uint16_t port) {
    T ret = 0;

    // NOLINTBEGIN
    // Ignore `bugprone-branch-clone` check
    if constexpr (std::same_as<T, uint8_t>) {
        asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    } else if constexpr (std::same_as<T, uint16_t>) {
        asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    } else if constexpr (std::same_as<T, uint32_t>) {
        asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    } else {
        static_assert(false, "Unsupported type size for hal::in() operation.");
    }
    // NOLINTEND

    return ret;
}

/**
 * @brief Write a value to an I/O port.
 *
 * The size of the write is determined by the template parameter `T`, which
 * must be `uint8_t`, `uint16_t`, or `uint32_t`. The function issues the
 * appropriate `outb`, `outw`, or `outl` instruction.
 *
 * @tparam T Unsigned integer type up to 32 bits.
 * @param port I/O port number.
 * @param value Value to write to the port.
 */
template <typename T>
    requires(sizeof(T) <= sizeof(uint32_t))
inline void out(uint16_t port, T value) {
    // NOLINTBEGIN
    // Ignore `bugprone-branch-clone` check
    if constexpr (std::same_as<T, uint8_t>) {
        asm volatile("outb %0, %1" ::"a"(value), "Nd"(port));
    } else if constexpr (std::same_as<T, uint16_t>) {
        asm volatile("outw %0, %1" ::"a"(value), "Nd"(port));
    } else if constexpr (std::same_as<T, uint32_t>) {
        asm volatile("outl %0, %1" ::"a"(value), "Nd"(port));
    } else {
        static_assert(false, "Unsupported type size for hal::out() operation.");
    }
    // NOLINTEND
}

/**
 * @brief Short I/O delay helper.
 *
 * Performs a small delay by writing to an unused port (0x80). This is a
 * common technique on x86 to give slow devices time to settle after an
 * I/O operation, especially in early boot code.
 */
inline void io_wait() {
    out<uint8_t>(0x80, 0);
}

}  // namespace kernel::hal