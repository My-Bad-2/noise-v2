#include "task/scheduler.hpp"
#include "hal/smp_manager.hpp"
#include "hal/timer.hpp"

// Low-level context switch routine implemented in architecture-specific assembly.
extern "C" void context_switch(kernel::task::Thread* prev, kernel::task::Thread* next);

namespace kernel::task {
namespace {
// Generated using `misc/scripts/gen_time_quanta.py`
constexpr uint16_t TIME_SLICE_QUANTA[64] = {
    10,  11,   12,   13,   15,   16,   18,   19,   21,   24,   26,   29,   31,   35,   38,   42,
    46,  51,   56,   61,   67,   74,   81,   90,   98,   108,  119,  131,  144,  159,  174,  192,
    211, 232,  255,  281,  309,  340,  374,  411,  453,  498,  548,  602,  663,  729,  802,  882,
    970, 1067, 1174, 1291, 1420, 1562, 1719, 1891, 2080, 2288, 2516, 2768, 3045, 3349, 3684, 4053,
};
}  // namespace

void Scheduler::add_thread(Thread* t) {
    cpu::CpuCoreManager& cpu_manager = cpu::CpuCoreManager::get();
    cpu::PerCpuData* cpu             = cpu_manager.get_core_by_index(this->cpu_id);

    if (t->priority >= MLFQ_LEVELS) {
        t->priority = MLFQ_LEVELS - 1;
    }

    if (t->quantum <= 0) {
        t->quantum = TIME_SLICE_QUANTA[t->priority];
    }

    t->state = Ready;
    t->cpu   = cpu;

    {
        LockGuard guard(this->lock);

        // If `t` is already in a list, pushing it again will corrupt pointers and hang the kernel.
        if (is_linked<SchedulerTag>(*t)) {
            return;
        }

        this->ready_queue[t->priority].push_back(*t);
        this->active_queues_bitmap |= (1 << t->priority);
    }

    if (t->priority < cpu->curr_thread->priority) {
        cpu->reschedule_needed = true;

        // If this is running on a different core, send an IPI to wake it up
        if (cpu_manager.get_current_core()->core_idx != this->cpu_id) {
            cpu_manager.send_ipi(this->cpu_id, IPI_RESCHEDULE_VECTOR);
        }
    }
}

Thread* Scheduler::get_next_thread() {
    // Pick thread from the highest priority none-empty queue
    if (this->active_queues_bitmap != 0) {
        int highest_prio = __builtin_ctzll(this->active_queues_bitmap);

        Thread* t = &this->ready_queue[highest_prio].front();
        this->ready_queue[highest_prio].pop_front();

        if (this->ready_queue[highest_prio].empty()) {
            // We know that the bit is set, just xor it.
            this->active_queues_bitmap ^= (1u << highest_prio);
        }

        return t;
    }

    // Local Queues Empty? Try stealing
    if (Thread* stolen = this->try_steal()) {
        return stolen;
    }

    // Give up -> Idle
    return cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id)->idle_thread;
}

Thread* Scheduler::try_steal() {
    cpu::CpuCoreManager& cpu_manager = cpu::CpuCoreManager::get();
    // Iterate over all other CPUs
    size_t max_cpus = cpu_manager.get_total_cores();

    for (size_t i = 0; i < max_cpus; ++i) {
        size_t victim_id = (this->cpu_id + i) % max_cpus;

        if (victim_id == this->cpu_id) {
            // Don't steal from your own house
            continue;
        }

        cpu::PerCpuData* victim_cpu =
            cpu_manager.get_core_by_index(static_cast<uint32_t>(victim_id));
        Scheduler& victim_sched = victim_cpu->sched;

        // Use `try_lock()` to avoid deadlock
        // If core A tries to steal from B, and B from A, locking hangs the kernel.
        if (victim_sched.lock.try_lock()) {
            Thread* stolen = nullptr;

            // Take from the tail of the highest priority queue available.
            // Ideally, we want to help with the most important work, but taking from tail
            // minimizes cache thrashing for the victim.
            if (victim_sched.active_queues_bitmap != 0) {
                int p = __builtin_ctzll(victim_sched.active_queues_bitmap);

                // We found a queue. Steal the tail.
                if (!victim_sched.ready_queue[p].empty()) {
                    stolen = &victim_sched.ready_queue[p].back();
                    victim_sched.ready_queue[p].pop_back();

                    // Update victim's bitmap
                    if (victim_sched.ready_queue[p].empty()) {
                        victim_sched.active_queues_bitmap &= ~(1u << p);
                    }
                }
            }

            victim_sched.lock.unlock();

            if (stolen) {
                // Migrate the thread to this cpu
                stolen->cpu = cpu_manager.get_core_by_index(this->cpu_id);
                return stolen;
            }
        }
    }

    return nullptr;
}

bool Scheduler::check_for_higher_priority(int curr_level) {
    // Check if any queue above `curr_level` has threads waiting.
    for (int i = 0; i < curr_level; ++i) {
        if (!this->ready_queue[i].empty()) {
            return true;
        }
    }

    return false;
}

void Scheduler::schedule() {
    // Scheduler entry point: choose the next thread and perform a context switch.
    bool int_enabled = arch::interrupt_status();
    arch::disable_interrupts();

    cpu::PerCpuData* cpu = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id);
    Thread* prev         = cpu->curr_thread;

    // Select the next runnable thread under the scheduler lock.
    lock.lock();

    // If prev is currently Running, it means it was preempted.
    // We must download it to Ready and add it back to the run queue.
    // If it is Blocked or Zombie, we leave it alone.
    if (prev && prev->state == Running) {
        prev->state                = Ready;
        prev->wait_start_timestamp = this->current_ticks;

        this->ready_queue[prev->priority].push_back(*prev);
        this->active_queues_bitmap |= (1 << prev->priority);
    }

    // This unlinks next from the queue
    Thread* next = this->get_next_thread();
    lock.unlock();

    // If the scheduler picked the same thread, we simply mark it Running and return.
    if (prev == next) {
        if (prev->state != ThreadState::Zombie) {
            // Ensure state is correct
            next->state = Running;
        }

        if (int_enabled) {
            arch::enable_interrupts();
        }

        return;
    }

    Process* prev_proc = prev ? prev->owner : nullptr;
    Process* next_proc = next->owner;

    // Update CPU bookkeeping
    cpu->curr_thread = next;
    next->state      = Running;

    if ((!prev || prev_proc != next_proc) && next_proc) {
        memory::PcidManager* pcid_manager = cpu->pcid_manager;

        uint16_t pcid    = pcid_manager->get_pcid(next_proc);
        bool needs_flush = (next_proc->pcid_cache[cpu->core_idx] == static_cast<uint16_t>(-1));

        next_proc->map->load(pcid, needs_flush);
    }

    // Eager Switching
    if (prev && prev->state != Zombie) {
        this->save_fpu(prev->fpu_storage);
    }

    if (prev->state == Ready) {
        prev->wait_start_timestamp = this->current_ticks;
    }

    context_switch(prev, next);

    // When execution returns here, we are running on the `next` thread's stack.
    // However, inside this stack frame, the variable `prev` refers to
    // the current running thread (because it was `prev` when it suspended itself).
    // Therefore, we restore `prev` to load the state of the thread that just woke up.
    this->restore_fpu(prev->fpu_storage);

    if (int_enabled) {
        arch::enable_interrupts();
    }
}

cpu::IrqStatus Scheduler::tick() {
    this->current_ticks++;

    if (!this->sleeping_queue.empty()) {
        // Wake up Local Sleeper cells
        LockGuard guard(this->lock);

        while (!this->sleeping_queue.empty()) {
            Thread* t = this->sleeping_queue.top();

            if (!t || t->wake_time_ticks > this->current_ticks) {
                break;
            }

            this->sleeping_queue.extract_min(t);

            t->state   = Ready;
            t->quantum = TIME_SLICE_QUANTA[t->priority];

            this->ready_queue[t->priority].push_back(*t);
            this->active_queues_bitmap |= (1 << t->priority);
        }
    }

    cpu::PerCpuData* cpu = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id);

    // Wake up Thread Reaper (Maintenance)
    // Run Periodically (Every 2000 ticks / ~2 seconds)
    if ((this->current_ticks % GRIM_REAPER_INTERVAL == 0) && !this->zombie_list.empty()) {
        if (cpu->reaper_thread && cpu->reaper_thread->state != Ready) {
            this->unblock(cpu->reaper_thread);
        }
    }

    Thread* curr = cpu->curr_thread;

    // If we are Idle and a work just arrived (via wake up above, schedule immediately).
    if (curr == cpu->idle_thread) {
        if (this->active_queues_bitmap != 0) {
            this->schedule();
        }

        return cpu::IrqStatus::Handled;
    }

    if (curr->quantum > 0) {
        curr->quantum--;
    }

    if (curr->quantum <= 0) {
        LockGuard guard(this->lock);

        // Demote if not already at bottom
        if (curr->priority < MLFQ_LEVELS - 1) {
            curr->priority++;
        }

        curr->quantum = TIME_SLICE_QUANTA[curr->priority];
        curr->state   = Ready;

        this->ready_queue[curr->priority].push_back(*curr);
        this->active_queues_bitmap |= (1u << curr->priority);

        guard.unlock();

        this->schedule();
    } else {
        if (this->active_queues_bitmap != 0) {
            int highest_active_prio = __builtin_ctzll(this->active_queues_bitmap);

            if (highest_active_prio < curr->priority) {
                // A more important thread is ready, schedule immediately without
                // punishing the current thread (by demoting it).
                this->schedule();
            }
        }
    }

    return cpu::IrqStatus::Handled;
}

void Scheduler::boost_all() {
    LockGuard guard(this->lock);

    // Mask out the highest priority (bit 0). If the result i 0,
    // it means queues 1..N are all empty. Nothing to move.
    if ((this->active_queues_bitmap & ~1u) == 0) {
        // Sorry, Mr. Dijkstra
        goto update_curr;
    }

    // Move everyone from levels 1..MLFQ_LEVELS - 1 up to level 0
    for (int i = 1; i < MLFQ_LEVELS; ++i) {
        if (((this->active_queues_bitmap) & (1u << i)) == 0) {
            continue;
        }

        auto& src_queue  = this->ready_queue[i];
        auto& dest_queue = this->ready_queue[0];

        while (!src_queue.empty()) {
            Thread* t = &src_queue.front();
            src_queue.pop_front();

            t->priority = 0;
            t->quantum  = TIME_SLICE_QUANTA[0];

            dest_queue.push_back(*t);
        }

        // Clear bit for empty queue
        this->active_queues_bitmap &= ~(1u << i);
    }

    // Set bit for queue 0 (we definitely moved threads there)
    this->active_queues_bitmap |= 1;

update_curr:
    // Boost current thread too
    cpu::PerCpuData* cpu_data = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id);
    Thread* curr              = cpu_data->curr_thread;

    // Check if valid and not idle
    if (curr && curr != cpu_data->idle_thread) {
        // Avoid unnecessary writes
        if (curr->priority != 0) {
            curr->priority = 0;
            curr->quantum  = TIME_SLICE_QUANTA[0];
        }
    }
}

void Scheduler::sleep(size_t ms) {
    // If sleep time is 0, just yield the timeslice. Faster than
    // adding to sleep queue and dealing with heap sorting overhead.
    if (ms == 0) {
        this->yield();
        return;
    }

    bool int_status = arch::interrupt_status();
    arch::disable_interrupts();

    cpu::PerCpuData* cpu = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id);
    Thread* curr         = cpu->curr_thread;

    // Set wakeup time
    curr->wake_time_ticks = this->current_ticks + ms;
    curr->state           = ThreadState::Sleeping;

    {
        LockGuard guard(this->lock);
        this->sleeping_queue.insert(curr);
    }

    this->schedule();

    if (int_status) {
        arch::enable_interrupts();
    }
}

void Scheduler::yield() {
    bool int_status = arch::interrupt_status();
    arch::disable_interrupts();

    // Voluntary yield from the current thread back into the scheduler.
    cpu::PerCpuData* cpu = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id);

    {
        LockGuard guard(this->lock);

        // Check if anyone elese is actually waiting.
        // If the bitmap is empty, we are the only runnable thread.
        // Switching to ourselves is a waste of cache and TLB.
        if (this->active_queues_bitmap == 0) {
            if (int_status) {
                arch::enable_interrupts();
            }

            return;
        }

        Thread* curr = cpu->curr_thread;
        curr->state  = Ready;

        // If a thread yields voluntarily while having > 50% of its timeslice
        // left, it is "good behavior".
        const uint16_t max_slice = TIME_SLICE_QUANTA[curr->priority];
        const uint16_t used      = max_slice - curr->quantum;

        if (used < (max_slice / 2)) {
            if (curr->priority > 0) {
                curr->priority--;
            }
        }

        this->ready_queue[curr->priority].push_back(*curr);
        this->active_queues_bitmap |= (1 << curr->priority);
    }

    this->schedule();

    if (int_status) {
        arch::enable_interrupts();
    }
}

void Scheduler::block() {
    bool int_status = arch::interrupt_status();
    arch::disable_interrupts();

    cpu::PerCpuData* cpu = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id);
    Thread* curr         = cpu->curr_thread;

    const uint16_t max_slice = TIME_SLICE_QUANTA[curr->priority];
    const uint16_t used      = max_slice - curr->quantum;

    if (used < (max_slice / 2)) {
        if (curr->priority > 0) {
            curr->priority--;
        }
    }

    curr->quantum = TIME_SLICE_QUANTA[curr->priority];
    curr->state   = ThreadState::Blocked;

    this->schedule();

    if (int_status) {
        arch::enable_interrupts();
    }
}

void Scheduler::unblock(Thread* t) {
    // Target acquired
    cpu::PerCpuData* target_cpu = t->cpu;
    Scheduler& target_sched     = target_cpu->sched;

    {
        LockGuard guard(target_sched.lock);

        if (t->state == Ready) {
            return;
        }

        t->state = Ready;

        if (t->quantum <= 0) {
            t->quantum = TIME_SLICE_QUANTA[t->priority];
        }

        target_sched.ready_queue[t->priority].push_back(*t);
        target_sched.active_queues_bitmap |= (1 << t->priority);

        Thread* target_curr = target_cpu->curr_thread;

        if (target_curr == target_cpu->idle_thread || t->priority < target_curr->priority) {
            target_cpu->reschedule_needed = true;

            if (target_cpu->core_idx != this->cpu_id) {
                cpu::CpuCoreManager::get().send_ipi(target_cpu->core_idx, IPI_RESCHEDULE_VECTOR);
            }
        }
    }
}

void Scheduler::terminate() {
    // Terminate the current thread and switch to the next runnable one.
    arch::disable_interrupts();

    cpu::PerCpuData* cpu = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id);
    Thread* curr         = cpu->curr_thread;

    curr->state = Zombie;

    // Timestamp the death
    curr->last_run_timestamp = this->current_ticks;

    {
        LockGuard guard(zombie_lock);
        zombie_list.push_back(*curr);
    }

    lock.lock();
    Thread* next = this->get_next_thread();
    lock.unlock();

    Process* prev_proc = curr->owner;
    Process* next_proc = next->owner;

    if (prev_proc != next_proc && next_proc) {
        memory::PcidManager* pcid_manager = cpu->pcid_manager;

        uint16_t pcid    = pcid_manager->get_pcid(next_proc);
        bool needs_flush = (next_proc->pcid_cache[cpu->core_idx] == static_cast<uint16_t>(-1));

        next_proc->map->load(pcid, needs_flush);
    }

    cpu->curr_thread = next;
    next->state      = Running;

    // Restore Next Thread's FPU
    // Don't store 'curr' FPU state (it's ded)
    this->restore_fpu(next->fpu_storage);

    context_switch(curr, next);

    // Welcome from Afterlife, Mr. Zombie. The kernel is fucked.
    PANIC("Zombie wants REVENGE!");
}

void Scheduler::scan_for_starvation() {
    size_t now       = this->current_ticks;
    size_t threshold = 500;

    for (int prio = 1; prio < MLFQ_LEVELS; ++prio) {
        if ((this->active_queues_bitmap & (1u << prio)) == 0) {
            continue;
        }

        auto& queue = this->ready_queue[prio];
        auto it     = queue.begin();

        while (it != queue.end()) {
            // We are about to potentially remove *it.
            // Save the next iterator now, because after remove(),
            // `it` is detached and cannot find its successor.
            auto next_it = it;
            ++next_it;

            Thread* t = &*it;

            // Check starvation
            if ((now - t->wait_start_timestamp) > threshold) {
                queue.erase(it);

                // Boost to highest
                t->priority             = 0;
                t->wait_start_timestamp = now;

                // Add to the high-priority queue
                this->ready_queue[0].push_back(*t);
                this->active_queues_bitmap |= (1u << 0);

                if (queue.empty()) {
                    this->active_queues_bitmap &= ~(1u << prio);
                    break;
                }
            }

            it = next_it;
        }
    }
}

void Scheduler::reap_zombies() {
    IntrusiveList<Thread, SchedulerTag> death_row;

    {
        LockGuard guard(this->zombie_lock);

        if (this->zombie_list.empty()) {
            return;
        }

        // Move everything from global list to local list
        death_row.splice(death_row.end(), this->zombie_list);
    }

    auto it = death_row.begin();

    if (it == nullptr) {
        return;
    }

    while (it != death_row.end()) {
        auto next_it = it;
        ++next_it;

        Thread* t = &(*it);

        // Ensure the thread has been dead long enough for context switch
        // to have definitely finished on the original core.
        // 1ms is enough for now.
        if ((this->current_ticks - t->last_run_timestamp) <= 1) {
            death_row.remove(*t);
            this->zombie_lock.lock();
            this->zombie_list.push_back(*t);
            this->zombie_lock.unlock();
        } else {
            // Unlink from Process
            if (Process* owner = t->owner) {
                LockGuard proc_guard(owner->lock);
                owner->threads.remove(*t);
            }

            // Free Thread resources
            if (t->kernel_stack) {
                delete[] t->kernel_stack;
            }

            if (t->fpu_storage) {
                delete[] t->fpu_storage;
            }

            delete t;
        }

        it = next_it;
    }
}

Scheduler& Scheduler::get() {
    return cpu::CpuCoreManager::get().get_current_core()->sched;
}

void Scheduler::init(uint32_t id) {
    if (id == 0) {
        hal::Timer& timer = hal::Timer::get();

        timer.schedule(
            hal::TimerMode::Periodic, PRIORITY_BOOST_INTERVAL,
            [](void*) {
                Scheduler& sched = Scheduler::get();
                sched.boost_all();
            },
            nullptr);

        timer.schedule(
            hal::TimerMode::Periodic, STARVATION_CHECK_INTERVAL,
            [](void*) {
                Scheduler& sched = Scheduler::get();
                sched.scan_for_starvation();
            },
            nullptr);

        register_reschedule_handler();
    }

    this->cpu_id               = id;
    this->active_queues_bitmap = 0;
    this->ready_queue          = new IntrusiveList<Thread, SchedulerTag>[MLFQ_LEVELS];
}
}  // namespace kernel::task