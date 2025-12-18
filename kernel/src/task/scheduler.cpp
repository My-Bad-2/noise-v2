#include <cstdint>
#include "hal/smp_manager.hpp"
#include "hal/timer.hpp"
#include "libs/log.hpp"
#include "memory/pcid_manager.hpp"

// Low-level context switch routine implemented in architecture-specific assembly.
extern "C" void context_switch(kernel::task::Thread* prev, kernel::task::Thread* next);

namespace kernel::task {
namespace {
constexpr uint16_t TIME_SLICE_QUANTA[MLFQ_LEVELS] = {10, 20, 40, 80};
}  // namespace

void Scheduler::add_thread(Thread* t) {
    LockGuard guard(this->lock);

    if (t->priority >= MLFQ_LEVELS) {
        t->priority = MLFQ_LEVELS - 1;
    }

    if (t->quantum <= 0) {
        t->quantum = TIME_SLICE_QUANTA[t->priority];
    }

    t->thread_state = Ready;
    t->cpu          = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id);

    this->ready_queue[t->priority].push_back(t);
    this->active_queues_bitmap |= (1 << t->priority);
}

Thread* Scheduler::get_next_thread() {
    // Pick thread from the highest priority none-empty queue
    if (this->active_queues_bitmap != 0) {
        int highest_prio = __builtin_ctz(this->active_queues_bitmap);

        Thread* t = this->ready_queue[highest_prio].front();
        this->ready_queue[highest_prio].pop_front();

        if (this->ready_queue[highest_prio].empty()) {
            active_queues_bitmap &= ~(1u << highest_prio);
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
    // Iterate over all other CPUs
    size_t max_cpus = cpu::CpuCoreManager::get().get_total_cores();

    for (size_t i = 0; i < max_cpus; ++i) {
        if (i == this->cpu_id) {
            // Don't steal from your own house
            continue;
        }

        cpu::PerCpuData* victim_cpu =
            cpu::CpuCoreManager::get().get_core_by_index(static_cast<uint32_t>(i));
        Scheduler& victim_sched = victim_cpu->sched;

        // Use `try_lock()` to avoid deadlock
        // If core A tries to steal from B, and B from A, locking hangs the kernel.
        if (victim_sched.lock.try_lock()) {
            Thread* stolen = nullptr;

            // Take from the tail of the highest priority queue available.
            // Ideally, we want to help with the most important work, but taking from tail
            // minimizes cache thrashing for the victim.
            if (victim_sched.active_queues_bitmap != 0) {
                int p = __builtin_ctz(victim_sched.active_queues_bitmap);

                // We found a queue. Steal the tail.
                if (!victim_sched.ready_queue[p].empty()) {
                    stolen = victim_sched.ready_queue[p].back();
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
                stolen->cpu = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id);
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

    cpu::PerCpuData* cpu              = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id);
    memory::PcidManager* pcid_manager = cpu->pcid_manager;
    Thread* prev                      = cpu->curr_thread;

    // Select the next runnable thread under the scheduler lock.
    lock.lock();
    Thread* next = this->get_next_thread();
    lock.unlock();

    // Nothing to do if we're going to continue running the same thread.
    if ((prev == next) && (prev->thread_state != ThreadState::Zombie)) {
        if (int_enabled) {
            arch::enable_interrupts();
        }

        return;
    }

    Process* prev_proc = prev->owner;
    Process* next_proc = next->owner;

    uint16_t pcid    = pcid_manager->get_pcid(next_proc);
    bool needs_flush = (next_proc->pcid_cache[cpu->core_idx] == static_cast<uint16_t>(-1));

    // Switch bookkeeping to the next thread before jumping into assembly.
    cpu->curr_thread   = next;
    next->thread_state = Running;

    if ((prev_proc != next_proc) && next_proc) {
        // Switch address space
        next_proc->map->load(pcid, needs_flush);
    }

    context_switch(prev, next);

    if (int_enabled) {
        arch::enable_interrupts();
    }
}

cpu::IrqStatus Scheduler::tick() {
    this->current_ticks++;

    {
        // Wake up Local Sleeper cells
        LockGuard guard(this->lock);

        while (!this->sleeping_queue.empty()) {
            Thread* t = this->sleeping_queue.top();
            if (!t || t->wake_time_ticks > this->current_ticks) {
                break;
            }

            this->sleeping_queue.extract_min(t);

            t->thread_state = Ready;
            t->quantum      = TIME_SLICE_QUANTA[t->priority];

            this->ready_queue[t->priority].push_back(t);
            this->active_queues_bitmap |= (1 << t->priority);
        }
    }

    cpu::PerCpuData* cpu = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id);
    Thread* curr         = cpu->curr_thread;

    if (curr == cpu->idle_thread) {
        this->schedule();
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

        curr->quantum      = TIME_SLICE_QUANTA[curr->priority];
        curr->thread_state = Ready;

        this->ready_queue[curr->priority].push_back(curr);
        this->active_queues_bitmap |= (1u << curr->priority);

        guard.unlock();
        this->schedule();
    } else {
        LockGuard guard(this->lock);
        bool higher_priority_ready = this->check_for_higher_priority(curr->priority);

        if (higher_priority_ready) {
            guard.unlock();
            this->yield();
        }
    }

    return cpu::IrqStatus::Handled;
}

void Scheduler::boost_all() {
    LockGuard guard(this->lock);

    // Move everyone from levels 1..3 up to level 0
    for (int i = 1; i < MLFQ_LEVELS; ++i) {
        while (!this->ready_queue[i].empty()) {
            Thread* t = this->ready_queue[i].front();
            this->ready_queue[i].pop_front();

            t->priority = 0;
            t->quantum  = TIME_SLICE_QUANTA[0];

            this->ready_queue[0].push_back(t);
        }

        // Clear bit for empty queue
        this->active_queues_bitmap &= ~(1u << i);
    }

    // Set bit for queue 0 (if we moved anything)
    if (!this->ready_queue[0].empty()) {
        active_queues_bitmap |= (1 << 0);
    }

    // Boost current thread too
    Thread* curr = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id)->curr_thread;
    if (curr && curr != cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id)->idle_thread) {
        curr->priority = 0;
        curr->quantum  = TIME_SLICE_QUANTA[0];
    }
}

void Scheduler::sleep(size_t ms) {
    bool int_status = arch::interrupt_status();
    arch::disable_interrupts();

    cpu::PerCpuData* cpu = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id);
    Thread* curr         = cpu->curr_thread;

    // Set thread state and wakeup time
    curr->wake_time_ticks = this->current_ticks + ms;
    curr->thread_state    = ThreadState::Sleeping;

    lock.lock();
    this->sleeping_queue.insert(curr);
    lock.unlock();

    this->schedule();

    if (int_status) {
        arch::enable_interrupts();
    }
}

void Scheduler::yield() {
    // Voluntary yield from the current thread back into the scheduler.
    cpu::PerCpuData* cpu = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id);
    Thread* curr         = cpu->curr_thread;

    curr->thread_state = Ready;

    lock.lock();
    this->active_queues_bitmap |= (1 << curr->priority);
    this->ready_queue[curr->priority].push_back(curr);
    lock.unlock();

    this->schedule();
}

void Scheduler::block() {
    // Block the current thread and reschedule.
    arch::disable_interrupts();

    cpu::PerCpuData* cpu = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id);
    Thread* curr         = cpu->curr_thread;

    curr->thread_state = ThreadState::Blocked;

    // Promote if blocked before using significant quantum
    const uint16_t curr_slice_max = TIME_SLICE_QUANTA[curr->priority];
    uint16_t used_ticks           = curr_slice_max - curr->quantum;

    if (used_ticks < (curr_slice_max / 2)) {
        if (curr->priority > 0) {
            curr->priority--;
        }
    }

    // Ensure quantum is reset for when it wakes up
    curr->quantum = TIME_SLICE_QUANTA[curr->priority];

    this->schedule();
}

void Scheduler::unblock(Thread* t) {
    // Target acquired
    cpu::PerCpuData* target_cpu = t->cpu;
    Scheduler& target_sched     = target_cpu->sched;

    {
        LockGuard guard(target_sched.lock);

        t->thread_state = Ready;

        if (t->quantum <= 0) {
            t->quantum = TIME_SLICE_QUANTA[t->priority];
        }

        target_sched.ready_queue[t->priority].push_back(t);
        target_sched.active_queues_bitmap |= (1 << t->priority);
    }

    // If thread is on a different CPU, we might need to wake it up immediately
    if (target_cpu->core_idx != this->cpu_id) {
        // Only send IPI if the new thread is important.
        int current_prio = target_cpu->curr_thread->priority;

        bool is_idle = (target_cpu->curr_thread == target_cpu->idle_thread);

        if (is_idle || (t->priority < current_prio)) {
            cpu::CpuCoreManager::get().send_ipi(target_cpu->apic_id, IPI_RESCHEDULE_VECTOR);
        }
    }
}

void Scheduler::terminate() {
    // Terminate the current thread and switch to the next runnable one.
    arch::disable_interrupts();

    cpu::PerCpuData* cpu = cpu::CpuCoreManager::get().get_core_by_index(this->cpu_id);
    Thread* curr         = cpu->curr_thread;

    curr->thread_state = Zombie;

    lock.lock();
    Thread* next = this->get_next_thread();
    lock.unlock();

    cpu->curr_thread   = next;
    next->thread_state = Running;

    context_switch(curr, next);

    PANIC("Zombie wants REVENGE!");
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

        register_reschedule_handler();
    }

    this->cpu_id               = id;
    this->active_queues_bitmap = 0;
    this->ready_queue          = new Deque<Thread*>[MLFQ_LEVELS];
}
}  // namespace kernel::task