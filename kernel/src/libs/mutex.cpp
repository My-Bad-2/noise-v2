#include "libs/mutex.hpp"
#include <atomic>
#include "arch.hpp"
#include "hal/smp_manager.hpp"
#include "hal/timer.hpp"
#include "task/scheduler.hpp"

namespace kernel {
void Mutex::timeout_callback(void* data) {
    WaitContext* ctx = static_cast<WaitContext*>(data);

    ctx->timed_out.store(true, std::memory_order_release);

    task::Scheduler& sched = cpu::CpuCoreManager::get().get_current_core()->sched;
    sched.unblock(ctx->thread);
}

bool Mutex::lock_slow(size_t ms) {
    int spin_count = 0;

    while (true) {
        int32_t v = state.load(std::memory_order_relaxed);

        // If locked (1) but not contended (2), spin briefly to avoid to overhead.
        if (v == 1 && spin_count < SPIN_LIMIT) {
            arch::pause();
            spin_count++;
            continue;
        }

        // Try to acquire if it looks free (0)
        if (v == 0) {
            if (this->state.compare_exchange_weak(v, 1, std::memory_order_acquire)) {
                return true;
            }

            continue;
        }

        // Mark as Contended (2)
        // Before sleeping, we must ensure state is 2 so unlock() knows to wake us.
        if (v == 1) {
            if (!state.compare_exchange_weak(v, 2, std::memory_order_relaxed)) {
                continue;
            }
        }

        // Check for timeout
        if (ms == 0) {
            return false;
        }

        // Prepare to sleep
        cpu::PerCpuData* cpu = cpu::CpuCoreManager::get().get_current_core();
        task::Thread* me     = cpu->curr_thread;
        WaitContext ctx;
        ctx.thread    = me;
        ctx.mutex_ref = this;

        // Add self to internal wait queue
        this->add_waiter(me);

        // Schedule timeout (if not infinite)
        if (ms != static_cast<size_t>(-1)) {
            hal::Timer& timer = hal::Timer::get();
            timer.schedule(hal::OneShot, ms, timeout_callback, &ctx);
        }

        // Block the thread
        task::Scheduler& sched = cpu->sched;
        sched.block();

        // Remove self from wait queue (if still there)
        this->remove_waiter(me);

        // Did we time out?
        if (ms != static_cast<size_t>(-1) && ctx.timed_out.load(std::memory_order_acquire)) {
            return false;
        }

        // If not timeout, we were woken by unlock().
        // We loop back to the top to compete for the lock again.
        // Reset spin count to give it a fresh chance.
        spin_count = 0;
    }
}

void Mutex::add_waiter(task::Thread* t) {
    LockGuard guard(this->queue_lock);

    WaitNode* node = new WaitNode(t, nullptr);

    if (!this->wait_head) {
        this->wait_head = node;
        this->wait_tail = node;
    } else {
        this->wait_tail->next = node;
        this->wait_tail       = node;
    }
}

void Mutex::remove_waiter(task::Thread* t) {
    LockGuard guard(this->queue_lock);

    WaitNode* curr = this->wait_head;
    WaitNode* prev = nullptr;

    while (curr) {
        if (curr->thread == t) {
            if (prev) {
                prev->next = curr->next;
            } else {
                this->wait_head = curr->next;
            }

            if (curr == this->wait_tail) {
                this->wait_tail = prev;
            }

            delete curr;
            break;
        }

        prev = curr;
        curr = curr->next;
    }
}

void Mutex::wakeup_next() {
    LockGuard guard(this->queue_lock);

    if (this->wait_head) {
        task::Thread* t      = this->wait_head->thread;
        cpu::PerCpuData* cpu = cpu::CpuCoreManager::get().get_current_core();
        cpu->sched.unblock(t);

        WaitNode* old   = this->wait_head;
        this->wait_head = this->wait_head->next;

        if (!this->wait_head) {
            this->wait_tail = nullptr;
        }

        delete old;
    }
}

bool Mutex::lock(size_t ms) {
    int32_t expected = 0;

    // If unlocked, grab it immediately
    if (state.compare_exchange_strong(expected, 1, std::memory_order_acquire)) {
        return true;
    }

    return this->lock_slow(ms);
}

void Mutex::unlock() {
    // If we transition 1 -> 0, no one was waiting. We are done.
    if (this->state.fetch_sub(1, std::memory_order_release) == 1) {
        return;
    }

    this->state.store(0, std::memory_order_release);
    this->wakeup_next();
}
}  // namespace kernel