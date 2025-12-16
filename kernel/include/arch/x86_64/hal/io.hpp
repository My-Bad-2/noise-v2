#pragma once

#include <cstdint>
#include <concepts>

namespace kernel::hal {
template <typename T>
    requires(sizeof(T) <= sizeof(uint32_t))
inline T in(uint16_t port) {
    T ret = 0;

    if constexpr (std::same_as<T, uint8_t>) {
        asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    } else if constexpr (std::same_as<T, uint16_t>) {
        asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    } else if constexpr (std::same_as<T, uint32_t>) {
        asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    } else {
        static_assert(false, "Unsupported type size for kernel::hal::in() operation.");
    }

    return ret;
}

template <typename T>
    requires(sizeof(T) <= sizeof(uint32_t))
inline void out(uint16_t port, T value) {
    if constexpr (std::same_as<T, uint8_t>) {
        asm volatile("outb %0, %1" ::"a"(value), "Nd"(port));
    } else if constexpr (std::same_as<T, uint16_t>) {
        asm volatile("outw %0, %1" ::"a"(value), "Nd"(port));
    } else if constexpr (std::same_as<T, uint32_t>) {
        asm volatile("outl %0, %1" ::"a"(value), "Nd"(port));
    } else {
        static_assert(false, "Unsupported type size for kernel::hal::out() operation.");
    }
}

inline void io_wait() {
    // Use port 0x80 (historically unused) for a tiny delay.
    out<uint8_t>(0x80, 0);
}
}  // namespace kernel::hal