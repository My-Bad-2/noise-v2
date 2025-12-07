#pragma once

#include <cstddef>
#include <cstdint>

namespace kernel::hal {
class Timer {
   public:
    static size_t get_ticks_ns();
    static void udelay(uint32_t us);
    static void mdelay(uint32_t ms);
};
}  // namespace kernel::hal