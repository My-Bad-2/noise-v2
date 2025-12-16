#pragma once

#include <cstddef>
#include <cstdint>
#include "hal/interface/interrupt.hpp"

namespace kernel::hal {
enum TimerMode {
    TscDeadline,
    OneShot,
    Periodic,
};

class Timer {
   public:
    static size_t get_ticks_ns();
    static void udelay(uint32_t us);

    /// Busy-wait for the given number of milliseconds.
    static void mdelay(uint32_t ms);
    static bool configure_timer(uint32_t period_ms, TimerMode mode, uint8_t vector,
                                cpu::IrqStatus (*callback)());

   private:
    static void stop();
    static uint8_t curr_vector;
};
}  // namespace kernel::hal