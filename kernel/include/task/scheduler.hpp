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

    void init(uint32_t id);
    static Scheduler& get();

   private:
    Thread* get_next_thread();
    bool check_for_higher_priority(int curr_level);
    Thread* try_steal();

    SpinLock lock;
    Deque<Thread*>* ready_queue;
    MinHeap<Thread*> sleeping_queue;
    uint32_t cpu_id;
    uint32_t active_queues_bitmap;
    volatile size_t current_ticks = 0;
};
}  // namespace kernel::task