#include "task/scheduler.hpp"
#include "arch.hpp"
#include "cpu/exception.hpp"
#include "hal/cpu.hpp"
#include "hal/interface/interrupt.hpp"
#include "libs/log.hpp"
#include "task/process.hpp"

extern "C" void context_switch(kernel::task::Thread* prev, kernel::task::Thread* next);

namespace kernel::task {
Thread* Scheduler::get_next_thread() {
    for (int i = MAX_PRIORITY - 1; i >= 0; i--) {
        if (!this->ready_queue[i].empty()) {
            Thread* t = this->ready_queue[i].front();
            this->ready_queue[i].pop_front();
            return t;
        }
    }

    return cpu::CPUCoreManager::get_curr_cpu()->idle_thread;
}

void Scheduler::schedule() {
    bool int_enabled = arch::interrupt_status();
    arch::disable_interrupts();

    cpu::PerCPUData* cpu = cpu::CPUCoreManager::get_curr_cpu();
    Thread* prev         = cpu->curr_thread;

    lock.lock();
    Thread* next = this->get_next_thread();
    lock.unlock();

    if (prev == next) {
        if (int_enabled) {
            arch::enable_interrupts();
        }

        return;
    }

    cpu->curr_thread   = next;
    next->thread_state = Running;

    if (prev->tid != next->tid) {
        LOG_DEBUG("Context Switch: Thread %lu (%s) -> Thread %lu (%s) [Prio: %u]", prev->tid,
                  (prev->thread_state == ThreadState::Zombie) ? "Zombie" : "Ready", next->tid,
                  "Running", next->priority);
    }

    context_switch(prev, next);

    if (int_enabled) {
        arch::enable_interrupts();
    }
}

void Scheduler::add_thread(Thread* t) {
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
    cpu::PerCPUData* cpu           = cpu::CPUCoreManager::get_curr_cpu();
    cpu->curr_thread->thread_state = ThreadState::Blocked;
    this->schedule();
}

void Scheduler::yield() {
    cpu::PerCPUData* cpu           = cpu::CPUCoreManager::get_curr_cpu();
    cpu->curr_thread->thread_state = ThreadState::Ready;

    this->add_thread(cpu->curr_thread);

    this->schedule();
}

cpu::IrqStatus Scheduler::handle(cpu::arch::TrapFrame*) {
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
        if (curr->priority > 0) {
            curr->priority--;
        }

        curr->quantum      = DEFAULT_QUANTUM;
        curr->thread_state = Ready;

        uint32_t p = curr->priority;

        if (p >= MAX_PRIORITY) {
            p = MAX_PRIORITY - 1;
        }

        lock.lock();
        this->ready_queue[curr->priority].push_back(curr);
        lock.unlock();

        this->schedule();
    }

    return cpu::IrqStatus::Handled;
}

Scheduler& Scheduler::get() {
    static Scheduler* sched = nullptr;

    if (sched != nullptr) {
        return *sched;
    }

    sched = new Scheduler;
    return *sched;
}

void Scheduler::terminate() {
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
}  // namespace kernel::task