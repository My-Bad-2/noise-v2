#pragma once

#include "cpu/exception.hpp"
#include "libs/deque.hpp"
#include "hal/interface/interrupt.hpp"
#include "libs/spinlock.hpp"
#include "task/process.hpp"

#define MAX_PRIORITY    32
#define DEFAULT_QUANTUM 20

namespace kernel::task {
struct Scheduler : public cpu::IInterruptHandler {
   public:
    Scheduler() = default;

    cpu::IrqStatus handle(cpu::arch::TrapFrame*) override;
    void init();

    void yield();
    void schedule();

    void block();
    void unblock(Thread* t);
    void terminate();

    void add_thread(Thread* t);

    const char* name() const override {
        return "Scheduler";
    }

    static Scheduler& get();

   private:
    Thread* get_next_thread();

    SpinLock lock;
    Deque<Thread*> ready_queue[MAX_PRIORITY];
};
}  // namespace kernel::task