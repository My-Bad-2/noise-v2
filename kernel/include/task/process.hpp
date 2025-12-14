#pragma once

#include <cstdint>
#include <cstddef>
#include "memory/pagemap.hpp"
#include "libs/deque.hpp"

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
    Deque<Thread> threads;

    Process();
    ~Process();
};

struct Thread {
    size_t tid;
    uintptr_t kernel_stack_ptr;

    cpu::PerCPUData* cpu;
    
    ThreadState thread_state;
    std::byte* kernel_stack;

    Process* parent;

    uint32_t quantum;

    Thread() = default;
    Thread(Process* parent, void (*callback)(void*), void* args);
    ~Thread() {}

   private:
    void arch_init(uintptr_t entry, uintptr_t arg);
};
}  // namespace kernel::task