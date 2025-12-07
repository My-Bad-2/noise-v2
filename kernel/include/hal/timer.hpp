#pragma once

#include <cstddef>

namespace kernel::hal {
class Timer {
   public:
    static void init();
    static void udelay(size_t us);
    static void mdelay(size_t ms);
};
}  // namespace kernel::hal