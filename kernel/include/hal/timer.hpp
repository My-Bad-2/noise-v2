#pragma once

#include "hal/interface/interrupt.hpp"
#include "libs/min_heap.hpp"

namespace kernel::hal {
enum TimerMode {
    TscDeadline,
    OneShot,
    Periodic,
};

using TimerCallback = void (*)(void*);

struct TimerEvent {
    size_t expiration_ticks;
    size_t interval;
    TimerMode mode;

    TimerCallback callback;
    void* data;
    uint32_t id;

    bool operator<(const TimerEvent& other) const {
        return this->expiration_ticks < other.expiration_ticks;
    }
};

class TimerManager {
   public:
    TimerManager() = default;

    uint32_t schedule(TimerMode mode, size_t ticks, TimerCallback callback, void* data);
    void tick();

    bool cancel(uint32_t timer_id);

    inline size_t get_current_tick() const {
        return this->current_tick;
    }

    inline size_t pending_count() const {
        return this->events.size();
    }

   private:
    size_t current_tick;
    uint32_t next_timer_id;
    MinHeap<TimerEvent> events;
};

class Timer : public cpu::IInterruptHandler {
   public:
    const char* name() const override {
        return "Timer";
    }

    cpu::IrqStatus handle(cpu::arch::TrapFrame* frame) override;

    uint32_t schedule(TimerMode mode, size_t ticks, TimerCallback callback, void* data) {
        return this->manager->schedule(mode, ticks, callback, data);
    }

    static size_t get_ticks_ns();
    
    static void udelay(uint32_t us);
    static void mdelay(uint32_t ms);
    
    static void init();
    static Timer& get();

   private:
    static void stop();
    TimerManager* manager;
};
}  // namespace kernel::hal