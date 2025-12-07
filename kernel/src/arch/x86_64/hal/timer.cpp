#include "hal/timer.hpp"
#include "hal/pit.hpp"
#include "hal/lapic.hpp"
#include "libs/log.hpp"

namespace kernel::hal {
void Timer::udelay(uint32_t us) {
    if (Lapic::is_ready()) {
        // Prefer LAPIC-based delays once calibration is complete.
        Lapic::udelay(us);
    } else {
        // Fallback to PIT for early-boot or non-calibrated scenarios.
        LOG_DEBUG("Timer: using PIT udelay (%u us) because LAPIC is not ready", us);
        PIT::udelay(us);
    }
}

void Timer::mdelay(uint32_t ms) {
    if (Lapic::is_ready()) {
        Lapic::mdelay(ms);
    } else {
        LOG_DEBUG("Timer: using PIT mdelay (%u ms) because LAPIC is not ready", ms);
        PIT::mdelay(ms);
    }
}

size_t Timer::get_ticks_ns() {
    if (Lapic::is_ready()) {
        return Lapic::get_ticks_ns();
    }

    // No calibrated time source yet; callers should treat 0 as "unknown".
    return 0;
}
}  // namespace kernel::hal