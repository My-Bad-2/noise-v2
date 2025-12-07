#include "hal/timer.hpp"
#include "hal/pit.hpp"
#include "hal/lapic.hpp"

namespace kernel::hal {
void Timer::udelay(uint32_t us) {
    if (Lapic::is_ready()) {
        Lapic::udelay(us);
    } else {
        PIT::udelay(us);
    }
}

void Timer::mdelay(uint32_t ms) {
    if (Lapic::is_ready()) {
        Lapic::mdelay(ms);
    } else {
        PIT::mdelay(ms);
    }
}

size_t Timer::get_ticks_ns() {
    if (Lapic::is_ready()) {
        return Lapic::get_ticks_ns();
    }

    return 0;
}
}  // namespace kernel::hal