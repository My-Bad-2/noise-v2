/**
 * @file memory.hpp
 * @brief Core memory utilities and higher-half direct-map helpers.
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace kernel::memory {
namespace __details {
/// Higher-half direct-map offset (set during memory::init()).
extern uintptr_t hhdm_offset;
}  // namespace __details

enum class PageSize : uint8_t {
    Size4K,
    Size2M,
    Size1G,
};

constexpr size_t PAGE_SIZE_4K = 4096ull;
constexpr size_t PAGE_SIZE_2M = 2 * 1024ull * 1024;
constexpr size_t PAGE_SIZE_1G = 1 * 1024ull * 1024 * 1024;

#ifndef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#endif

/// Check whether an address lies in the higher-half area.
inline constexpr bool is_higher_half(uintptr_t val) {
    return val >= __details::hhdm_offset;
}

template <typename T>
inline constexpr T to_higher_half(T val) {
    uintptr_t addr = uintptr_t(val);
    // Add HHDM offset only for low-half addresses.
    return is_higher_half(addr) ? val : (T)(addr + __details::hhdm_offset);
}

template <typename T>
inline constexpr T from_higher_half(T val) {
    uintptr_t addr = uintptr_t(val);
    // Subtract HHDM offset only for higher-half addresses.
    return !is_higher_half(addr) ? val : (T)(addr - __details::hhdm_offset);
}

#ifndef __clang__
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif

/**
 * @brief Initialize memory subsystem.
 *
 * Sets the higher-half direct-map offset from the bootloader and
 * initializes the physical memory manager.
 */
void init();

}  // namespace kernel::memory