#pragma once

#include <atomic>
#include <cstdint>

#include "task/process.hpp"
#include "libs/spinlock.hpp"

namespace kernel {
class Mutex {
   public:
    Mutex()                        = default;
    Mutex(const Mutex&)            = delete;
    Mutex& operator=(const Mutex&) = delete;

    bool lock(size_t ms = static_cast<size_t>(-1));
    void unlock();

   private:
    static void timeout_callback(void* data);
    bool lock_slow(size_t ms);

    void add_waiter(task::Thread* t);
    void remove_waiter(task::Thread* t);
    void wakeup_next();

    std::atomic<int32_t> state{0};

    struct WaitNode {
        task::Thread* thread;
        WaitNode* next;
    };

    WaitNode* wait_head = nullptr;
    WaitNode* wait_tail = nullptr;
    SpinLock queue_lock;

    struct WaitContext {
        task::Thread* thread;
        std::atomic<bool> timed_out{false};
        Mutex* mutex_ref;
    };

    static constexpr int SPIN_LIMIT = 100;
};
}  // namespace kernel