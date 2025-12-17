#include "hal/timer.hpp"

namespace kernel::hal {
uint32_t TimerManager::schedule(TimerMode mode, size_t tick, TimerCallback callback, void* data) {
    TimerEvent ev;

    if (mode == Periodic) {
        if (tick == 0) {
            tick = 1;
        }

        ev.interval = tick;
    } else {
        ev.interval = 0;
    }

    if (mode == TscDeadline) {
        ev.expiration_ticks = tick;
    } else {
        ev.expiration_ticks = this->current_tick + tick;
    }

    ev.mode     = mode;
    ev.callback = callback;
    ev.data     = data;
    ev.id       = this->next_timer_id++;

    this->events.insert(ev);

    return ev.id;
}

void TimerManager::tick() {
    this->current_tick++;

    while (!this->events.empty()) {
        if (this->events.top().expiration_ticks > this->current_tick) {
            break;
        }

        TimerEvent current;

        if (!this->events.extract_min(current)) {
            break;
        }

        if (current.callback) {
            current.callback(current.data);
        }

        // If periodic, calculate new expiration time and re-insert
        if (current.mode == TimerMode::Periodic) {
            current.expiration_ticks += current.interval;
            this->events.insert(current);
        }
    }
}

bool TimerManager::cancel(uint32_t timer_id) {
    return this->events.erase_if([timer_id](const TimerEvent& ev) { return ev.id == timer_id; });
}
}  // namespace kernel::hal