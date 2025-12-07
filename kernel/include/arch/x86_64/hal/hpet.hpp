#pragma once

#include "hal/mmio.hpp"

namespace kernel::hal {
/**
 * @brief High Precision Event Timer (HPET) abstraction.
 *
 * HPET provides a higher-resolution, memory-mapped timer than the
 * legacy PIT/LAPIC combination. This wrapper:
 *  - Discovers the HPET MMIO block via ACPI (HPET table).
 *  - Exposes a monotonic counter in (approximate) nanoseconds.
 *  - Provides helpers for one-shot and periodic timers.
 *
 * It is intended as an optional, higher-quality time source; systems
 * without HPET simply report `is_available() == false`.
 */
class HPET {
   private:
    static MMIORegion hpet_base;
    static uint32_t period_fs;  ///< Timer period in femtoseconds (from capabilities).
    static uint8_t num_timers;  ///< Number of timer comparators exposed by this HPET.
    static bool available;      ///< Whether init() succeeded and the block is usable.

   public:
    /**
     * @brief Discover and initialize the HPET block using ACPI tables.
     *
     * This maps the HPET MMIO region, reads capabilities (period, timer
     * count), and enables the main counter. If anything fails, HPET is
     * marked unavailable and higher layers must rely on PIT/LAPIC.
     */
    static void init();

    /// Return whether a usable HPET instance was found and initialized.
    static bool is_available() {
        return available;
    }

    /**
     * @brief Return a coarse monotonic time value in nanoseconds.
     *
     * The value is derived from the HPET main counter and the advertised
     * femtosecond period. On systems without HPET, returns 0.
     */
    static size_t get_ns();

    /// Read the raw HPET main counter value.
    static size_t read_counter();

    /// Busy-wait for approximately @p ns nanoseconds using HPET.
    static void ndelay(size_t ns);
    /// Busy-wait for approximately @p us microseconds using HPET.
    static void udelay(size_t us);
    /// Busy-wait for approximately @p ms milliseconds using HPET.
    static void mdelay(size_t ms);

    /**
     * @brief Configure a periodic HPET timer.
     *
     * Sets up timer @p timer_idx to generate interrupts at @p hz using
     * global system interrupt @p irq_gsi.
     *
     * Returns false if HPET is not available, the timer index is invalid,
     * the frequency is zero, or the selected timer lacks periodic mode.
     */
    static bool enable_periodic_timer(uint8_t timer_idx, size_t hz, uint8_t irq_gsi);

    /**
     * @brief Configure a one-shot HPET timer.
     *
     * Arms timer @p timer_idx to fire once after @p us_delay microseconds
     * on @p irq_gsi. Returns false if HPET or the timer index is invalid.
     */
    static bool enable_oneshot_timer(uint8_t timer_idx, size_t us_delay, uint8_t irq_gsi);

   private:
    static void write(size_t reg, size_t val);
    static size_t read(size_t reg);

    static void write_timer(uint8_t index, size_t offset, size_t val);
    static size_t read_timer(uint8_t index, size_t offset);
};
}  // namespace kernel::hal