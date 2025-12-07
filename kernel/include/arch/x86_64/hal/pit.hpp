#pragma once

#include <cstdint>

namespace kernel::hal {
/**
 * @brief Programmable Interval Timer (PIT) helper.
 *
 * The PIT is kept around primarily as:
 *  - A conservative timebase for calibrating the LAPIC/TSC.
 *  - A fallback delay mechanism when LAPIC timing is not yet ready.
 *
 * Direct PIT usage is limited to HAL code; higher layers typically use
 * `hal::Timer`, which chooses between LAPIC and PIT automatically.
 */
class PIT {
   public:
    static void init(uint32_t frequency);
    static void set_frequency(uint32_t frequency);
    static void disable();

    /// Prepare for a blocking wait of @p ms by configuring the PIT counter.
    static void prepare_wait(uint16_t ms);
    /// Read the current down-counter value (used for elapsed-time detection).
    static uint16_t read_count();

    static bool check_expiration();
    static void wait_ticks(uint16_t ticks);
    static void udelay(uint32_t us);
    static void mdelay(uint32_t ms);
};
}  // namespace kernel::hal