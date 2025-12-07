#include "hal/timer.hpp"
#include "hal/pit.hpp"
#include "hal/lapic.hpp"
#include "hal/hpet.hpp"
#include "libs/log.hpp"

namespace kernel::hal {
void Timer::udelay(uint32_t us) {
    if (Lapic::is_ready()) {
        // Priority 1: Local APIC (fastest, per-core, very low overhead).
        Lapic::udelay(us);
    } else if (HPET::is_available()) {
        // Priority 2: HPET (shared, precise, but MMIO-based).
        HPET::udelay(us);
    } else {
        // Priority 3: PIT as a last-resort delay source in early boot.
        LOG_DEBUG("Timer: using PIT udelay (%u us) because LAPIC/HPET are not usable", us);
        PIT::udelay(us);
    }
}

void Timer::mdelay(uint32_t ms) {
    if (Lapic::is_ready()) {
        Lapic::mdelay(ms);
    } else if (HPET::is_available()) {
        HPET::mdelay(ms);
    } else {
        LOG_DEBUG("Timer: using PIT mdelay (%u ms) because LAPIC/HPET are not usable", ms);
        PIT::mdelay(ms);
    }
}

size_t Timer::get_ticks_ns() {
    // Prefer per-core TSC-derived time when the LAPIC timer has been
    // calibrated; it is cheap and monotonic on modern CPUs.
    if (Lapic::is_ready()) {
        return Lapic::get_ticks_ns();
    }

    // Fall back to HPET when available for a shared monotonic timebase.
    if (HPET::is_available()) {
        return HPET::get_ns();
    }

    // No calibrated time source yet; callers should treat 0 as "unknown".
    return 0;
}
}  // namespace kernel::hal