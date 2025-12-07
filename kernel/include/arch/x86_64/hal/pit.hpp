#pragma once

#include <cstdint>

namespace kernel::hal {
class PIT {
   public:
    static void init(uint32_t frequency);
    static void set_frequency(uint32_t frequency);
    static void disable();

    static void prepare_wait(uint16_t ms);
    static uint16_t read_count();

    static bool check_expiration();
    static void wait_ticks(uint16_t ticks);
    static void udelay(uint32_t us);
    static void mdelay(uint32_t ms);
};
}  // namespace kernel::hal