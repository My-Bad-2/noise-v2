#include "task/scheduler.hpp"
#include "hal/lapic.hpp"
#include "hal/interrupt.hpp"

namespace kernel::task {
void Scheduler::init() {
    cpu::arch::InterruptDispatcher::register_handler(32, this, true);
    hal::Lapic::configure_timer(32, hal::Periodic);
    const uint32_t ticks = hal::Lapic::get_ticks_ms();
    hal::Lapic::start_timer_legacy(ticks);
}
}  // namespace kernel::task