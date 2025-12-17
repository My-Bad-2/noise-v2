#pragma once

#include "libs/deque.hpp"
#include "hal/interface/interrupt.hpp"
#include "libs/min_heap.hpp"
#include "libs/spinlock.hpp"
#include "task/process.hpp"

#define MLFQ_LEVELS             4
#define PRIORITY_BOOST_INTERVAL 1000

namespace kernel::task {
struct Scheduler {
   public:
    Scheduler() = default;

    void yield();
    void schedule();

    void boost_all();
    void block();
    void unblock(Thread* t);
    void terminate();
    void sleep(size_t ms);

    void add_thread(Thread* t);
    cpu::IrqStatus tick();

    static void init();
    static Scheduler& get();

   private:
    Thread* get_next_thread();
    bool check_for_higher_priority(int curr_level);

    SpinLock lock;
    Deque<Thread*> ready_queue[MLFQ_LEVELS];
    MinHeap<Thread*> sleeping_queue;
};
}  // namespace kernel::task