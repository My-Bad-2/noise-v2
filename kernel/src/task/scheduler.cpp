#include "task/scheduler.hpp"
#include "arch.hpp"
#include "hal/cpu.hpp"
#include "hal/interface/interrupt.hpp"
#include "hal/timer.hpp"
#include "libs/log.hpp"
#include "libs/spinlock.hpp"
#include "task/process.hpp"

// Low-level context switch routine implemented in architecture-specific assembly.
extern "C" void context_switch(kernel::task::Thread* prev, kernel::task::Thread* next);

namespace kernel::task {
namespace {
volatile size_t total_ticks = 0;

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
    this->ready_queue[t->priority].push_back(t);
}

Thread* Scheduler::get_next_thread() {
    // Pick thread from the highest priority none-empty queue
    for (int i = 0; i < MLFQ_LEVELS; ++i) {
        if (!this->ready_queue[i].empty()) {
            Thread* t = this->ready_queue[i].front();
            this->ready_queue[i].pop_front();
            return t;
        }
    }

    // No runnable threads, fall back to the per-CPU idle thread.
    return cpu::CPUCoreManager::get_curr_cpu()->idle_thread;
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

    cpu::PerCPUData* cpu = cpu::CPUCoreManager::get_curr_cpu();
    Thread* prev         = cpu->curr_thread;

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

    if (prev_proc->map->is_dirty) {
        prev_proc->map->load();
        prev_proc->map->is_dirty = false;
    }

    // Switch bookkeeping to the next thread before jumping into assembly.
    cpu->curr_thread   = next;
    next->thread_state = Running;

    if (prev->tid != next->tid) {
        LOG_DEBUG("Scheduler: context switch T%lu (%s) -> T%lu (%s) [prio=%u]", prev->tid,
                  (prev->thread_state == ThreadState::Zombie) ? "Zombie" : "Ready", next->tid,
                  "Running", next->priority);
    }

    if ((prev_proc != next_proc) && next_proc) {
        // Switch address space
        next_proc->map->load();
        next_proc->map->is_dirty = false;
    }

    context_switch(prev, next);

    if (int_enabled) {
        arch::enable_interrupts();
    }
}

cpu::IrqStatus Scheduler::tick() {
    total_ticks++;

    {
        LockGuard guard(this->lock);

        while (!this->sleeping_queue.empty()) {
            Thread* t = this->sleeping_queue.top();

            if (!t || (t->wake_time_ticks > total_ticks)) {
                break;
            }

            this->sleeping_queue.extract_min(t);

            t->thread_state = Ready;
            t->quantum      = TIME_SLICE_QUANTA[t->priority];

            this->ready_queue[t->priority].push_back(t);
        }
    }

    cpu::PerCPUData* cpu = cpu::CPUCoreManager::get_curr_cpu();
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

        guard.unlock();
        this->schedule();
        return cpu::IrqStatus::Handled;
    }

    LockGuard guard(this->lock);
    bool higher_priority_ready = this->check_for_higher_priority(curr->priority);

    if (higher_priority_ready) {
        guard.unlock();
        this->yield();
        guard.lock();
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
    }
}

void Scheduler::sleep(size_t ms) {
    arch::disable_interrupts();
    cpu::PerCPUData* cpu = cpu::CPUCoreManager::get_curr_cpu();
    Thread* curr         = cpu->curr_thread;

    // Calculate wake time
    curr->wake_time_ticks = total_ticks + ms;
    curr->thread_state    = ThreadState::Sleeping;

    lock.lock();
    this->sleeping_queue.insert(curr);
    lock.unlock();

    this->schedule();
}

void Scheduler::yield() {
    // Voluntary yield from the current thread back into the scheduler.
    cpu::PerCPUData* cpu = cpu::CPUCoreManager::get_curr_cpu();
    Thread* curr         = cpu->curr_thread;

    curr->thread_state = Ready;

    lock.lock();
    this->ready_queue[curr->priority].push_back(curr);
    lock.unlock();

    this->schedule();
}

void Scheduler::block() {
    // Block the current thread and reschedule.
    arch::disable_interrupts();

    cpu::PerCPUData* cpu = cpu::CPUCoreManager::get_curr_cpu();
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
    // Just add it back
    this->add_thread(t);
}

void Scheduler::terminate() {
    // Terminate the current thread and switch to the next runnable one.
    arch::disable_interrupts();

    cpu::PerCPUData* cpu = cpu::CPUCoreManager::get_curr_cpu();
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
    // Singleton-style accessor used throughout the kernel.
    static Scheduler* sched = nullptr;

    if (sched != nullptr) {
        return *sched;
    }

    sched = new Scheduler;
    return *sched;
}

void Scheduler::init() {
    hal::Timer& timer = hal::Timer::get();

    timer.schedule(
        hal::TimerMode::Periodic, PRIORITY_BOOST_INTERVAL,
        [](void*) {
            Scheduler& sched = Scheduler::get();
            sched.boost_all();
        },
        nullptr);
}
}  // namespace kernel::task