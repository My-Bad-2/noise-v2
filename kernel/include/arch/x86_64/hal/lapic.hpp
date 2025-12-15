#pragma once

#include "hal/mmio.hpp"

namespace kernel::hal {
/**
 * @brief LAPIC timer operating modes.
 *
 * These are abstract modes exposed to higher layers; the LAPIC
 * implementation translates them into xAPIC/x2APIC LVT encodings and
 * falls back to the closest behavior hardware supports.
 */
enum TimerMode : uint8_t {
    OneShort,
    Periodic,
    TSCDeadline,
};

/**
 * @brief Local APIC (LAPIC/x2APIC) abstraction.
 *
 * This wrapper:
 *  - Hides MMIO vs MSR differences between xAPIC and x2APIC.
 *  - Centralizes IPI sending and EOI signaling.
 *  - Exposes calibrated micro/millisecond delay helpers, so the rest of
 *    the kernel does not need to know about LAPIC tick or TSC rates.
 *
 * Readiness:
 *  - `is_ready()` tells clients whether the LAPIC timer has been
 *    calibrated; until then, `Timer` will fall back to the PIT.
 */
class Lapic {
   public:
    static void init();

    static uint32_t get_id();
    static void send_eoi();

    static void send_ipi(uint32_t dest_id, uint8_t vector);
    static void send_init_sipi(uint32_t dest_id, uint8_t page);
    static void broadcast_ipi(uint8_t vector);

    static void configure_timer(uint8_t vector, TimerMode mode);
    static void start_timer_legacy(uint32_t count);
    static void arm_tsc_deadline(uint64_t target_tsc);
    static void stop_timer();

    /// Calibrate LAPIC and TSC against a known time base.
    static void calibrate();
    /// Busy-wait for the requested number of microseconds.
    static void udelay(uint32_t us);
    /// Busy-wait for the requested number of milliseconds.
    static void mdelay(uint32_t ms);

    /// Report whether calibration has completed successfully.
    static bool is_ready() {
        return is_calibrated;
    }

    /// Return a coarse timestamp in nanoseconds derived from the TSC.
    static uint64_t get_ticks_ns();

    static const uint32_t get_ticks_ms() {
        return ticks_per_ms;
    }

   private:
    static uint32_t read(uint32_t offset);
    static void write(uint32_t offset, uint32_t value);

    static void perform_calibration_race(void (*callback)());
    static void calibrate_with_pit();
    static void calibrate_with_hpet();

    static MMIORegion lapic_base;

    static bool x2apic_active;
    static bool tsc_deadline_supported;
    static bool is_calibrated;

    /// LAPIC timer ticks per millisecond and microsecond.
    static uint32_t ticks_per_ms;
    static uint32_t ticks_per_us;
    /// TSC ticks per millisecond (used for get_ticks_ns()).
    static size_t tsc_per_ms;
};
}  // namespace kernel::hal