#include "hal/timer.hpp"
#include "cpu/exception.hpp"
#include "hal/interface/interrupt.hpp"
#include "hal/interrupt.hpp"
#include "hal/pit.hpp"
#include "hal/lapic.hpp"
#include "hal/hpet.hpp"
#include "libs/log.hpp"
#include "task/scheduler.hpp"

#define TIMER_VECTOR 32

namespace kernel::hal {
namespace {
void setup_lapic(uint32_t period_ms, cpu::IInterruptHandler* handler) {
    cpu::arch::InterruptDispatcher::register_handler(TIMER_VECTOR, handler, true);

    Lapic::configure_timer(TIMER_VECTOR, Periodic);

    uint32_t ticks = period_ms * Lapic::get_ticks_ms();
    Lapic::start_timer(ticks);
}

void setup_hpet(uint32_t period_ms, cpu::IInterruptHandler* handler) {
    const uint8_t hpet_gsi = 2;

    cpu::arch::InterruptDispatcher::map_pci_irq(hpet_gsi, TIMER_VECTOR, handler, 0, true);

    size_t hz = 1000 / static_cast<size_t>(period_ms);

    if (hz == 0) {
        hz = 1;
    }

    HPET::enable_periodic_timer(0, hz, hpet_gsi);
}

void setup_pit(uint32_t period_ms, cpu::IInterruptHandler* handler) {
    cpu::arch::InterruptDispatcher::map_legacy_irq(0, TIMER_VECTOR, handler, 0, true);

    uint32_t hz = 1000 / period_ms;

    if (hz == 0) {
        hz = 1;
    }

    PIT::configure_periodic(hz);
}
}  // namespace

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
    // calibrated; it is cheap and monotonic
    if (Lapic::is_ready()) {
        return Lapic::get_ticks_ns();
    }

    // Fall back to HPET when available for a shared monotonic timebase.
    if (HPET::is_available()) {
        return HPET::get_ns();
    }

    // No calibrated time source yet
    return 0;
}

void Timer::stop() {
    Lapic::stop_timer();

    cpu::arch::InterruptDispatcher::unmap_legacy_irq(0, TIMER_VECTOR);
}

cpu::IrqStatus Timer::handle(cpu::arch::TrapFrame* frame) {
    this->manager->tick();

    task::Scheduler& sched = task::Scheduler::get();
    return sched.tick();
}

void Timer::init() {
    Timer& timer  = timer.get();
    timer.manager = new TimerManager;

    if (Lapic::is_ready()) {
        setup_lapic(1, &timer);
    } else if (HPET::is_available()) {
        setup_hpet(1, &timer);
    } else {
        LOG_DEBUG("Timer: using PIT because LAPIC/HPET are not usable");
        setup_pit(1, &timer);
    }
}

Timer& Timer::get() {
    static Timer timer;
    return timer;
}
}  // namespace kernel::hal