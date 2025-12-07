#pragma once

#include <cstddef>
#include <cstdint>

namespace kernel::hal {
/**
 * @brief High-level time/delay facade for the HAL.
 *
 * `Timer` chooses the most appropriate underlying time source.
 */
class Timer {
   public:
    /// Return a coarse, monotonic timestamp in nanoseconds if available.
    static size_t get_ticks_ns();

    /// Busy-wait for the given number of microseconds.
    static void udelay(uint32_t us);

    /// Busy-wait for the given number of milliseconds.
    static void mdelay(uint32_t ms);
};
}  // namespace kernel::hal