#include "task/scheduler.hpp"
#include "arch.hpp"
#include "hal/cpu.hpp"
#include "hal/interface/interrupt.hpp"
#include "hal/timer.hpp"
#include "libs/log.hpp"
#include "task/process.hpp"

// Low-level context switch routine implemented in architecture-specific assembly.
extern "C" void context_switch(kernel::task::Thread* prev, kernel::task::Thread* next);

namespace kernel::task {
namespace {
// Global tick counter used to trigger periodic scheduler maintenance (e.g. aging).
uint64_t total_ticks = 0;

static cpu::IrqStatus scheduler_callback() {
    Scheduler& sched = Scheduler::get();
    return sched.tick();
}
}  // namespace

Thread* Scheduler::get_next_thread() {
    // Pick the next runnable thread, starting from the highest priority queue.
    for (int i = MAX_PRIORITY - 1; i >= 0; i--) {
        if (!this->ready_queue[i].empty()) {
            Thread* t = this->ready_queue[i].front();
            this->ready_queue[i].pop_front();
            return t;
        }
    }

    // No runnable threads, fall back to the per-CPU idle thread.
    return cpu::CPUCoreManager::get_curr_cpu()->idle_thread;
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

    Process* prev_proc = prev->owner;
    Process* next_proc = next->owner;

    if (prev_proc->map->is_dirty) {
        prev_proc->map->load();
        prev_proc->map->is_dirty = false;
    }

    // Nothing to do if we're going to continue running the same thread.
    if (prev == next) {
        if (int_enabled) {
            arch::enable_interrupts();
        }

        return;
    }

    // Switch bookkeeping to the next thread before jumping into assembly.
    cpu->curr_thread   = next;
    next->thread_state = Running;

    if (prev->tid != next->tid) {
        LOG_DEBUG("Scheduler: context switch T%lu (%s) -> T%lu (%s) [prio=%u]", prev->tid,
                  (prev->thread_state == ThreadState::Zombie) ? "Zombie" : "Ready", next->tid,
                  "Running", next->priority);
    }

    total_ticks++;

    // Drive slow-path maintenance work (like priority aging) from scheduler ticks.
    if ((total_ticks % 1000) == 0) {
        this->aging_tick();
    }

    if (prev_proc != next_proc) {
        // Switch address space
        if (next_proc) {
            next_proc->map->load();
            next_proc->map->is_dirty = false;
        }
    }

    context_switch(prev, next);

    if (int_enabled) {
        arch::enable_interrupts();
    }
}

void Scheduler::add_thread(Thread* t) {
    // New threads always start with a full quantum and in the ready state.
    t->quantum      = DEFAULT_QUANTUM;
    t->thread_state = ThreadState::Ready;

    uint32_t p = t->priority;
    if (p >= MAX_PRIORITY) {
        p = MAX_PRIORITY - 1;
    }

    lock.lock();
    this->ready_queue[p].push_back(t);
    lock.unlock();
}

void Scheduler::block() {
    // Block the current thread and reschedule.
    arch::disable_interrupts();

    cpu::PerCPUData* cpu = cpu::CPUCoreManager::get_curr_cpu();
    Thread* curr         = cpu->curr_thread;

    curr->thread_state = ThreadState::Blocked;

    // If the thread used only a part of its quantum, reward it with
    // a slightly higher priority (interactive / I/O-friendly behavior).
    uint32_t used_ticks = DEFAULT_QUANTUM - curr->quantum;

    if (used_ticks < (DEFAULT_QUANTUM / 2)) {
        if (curr->priority < (MAX_PRIORITY - 1)) {
            curr->priority++;
        }
    }

    curr->quantum = DEFAULT_QUANTUM;

    this->schedule();
}

void Scheduler::yield() {
    // Voluntary yield from the current thread back into the scheduler.
    cpu::PerCPUData* cpu           = cpu::CPUCoreManager::get_curr_cpu();
    cpu->curr_thread->thread_state = ThreadState::Ready;

    this->add_thread(cpu->curr_thread);

    this->schedule();
}

cpu::IrqStatus Scheduler::tick() {
    // Timer interrupt handler: charge quanta and reschedule when needed.
    cpu::PerCPUData* cpu = cpu::CPUCoreManager::get_curr_cpu();
    Thread* curr         = cpu->curr_thread;

    if (curr == cpu->idle_thread) {
        this->schedule();
        return cpu::IrqStatus::Handled;
    }

    if (curr->quantum > 0) {
        curr->quantum--;
    }

    if (curr->quantum == 0) {
        // Thread exhausted its slice of CPU, move it to a lower priority.
        if (curr->priority > 0) {
            curr->priority--;
        }

        curr->quantum      = DEFAULT_QUANTUM;
        curr->thread_state = Ready;

        lock.lock();
        this->ready_queue[curr->priority].push_back(curr);
        lock.unlock();

        this->schedule();
    }

    return cpu::IrqStatus::Handled;
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

void Scheduler::aging_tick() {
    // Periodically increase the priority of ready threads to avoid starvation.
    LockGuard guard(this->lock);

    for (int p = 0; p < MAX_PRIORITY - 1; p--) {
        if (this->ready_queue[p].empty()) {
            continue;
        }

        while (!this->ready_queue[p].empty()) {
            Thread* t = this->ready_queue[p].front();
            this->ready_queue[p].pop_front();

            t->priority++;
            t->quantum = DEFAULT_QUANTUM;

            this->ready_queue[t->priority].push_back(t);
        }
    }
}

void Scheduler::init() {
    hal::Timer::configure_timer(1, hal::Periodic, 32, scheduler_callback);
}
}  // namespace kernel::task