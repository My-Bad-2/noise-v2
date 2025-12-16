#pragma once

#include "libs/deque.hpp"
#include "hal/interface/interrupt.hpp"
#include "libs/spinlock.hpp"
#include "task/process.hpp"

#define MAX_PRIORITY    32
#define DEFAULT_QUANTUM 20

namespace kernel::task {
struct Scheduler {
   public:
    Scheduler() = default;

    void init();

    void yield();
    void schedule();

    void aging_tick();
    void block();
    void unblock(Thread* t);
    void terminate();

    void add_thread(Thread* t);
    cpu::IrqStatus tick();

    static Scheduler& get();

   private:
    Thread* get_next_thread();

    SpinLock lock;
    Deque<Thread*> ready_queue[MAX_PRIORITY];
};
}  // namespace kernel::task