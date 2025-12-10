#pragma once

#include <cstdint>
#include <cstddef>
#include "sched/thread.hpp"

namespace kernel::sched {
struct Process;
enum class ThreadState {
    New,
    Running,
    Ready,
    Stopped,
};

struct Thread {
    arch::Thread arch;
    size_t tid;
    size_t parent_pid;

    ThreadState state;
    size_t ticks;

    Thread(void (*entry_point)(void*), void* args, int arg_amount, Process* parent);
    ~Thread();
};

struct Process {
    
};
}  // namespace kernel::sched