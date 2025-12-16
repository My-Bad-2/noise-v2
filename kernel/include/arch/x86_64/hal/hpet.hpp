#pragma once

#include "hal/mmio.hpp"

namespace kernel::hal {
class HPET {
   private:
    static MMIORegion hpet_base;
    static uint32_t period_fs;  // Timer period in femtoseconds (from capabilities).
    static uint8_t num_timers;
    static bool available;

   public:
    static void init();

    static bool is_available() {
        return available;
    }

    static size_t get_ns();
    static size_t read_counter();

    static void ndelay(size_t ns);
    static void udelay(size_t us);
    static void mdelay(size_t ms);

    static bool enable_periodic_timer(uint8_t timer_idx, size_t hz, uint8_t irq_gsi);
    static bool enable_oneshot_timer(uint8_t timer_idx, size_t us_delay, uint8_t irq_gsi);

   private:
    static void write(size_t reg, size_t val);
    static size_t read(size_t reg);

    static void write_timer(uint8_t index, size_t offset, size_t val);
    static size_t read_timer(uint8_t index, size_t offset);
};
}  // namespace kernel::hal