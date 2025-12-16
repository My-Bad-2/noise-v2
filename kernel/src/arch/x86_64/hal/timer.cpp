#include "hal/timer.hpp"
#include "cpu/exception.hpp"
#include "hal/interface/interrupt.hpp"
#include "hal/interrupt.hpp"
#include "hal/pit.hpp"
#include "hal/lapic.hpp"
#include "hal/hpet.hpp"
#include "libs/log.hpp"

namespace kernel::hal {
namespace {
class TimerScheduler : public cpu::IInterruptHandler {
   public:
    inline void set_callback(cpu::IrqStatus (*call)()) {
        this->callback = call;
    }

    const char* name() const override {
        return "Timer";
    }

    cpu::IrqStatus handle(cpu::arch::TrapFrame*) override {
        return this->callback();
    }

   private:
    cpu::IrqStatus (*callback)();
};

TimerScheduler timer;

bool setup_lapic(uint8_t vector, uint32_t period_ms, TimerMode mode) {
    Lapic::configure_timer(vector, mode);

    if (mode == TimerMode::TscDeadline) {
        size_t delta_ticks = static_cast<size_t>(period_ms) * Lapic::get_ticks_ms();
        Lapic::arm_tsc_deadline(Lapic::rdtsc() + delta_ticks);
    } else {
        uint32_t ticks = period_ms * Lapic::get_ticks_ms();
        Lapic::start_timer(ticks);
    }

    cpu::arch::InterruptDispatcher::register_handler(vector, &timer, true);

    return true;
}

bool setup_hpet(uint8_t vector, uint32_t period_ms, TimerMode mode) {
    const uint8_t hpet_gsi = 2;

    cpu::arch::InterruptDispatcher::map_pci_irq(hpet_gsi, vector, &timer, 0, true);

    if (mode == TimerMode::Periodic) {
        size_t hz = 1000 / static_cast<size_t>(period_ms);

        if (hz == 0) {
            hz = 1;
        }

        return HPET::enable_periodic_timer(0, hz, hpet_gsi);
    } else if (mode == TimerMode::OneShot) {
        size_t delay_us = static_cast<size_t>(period_ms) * 1000;
        return HPET::enable_oneshot_timer(0, delay_us, hpet_gsi);
    }

    return false;
}

bool setup_pit(uint8_t vector, uint32_t period_ms, TimerMode mode) {
    cpu::arch::InterruptDispatcher::map_legacy_irq(0, vector, &timer, 0, true);

    if (mode == TimerMode::Periodic) {
        uint32_t hz = 1000 / period_ms;

        if (hz == 0) {
            hz = 1;
        }

        PIT::configure_periodic(hz);
    } else {
        PIT::configure_oneshot(period_ms);
    }

    return true;
}
}  // namespace

uint8_t Timer::curr_vector = 0;

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

void Timer::stop() {
    Lapic::stop_timer();

    if (curr_vector != 0) {
        cpu::arch::InterruptDispatcher::unmap_legacy_irq(0, curr_vector);
    }
}

bool Timer::configure_timer(uint32_t period_ms, TimerMode mode, uint8_t vector,
                            cpu::IrqStatus (*callback)()) {
    if (period_ms == 0) {
        return false;
    }

    stop();

    curr_vector = vector;
    timer.set_callback(callback);

    if (Lapic::is_ready()) {
        if (setup_lapic(curr_vector, period_ms, mode)) {
            return true;
        }
    }

    if (HPET::is_available()) {
        if (setup_hpet(curr_vector, period_ms, mode)) {
            return true;
        }
    }

    if (mode != TimerMode::TscDeadline) {
        if (setup_pit(vector, period_ms, mode)) {
            return true;
        }
    }

    return false;
}
}  // namespace kernel::hal