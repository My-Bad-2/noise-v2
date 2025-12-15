#pragma once

#include "memory/pagemap.hpp"

namespace kernel::cpu {
struct PerCPUData;
}

namespace kernel::task {
struct Thread;

enum ThreadState : uint32_t {
    Ready     = (1 << 0),
    Running   = (1 << 1),
    Blocked   = (1 << 2),
    Suspended = (1 << 3),
    Zombie    = (1 << 4),
};

struct Process {
    size_t pid;
    memory::PageMap map;
    // Deque<Thread> threads; // Use a vector instead

    Process();
    ~Process();
};

struct Thread {
    size_t tid;
    uintptr_t kernel_stack_ptr;

    cpu::PerCPUData* cpu;
    std::byte* kernel_stack;
    Process* parent;
    
    ThreadState thread_state;
    uint16_t priority;
    uint16_t quantum;

    Thread() = default;
    Thread(Process* parent, void (*callback)(void*), void* args);
    ~Thread() {}

   private:
    void arch_init(uintptr_t entry, uintptr_t arg);
};
}  // namespace kernel::task