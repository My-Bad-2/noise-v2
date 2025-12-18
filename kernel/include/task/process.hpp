#pragma once

#include <atomic>
#include "libs/vector.hpp"
#include "memory/pagemap.hpp"
#include "libs/spinlock.hpp"

namespace kernel::cpu {
struct PerCpuData;
}

namespace kernel::task {
struct Thread;

enum ThreadState : uint32_t {
    Ready     = (1 << 0),
    Running   = (1 << 1),
    Blocked   = (1 << 2),
    Suspended = (1 << 3),
    Sleeping  = (1 << 4),
    Zombie    = (1 << 5),
};

struct Process {
    size_t pid;
    memory::PageMap* map;

    Vector<Process*> children;
    Vector<Thread*> threads;

    int exit_code;
    std::atomic<size_t> next_tid;
    SpinLock lock;

    uint16_t* pcid_cache;

    Process(memory::PageMap* map);
    Process();
    ~Process();

    static void init();
};

struct Thread {
    size_t tid;
    uintptr_t kernel_stack_ptr;

    cpu::PerCpuData* cpu;
    std::byte* kernel_stack;
    Process* owner;

    ThreadState thread_state;
    uint16_t priority;
    uint16_t quantum;

    size_t wake_time_ticks;

    Thread() = default;
    Thread(Process* parent, void (*callback)(void*), void* args, void* curr_cpu = nullptr);
    ~Thread() {}

   private:
    void arch_init(uintptr_t entry, uintptr_t arg);
};

extern Process* kernel_proc;
}  // namespace kernel::task