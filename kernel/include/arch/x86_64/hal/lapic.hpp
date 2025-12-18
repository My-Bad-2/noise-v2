#pragma once

#include "hal/mmio.hpp"
#include "hal/timer.hpp"

namespace kernel::hal {
class Lapic {
   public:
    static void init();

    static uint32_t get_id();
    static void send_eoi();

    static void send_ipi(uint32_t dest_id, uint8_t vector);
    static void send_init_sipi(uint32_t dest_id, uint8_t page);
    static void broadcast_ipi(uint8_t vector);
    static void broadcast_ipi(uint8_t vector, bool self);

    static void configure_timer(uint8_t vector, TimerMode mode);
    static void start_timer(uint32_t count);
    static void arm_tsc_deadline(uint64_t target_tsc);
    static void stop_timer();

    static void calibrate();
    static void udelay(uint32_t us);
    static void mdelay(uint32_t ms);

    static bool is_ready() {
        return is_calibrated;
    }

    static uint64_t get_ticks_ns();

    static const uint32_t get_ticks_ms() {
        return ticks_per_ms;
    }

    static size_t rdtsc();

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

    static uint32_t ticks_per_ms;
    static uint32_t ticks_per_us;
    static size_t tsc_per_ms;
};
}  // namespace kernel::hal