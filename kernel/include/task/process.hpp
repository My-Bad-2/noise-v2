#pragma once

#include <atomic>
#include "libs/vector.hpp"
#include "memory/pagemap.hpp"
#include "libs/spinlock.hpp"
#include "memory/vma.hpp"
#include "libs/intrusive_list.hpp"

namespace kernel::cpu {
struct PerCpuData;
}

namespace kernel::task {
struct Process;

enum ThreadState : uint32_t {
    Ready     = (1 << 0),
    Running   = (1 << 1),
    Blocked   = (1 << 2),
    Sleeping  = (1 << 3),
    Zombie    = (1 << 4),
};

struct SchedulerTag {};
struct ProcessTag {};

struct Thread : public IntrusiveListNode<SchedulerTag>, public IntrusiveListNode<ProcessTag> {
    size_t tid;
    uintptr_t kernel_stack_ptr;

    cpu::PerCpuData* cpu;
    ThreadState state;
    uint16_t priority;
    uint16_t quantum;

    std::byte* fpu_storage;
    size_t wake_time_ticks;
    size_t wait_start_timestamp;
    size_t last_run_timestamp;

    Process* owner;
    std::byte* kernel_stack;
    bool is_user_thread;

    Thread() = default;
    Thread(Process* parent, void (*callback)(void*), void* args, void* curr_cpu = nullptr,
           bool is_user = false);
    ~Thread() {}

   private:
    void arch_init(uintptr_t entry, uintptr_t arg);

    static std::byte* clean_fpu_state;
    static size_t fpu_state_size;
    static std::align_val_t fpu_alignment;
};

struct Process {
    size_t pid;
    memory::PageMap* map;
    SpinLock lock;

    Vector<Process*> children;
    Vector<Thread*> threads;

    int exit_code;
    std::atomic<size_t> next_tid;

    uint16_t* pcid_cache;
    memory::VirtualAllocator user_vmm;

    Process(memory::PageMap* map);
    Process();
    ~Process();

    void* mmap(size_t count, memory::PageSize size, uint8_t flags);
    void munmap(void* addr, size_t count, memory::PageSize size);

    static void init();
};

extern Process* kernel_proc;
}  // namespace kernel::task