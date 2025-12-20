#pragma once

#include "hal/interface/interrupt.hpp"
#include "libs/min_heap.hpp"
#include "libs/spinlock.hpp"
#include "task/process.hpp"

#define MLFQ_LEVELS               32
#define PRIORITY_BOOST_INTERVAL   1000
#define STARVATION_CHECK_INTERVAL 100
#define GRIM_REAPER_INTERVAL      2000

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
    void reap_zombies();
    cpu::IrqStatus tick();

    void init(uint32_t id);
    static Scheduler& get();

   private:
    void save_fpu(std::byte* fpu);
    void restore_fpu(std::byte* fpu);

    void scan_for_starvation();
    bool check_for_higher_priority(int curr_level);

    Thread* get_next_thread();
    Thread* try_steal();

    uint32_t cpu_id;
    uint32_t active_queues_bitmap;
    volatile size_t current_ticks = 0;

    IntrusiveList<Thread, SchedulerTag>* ready_queue;
    IntrusiveList<Thread, SchedulerTag> zombie_list;
    MinHeap<Thread*> sleeping_queue;

    SpinLock zombie_lock;
    SpinLock lock;
};

void register_reschedule_handler();
}  // namespace kernel::task